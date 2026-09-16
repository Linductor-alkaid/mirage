#include "../support/ipc_io.hpp"

#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace {

namespace ipc = mirage::runtime::ipc;
using mirage::testing::FrameRead;
using mirage::testing::TempDir;

constexpr auto kBudget = std::chrono::seconds{3};

// --- framing ----------------------------------------------------------------

void scenario_framing_round_trip() {
    std::string buffer = ipc::make_frame("framing hello");
    MIRAGE_CHECK(buffer.size() == ipc::kFrameHeaderBytes + 13);

    const ipc::FrameExtraction extraction = ipc::try_extract_frame(buffer);
    MIRAGE_CHECK(extraction.status == ipc::FrameExtract::Message);
    MIRAGE_CHECK(extraction.message == "framing hello");
    MIRAGE_CHECK(buffer.empty());
}

void scenario_framing_little_endian_header() {
    const std::string payload(300, 'x');
    const std::string frame = ipc::make_frame(payload);
    MIRAGE_CHECK(frame.size() == 304);
    MIRAGE_CHECK(static_cast<unsigned char>(frame[0]) == 0x2C);
    MIRAGE_CHECK(static_cast<unsigned char>(frame[1]) == 0x01);
    MIRAGE_CHECK(static_cast<unsigned char>(frame[2]) == 0x00);
    MIRAGE_CHECK(static_cast<unsigned char>(frame[3]) == 0x00);
}

void scenario_framing_empty_payload() {
    std::string buffer = ipc::make_frame("");
    const ipc::FrameExtraction extraction = ipc::try_extract_frame(buffer);
    MIRAGE_CHECK(extraction.status == ipc::FrameExtract::Message);
    MIRAGE_CHECK(extraction.message.empty());
    MIRAGE_CHECK(buffer.empty());
}

void scenario_framing_partial_feeds() {
    // One frame delivered in growing chunks: every incomplete prefix is
    // NeedMoreData, the complete stream yields exactly one Message.
    const std::string frame = ipc::make_frame("half-packet-payload");
    std::string buffer;

    buffer.append(frame, 0, 1);
    MIRAGE_CHECK(ipc::try_extract_frame(buffer).status == ipc::FrameExtract::NeedMoreData);
    buffer.append(frame, 1, 2);
    MIRAGE_CHECK(ipc::try_extract_frame(buffer).status == ipc::FrameExtract::NeedMoreData);
    buffer.append(frame, 3, 1);
    MIRAGE_CHECK(ipc::try_extract_frame(buffer).status == ipc::FrameExtract::NeedMoreData);
    // Header complete, payload short by one byte.
    buffer.append(frame, 4, frame.size() - 5);
    MIRAGE_CHECK(ipc::try_extract_frame(buffer).status == ipc::FrameExtract::NeedMoreData);
    buffer.append(frame, frame.size() - 1, 1);

    const ipc::FrameExtraction extraction = ipc::try_extract_frame(buffer);
    MIRAGE_CHECK(extraction.status == ipc::FrameExtract::Message);
    MIRAGE_CHECK(extraction.message == "half-packet-payload");
    MIRAGE_CHECK(buffer.empty());
}

void scenario_framing_two_frames_back_to_back() {
    std::string buffer = ipc::make_frame("first") + ipc::make_frame("second");

    const ipc::FrameExtraction first = ipc::try_extract_frame(buffer);
    MIRAGE_CHECK(first.status == ipc::FrameExtract::Message);
    MIRAGE_CHECK(first.message == "first");

    const ipc::FrameExtraction second = ipc::try_extract_frame(buffer);
    MIRAGE_CHECK(second.status == ipc::FrameExtract::Message);
    MIRAGE_CHECK(second.message == "second");

    MIRAGE_CHECK(buffer.empty());
    MIRAGE_CHECK(ipc::try_extract_frame(buffer).status == ipc::FrameExtract::NeedMoreData);
}

void scenario_framing_oversized_length_rejected() {
    const std::uint32_t oversized = static_cast<std::uint32_t>(ipc::kMaxFrameBytes) + 1;
    std::string buffer;
    buffer.push_back(static_cast<char>(oversized & 0xFFu));
    buffer.push_back(static_cast<char>((oversized >> 8) & 0xFFu));
    buffer.push_back(static_cast<char>((oversized >> 16) & 0xFFu));
    buffer.push_back(static_cast<char>((oversized >> 24) & 0xFFu));

    const ipc::FrameExtraction extraction = ipc::try_extract_frame(buffer);
    MIRAGE_CHECK(extraction.status == ipc::FrameExtract::ProtocolError);
    MIRAGE_CHECK(!extraction.reason.empty());
    MIRAGE_CHECK(extraction.message.empty());
}

void scenario_framing_cap_length_accepted() {
    // Exactly the cap is the largest legitimate payload: NeedMoreData while
    // short, Message once the full million bytes arrived.
    const std::uint32_t declared = static_cast<std::uint32_t>(ipc::kMaxFrameBytes);
    std::string header;
    header.push_back(static_cast<char>(declared & 0xFFu));
    header.push_back(static_cast<char>((declared >> 8) & 0xFFu));
    header.push_back(static_cast<char>((declared >> 16) & 0xFFu));
    header.push_back(static_cast<char>((declared >> 24) & 0xFFu));

    std::string buffer = header;
    MIRAGE_CHECK(ipc::try_extract_frame(buffer).status == ipc::FrameExtract::NeedMoreData);

    const std::string payload(ipc::kMaxFrameBytes, 'p');
    buffer += payload;
    const ipc::FrameExtraction extraction = ipc::try_extract_frame(buffer);
    MIRAGE_CHECK(extraction.status == ipc::FrameExtract::Message);
    MIRAGE_CHECK(extraction.message.size() == ipc::kMaxFrameBytes);
    MIRAGE_CHECK(buffer.empty());
}

// --- protocol: requests -----------------------------------------------------

void scenario_request_round_trips() {
    {
        const std::string payload = ipc::encode_request(7, ipc::HelloRequest{});
        const ipc::RequestDecode decoded = ipc::decode_request(payload);
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.id == 7);
        MIRAGE_CHECK(std::holds_alternative<ipc::HelloRequest>(decoded.body));
    }
    {
        ipc::SubmitTaskRequest request;
        request.goal = "read the file then run a command";
        request.steps.push_back({ipc::StepKind::FilesystemRead, "/tmp/a.txt"});
        request.steps.push_back({ipc::StepKind::ProcessExecute, "printf hi"});
        request.step_timeout = std::chrono::milliseconds{1500};

        const std::string payload = ipc::encode_request(123456789012345ULL, request);
        const ipc::RequestDecode decoded = ipc::decode_request(payload);
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.id == 123456789012345ULL);
        const auto *submit = std::get_if<ipc::SubmitTaskRequest>(&decoded.body);
        MIRAGE_CHECK(submit != nullptr);
        if (submit != nullptr) {
            MIRAGE_CHECK(submit->goal == "read the file then run a command");
            MIRAGE_CHECK(submit->steps.size() == 2);
            if (submit->steps.size() == 2) {
                MIRAGE_CHECK(submit->steps[0].kind == ipc::StepKind::FilesystemRead);
                MIRAGE_CHECK(submit->steps[0].argument == "/tmp/a.txt");
                MIRAGE_CHECK(submit->steps[1].kind == ipc::StepKind::ProcessExecute);
                MIRAGE_CHECK(submit->steps[1].argument == "printf hi");
            }
            MIRAGE_CHECK(submit->step_timeout.has_value());
            if (submit->step_timeout.has_value()) {
                MIRAGE_CHECK(submit->step_timeout->count() == 1500);
            }
        }
    }
    {
        const std::string payload = ipc::encode_request(3, ipc::ListTasksRequest{});
        const ipc::RequestDecode decoded = ipc::decode_request(payload);
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.id == 3);
        MIRAGE_CHECK(std::holds_alternative<ipc::ListTasksRequest>(decoded.body));
    }
    {
        const std::string payload = ipc::encode_request(9, ipc::InspectTaskRequest{"task-42"});
        const ipc::RequestDecode decoded = ipc::decode_request(payload);
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.id == 9);
        const auto *inspect = std::get_if<ipc::InspectTaskRequest>(&decoded.body);
        MIRAGE_CHECK(inspect != nullptr);
        if (inspect != nullptr) {
            MIRAGE_CHECK(inspect->task_id == "task-42");
        }
    }
    {
        const std::string payload = ipc::encode_request(0, ipc::ShutdownRequest{});
        const ipc::RequestDecode decoded = ipc::decode_request(payload);
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.id == 0);
        MIRAGE_CHECK(std::holds_alternative<ipc::ShutdownRequest>(decoded.body));
    }
    // M1-05: task.cancel round trip.
    {
        const std::string payload = ipc::encode_request(14, ipc::CancelTaskRequest{"task-abc"});
        MIRAGE_CHECK(payload.find("task.cancel") != std::string::npos);
        const ipc::RequestDecode decoded = ipc::decode_request(payload);
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.id == 14);
        const auto *cancel = std::get_if<ipc::CancelTaskRequest>(&decoded.body);
        MIRAGE_CHECK(cancel != nullptr);
        if (cancel != nullptr) {
            MIRAGE_CHECK(cancel->task_id == "task-abc");
        }
    }
    // An absent steps array is a legitimate task.submit with no scripted work.
    {
        const ipc::RequestDecode decoded = ipc::decode_request(
            R"({"v":1,"id":11,"op":"task.submit","goal":"control-plane only"})");
        MIRAGE_CHECK(decoded.ok);
        const auto *submit = std::get_if<ipc::SubmitTaskRequest>(&decoded.body);
        MIRAGE_CHECK(submit != nullptr);
        if (submit != nullptr) {
            MIRAGE_CHECK(submit->steps.empty());
            MIRAGE_CHECK(!submit->step_timeout.has_value());
        }
    }
    {
        // An empty goal is a semantic rejection made by the service layer
        // (invalid_argument), not a decode failure: the payload is
        // well-formed as long as 'goal' is a string.
        const ipc::RequestDecode decoded =
            ipc::decode_request(R"({"v":1,"id":12,"op":"task.submit","goal":""})");
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.id == 12);
        const auto *submit = std::get_if<ipc::SubmitTaskRequest>(&decoded.body);
        MIRAGE_CHECK(submit != nullptr);
        if (submit != nullptr) {
            MIRAGE_CHECK(submit->goal.empty());
        }
    }
}

void scenario_request_rejects_malformed_payloads() {
    const char *invalid_payloads[] = {
        "",
        "not json",
        "7",
        "[]",
        R"({"v":2,"id":1,"op":"hello"})",                              // version
        R"({"id":1,"op":"hello"})",                                    // missing v
        R"({"v":1,"op":"hello"})",                                     // missing id
        R"({"v":1,"id":-1,"op":"hello"})",                             // negative id
        R"({"v":1,"id":5})",                                           // missing op
        R"({"v":1,"id":5,"op":"nope"})",                               // unknown op
        R"({"v":1,"id":5,"op":"task.submit"})",                        // missing goal
        R"({"v":1,"id":5,"op":"task.submit","goal":3})",               // goal type
        R"({"v":1,"id":5,"op":"task.submit","goal":"g","steps":{}})",  // not array
        R"({"v":1,"id":5,"op":"task.submit","goal":"g","steps":[3]})", // element
        R"({"v":1,"id":5,"op":"task.submit","goal":"g","steps":[{"arg":"a"}]})",
        R"({"v":1,"id":5,"op":"task.submit","goal":"g","steps":[{"op":"window.click","arg":"a"}]})",
        R"({"v":1,"id":5,"op":"task.submit","goal":"g","steps":[{"op":"filesystem.read"}]})",
        R"({"v":1,"id":5,"op":"task.submit","goal":"g","steps":[{"op":"filesystem.read","arg":""}]})",
        R"({"v":1,"id":5,"op":"task.submit","goal":"g","step_timeout_ms":0})",
        R"({"v":1,"id":5,"op":"task.submit","goal":"g","step_timeout_ms":-100})",
        R"({"v":1,"id":5,"op":"task.inspect"})", // missing id
        R"({"v":1,"id":5,"op":"task.inspect","task_id":""})",
        R"({"v":1,"id":5,"op":"task.cancel"})",              // missing task_id
        R"({"v":1,"id":5,"op":"task.cancel","task_id":""})", // empty task_id
        R"({"v":1,"id":5,"op":"task.cancel","task_id":7})",  // task_id type
    };
    for (const char *payload : invalid_payloads) {
        const ipc::RequestDecode decoded = ipc::decode_request(payload);
        MIRAGE_CHECK(!decoded.ok);
        MIRAGE_CHECK(!decoded.error.empty());
    }
}

// --- protocol: responses ----------------------------------------------------

void scenario_response_round_trips() {
    {
        ipc::Response response;
        response.ok = true;
        response.id = 4;
        response.payload =
            ipc::ServiceIdentity{"mirage-runtime", "0.5.0", "1.2.3", "Running", 1, std::nullopt};
        const ipc::ResponseDecode decoded = ipc::decode_response(ipc::encode_response(response));
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.response.id == 4);
        MIRAGE_CHECK(decoded.response.ok);
        const auto *identity = std::get_if<ipc::ServiceIdentity>(&decoded.response.payload);
        MIRAGE_CHECK(identity != nullptr);
        if (identity != nullptr) {
            MIRAGE_CHECK(identity->name == "mirage-runtime");
            MIRAGE_CHECK(identity->mirage_version == "0.5.0");
            MIRAGE_CHECK(identity->mira_core_version == "1.2.3");
            MIRAGE_CHECK(identity->host_status == "Running");
            MIRAGE_CHECK(identity->protocol == 1);
        }
    }
    {
        ipc::Response response;
        response.ok = true;
        response.id = 5;
        response.payload = ipc::TaskSubmitted{"abc123"};
        const ipc::ResponseDecode decoded = ipc::decode_response(ipc::encode_response(response));
        MIRAGE_CHECK(decoded.ok);
        const auto *submitted = std::get_if<ipc::TaskSubmitted>(&decoded.response.payload);
        MIRAGE_CHECK(submitted != nullptr);
        if (submitted != nullptr) {
            MIRAGE_CHECK(submitted->task_id == "abc123");
        }
    }
    {
        ipc::Response response;
        response.ok = true;
        response.id = 6;
        response.payload = ipc::TaskList{{
            {"id-one", "goal one", "Active"},
            {"id-two", "goal two", "Completed"},
        }};
        const ipc::ResponseDecode decoded = ipc::decode_response(ipc::encode_response(response));
        MIRAGE_CHECK(decoded.ok);
        const auto *list = std::get_if<ipc::TaskList>(&decoded.response.payload);
        MIRAGE_CHECK(list != nullptr);
        if (list != nullptr) {
            MIRAGE_CHECK(list->tasks.size() == 2);
            if (list->tasks.size() == 2) {
                MIRAGE_CHECK(list->tasks[0].id == "id-one");
                MIRAGE_CHECK(list->tasks[0].goal == "goal one");
                MIRAGE_CHECK(list->tasks[0].progress == "Active");
                MIRAGE_CHECK(list->tasks[1].id == "id-two");
                MIRAGE_CHECK(list->tasks[1].progress == "Completed");
            }
        }
    }
    {
        // Terminal inspect: success present, full step views.
        ipc::Response response;
        response.ok = true;
        response.id = 8;
        ipc::InspectTask inspect;
        inspect.id = "task-1";
        inspect.goal = "do the thing";
        inspect.progress = "Completed";
        inspect.has_success = true;
        inspect.success = true;
        inspect.steps.push_back(ipc::StepView{0, "filesystem.read", "ok",
                                              "aa11bb11aa11bb11aa11bb11aa11bb11", "allowed", true,
                                              -1, "file content", false, ""});
        ipc::StepView executed{1,           "process.execute",
                               "ok",        "cc22dd22cc22dd22cc22dd22cc22dd22",
                               "confirmed", true,
                               0,           "shell output",
                               true,        ""};
        inspect.steps.push_back(executed);
        response.payload = std::move(inspect);

        const ipc::ResponseDecode decoded = ipc::decode_response(ipc::encode_response(response));
        MIRAGE_CHECK(decoded.ok);
        const auto *view = std::get_if<ipc::InspectTask>(&decoded.response.payload);
        MIRAGE_CHECK(view != nullptr);
        if (view != nullptr) {
            MIRAGE_CHECK(view->id == "task-1");
            MIRAGE_CHECK(view->goal == "do the thing");
            MIRAGE_CHECK(view->progress == "Completed");
            MIRAGE_CHECK(view->has_success);
            MIRAGE_CHECK(view->success);
            MIRAGE_CHECK(view->steps.size() == 2);
            if (view->steps.size() == 2) {
                MIRAGE_CHECK(view->steps[0].index == 0);
                MIRAGE_CHECK(view->steps[0].kind == "filesystem.read");
                MIRAGE_CHECK(view->steps[0].status == "ok");
                MIRAGE_CHECK(view->steps[0].operation_id == "aa11bb11aa11bb11aa11bb11aa11bb11");
                MIRAGE_CHECK(view->steps[0].permission == "allowed");
                MIRAGE_CHECK(view->steps[0].ok);
                MIRAGE_CHECK(view->steps[0].exit_code == -1);
                MIRAGE_CHECK(view->steps[0].result == "file content");
                MIRAGE_CHECK(!view->steps[0].result_truncated);
                MIRAGE_CHECK(view->steps[1].kind == "process.execute");
                MIRAGE_CHECK(view->steps[1].permission == "confirmed");
                MIRAGE_CHECK(view->steps[1].exit_code == 0);
                MIRAGE_CHECK(view->steps[1].result_truncated);
            }
        }
    }
    {
        // Pending inspect: has_success false must survive the round trip
        // (the encoder omits the member; the decoder reports it absent).
        ipc::Response response;
        response.ok = true;
        response.id = 12;
        ipc::InspectTask inspect;
        inspect.id = "task-2";
        inspect.goal = "still running";
        inspect.progress = "Active";
        inspect.steps.push_back(
            ipc::StepView{0, "filesystem.read", "running", "", "", false, -1, "", false, ""});
        response.payload = std::move(inspect);

        const ipc::ResponseDecode decoded = ipc::decode_response(ipc::encode_response(response));
        MIRAGE_CHECK(decoded.ok);
        const auto *view = std::get_if<ipc::InspectTask>(&decoded.response.payload);
        MIRAGE_CHECK(view != nullptr);
        if (view != nullptr) {
            MIRAGE_CHECK(view->progress == "Active");
            MIRAGE_CHECK(!view->has_success);
            MIRAGE_CHECK(!view->success);
            MIRAGE_CHECK(view->steps.size() == 1);
            if (!view->steps.empty()) {
                MIRAGE_CHECK(view->steps[0].status == "running");
                MIRAGE_CHECK(view->steps[0].operation_id.empty());
                // A step the gate has not judged yet reports no permission
                // outcome, and the member survives the round trip as empty.
                MIRAGE_CHECK(view->steps[0].permission.empty());
            }
        }
    }
    {
        // M1-05 task.cancel acknowledgement: nested task_cancelled object.
        ipc::Response response;
        response.ok = true;
        response.id = 31;
        response.payload = ipc::TaskCancelled{"task-abc", "Cancelling"};
        const std::string wire = ipc::encode_response(response);
        MIRAGE_CHECK(wire.find("\"task_cancelled\"") != std::string::npos);
        MIRAGE_CHECK(wire.find("\"task_id\":\"task-abc\"") != std::string::npos);
        MIRAGE_CHECK(wire.find("\"progress\":\"Cancelling\"") != std::string::npos);

        const ipc::ResponseDecode decoded = ipc::decode_response(wire);
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.response.id == 31);
        MIRAGE_CHECK(decoded.response.ok);
        const auto *cancelled = std::get_if<ipc::TaskCancelled>(&decoded.response.payload);
        MIRAGE_CHECK(cancelled != nullptr);
        if (cancelled != nullptr) {
            MIRAGE_CHECK(cancelled->task_id == "task-abc");
            MIRAGE_CHECK(cancelled->progress == "Cancelling");
        }
    }
    {
        // Shutdown acknowledgement: an ok envelope without payload members.
        ipc::Response response;
        response.ok = true;
        response.id = 21;
        response.payload = ipc::ShutdownAccepted{};
        const ipc::ResponseDecode decoded = ipc::decode_response(ipc::encode_response(response));
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.response.id == 21);
        MIRAGE_CHECK(std::holds_alternative<ipc::ShutdownAccepted>(decoded.response.payload));
    }
    {
        // Wire-level failure responses keep code and message stable.
        ipc::Response response;
        response.ok = false;
        response.id = 77;
        response.error = ipc::IpcError{"not_found", "unknown task id"};
        const ipc::ResponseDecode decoded = ipc::decode_response(ipc::encode_response(response));
        MIRAGE_CHECK(decoded.ok); // the frame itself decodes fine
        MIRAGE_CHECK(!decoded.response.ok);
        MIRAGE_CHECK(decoded.response.id == 77);
        MIRAGE_CHECK(decoded.response.error.code == "not_found");
        MIRAGE_CHECK(decoded.response.error.message == "unknown task id");
    }
}

void scenario_response_rejects_malformed_payloads() {
    const char *invalid_payloads[] = {
        "",
        "garbage",
        R"({"id":1,"ok":true})",                                    // missing v
        R"({"v":2,"id":1,"ok":true})",                              // version
        R"({"v":1,"ok":true})",                                     // missing id
        R"({"v":1,"id":-2,"ok":true})",                             // negative id
        R"({"v":1,"id":1})",                                        // missing ok
        R"({"v":1,"id":1,"ok":1})",                                 // ok type
        R"({"v":1,"id":1,"ok":false})",                             // no error
        R"({"v":1,"id":1,"ok":false,"error":{}})",                  // no code
        R"({"v":1,"id":1,"ok":false,"error":{"code":"x"}})",        // no message
        R"({"v":1,"id":1,"ok":true,"task_id":""})",                 // empty id
        R"({"v":1,"id":1,"ok":true,"service":"s"})",                // partial hello
        R"({"v":1,"id":1,"ok":true,"tasks":{}})",                   // list not array
        R"({"v":1,"id":1,"ok":true,"tasks":[{}]})",                 // entry members
        R"({"v":1,"id":1,"ok":true,"task":{"id":"i","goal":"g"}})", // no progress
        R"({"v":1,"id":1,"ok":true,"task":{"id":"i","goal":"g","progress":"Active","success":"yes"}})",
        R"({"v":1,"id":1,"ok":true,"task_cancelled":"cancelling"})",                 // not object
        R"({"v":1,"id":1,"ok":true,"task_cancelled":{}})",                           // no members
        R"({"v":1,"id":1,"ok":true,"task_cancelled":{"task_id":""}})",               // empty id
        R"({"v":1,"id":1,"ok":true,"task_cancelled":{"task_id":"t"}})",              // no progress
        R"({"v":1,"id":1,"ok":true,"task_cancelled":{"progress":"Cancelling"}})",    // no task_id
        R"({"v":1,"id":1,"ok":true,"task_cancelled":{"task_id":"t","progress":3}})", // type
    };
    for (const char *payload : invalid_payloads) {
        const ipc::ResponseDecode decoded = ipc::decode_response(payload);
        MIRAGE_CHECK(!decoded.ok);
        MIRAGE_CHECK(!decoded.error.empty());
    }
}

void scenario_step_kind_names() {
    MIRAGE_CHECK(std::string(ipc::step_kind_name(ipc::StepKind::FilesystemRead)) ==
                 "filesystem.read");
    MIRAGE_CHECK(std::string(ipc::step_kind_name(ipc::StepKind::ProcessExecute)) ==
                 "process.execute");

    // The name parser (M1-07 recovery hydration) is the exact inverse.
    const auto read_kind = ipc::step_kind_from_name("filesystem.read");
    MIRAGE_CHECK(read_kind.has_value() && *read_kind == ipc::StepKind::FilesystemRead);
    const auto execute_kind = ipc::step_kind_from_name("process.execute");
    MIRAGE_CHECK(execute_kind.has_value() && *execute_kind == ipc::StepKind::ProcessExecute);
    MIRAGE_CHECK(!ipc::step_kind_from_name("").has_value());
    MIRAGE_CHECK(!ipc::step_kind_from_name("filesystem.read ").has_value());
    MIRAGE_CHECK(!ipc::step_kind_from_name("filesystem.write").has_value());
    MIRAGE_CHECK(!ipc::step_kind_from_name("process.exec").has_value());
}

void scenario_step_permission_wire_compatibility() {
    // The encoder always writes the permission member, even for a step the
    // gate has not judged (DEC-010 wire shape).
    {
        ipc::Response response;
        response.ok = true;
        response.id = 91;
        ipc::InspectTask inspect;
        inspect.id = "task-perm";
        inspect.goal = "wire shape probe";
        inspect.progress = "Active";
        ipc::StepView pending;
        pending.index = 0;
        pending.kind = "filesystem.read";
        pending.status = "pending";
        inspect.steps.push_back(std::move(pending));
        response.payload = std::move(inspect);

        const std::string wire = ipc::encode_response(response);
        MIRAGE_CHECK(wire.find("\"permission\"") != std::string::npos);

        const ipc::ResponseDecode decoded = ipc::decode_response(wire);
        MIRAGE_CHECK(decoded.ok);
        const auto *view = std::get_if<ipc::InspectTask>(&decoded.response.payload);
        MIRAGE_CHECK(view != nullptr);
        if (view != nullptr && !view->steps.empty()) {
            MIRAGE_CHECK(view->steps[0].permission.empty());
        }
    }
    // Backward compatibility in the other direction: an inspect produced by
    // a pre-M1-06 peer carries no permission member at all; the decoder
    // accepts it and reports the outcome as empty.
    {
        const ipc::ResponseDecode decoded = ipc::decode_response(
            R"({"v":1,"id":92,"ok":true,"task":{"id":"task-old","goal":"legacy",)"
            R"("progress":"Completed","success":true,"steps":[{)"
            R"("index":0,"kind":"process.execute","status":"ok",)"
            R"("operation_id":"aa11bb11aa11bb11aa11bb11aa11bb11","ok":true,)"
            R"("exit_code":0,"result":"out","result_truncated":false,"error":""}]}})");
        MIRAGE_CHECK(decoded.ok);
        const auto *view = std::get_if<ipc::InspectTask>(&decoded.response.payload);
        MIRAGE_CHECK(view != nullptr);
        if (view != nullptr) {
            MIRAGE_CHECK(view->progress == "Completed");
            MIRAGE_CHECK(view->steps.size() == 1);
            if (view->steps.size() == 1) {
                MIRAGE_CHECK(view->steps[0].status == "ok");
                MIRAGE_CHECK(view->steps[0].permission.empty());
                MIRAGE_CHECK(view->steps[0].result == "out");
            }
        }
    }
}

// --- endpoint helpers -------------------------------------------------------

void scenario_socket_directory_components() {
    MIRAGE_CHECK(ipc::socket_directory("/run/mirage/mirage-service.sock") == "/run/mirage");
    MIRAGE_CHECK(ipc::socket_directory("/mirage-service.sock") == "/");
    MIRAGE_CHECK(ipc::socket_directory("plain-name.sock") == ".");
}

void scenario_default_socket_path_follows_xdg() {
    const char *previous = std::getenv("XDG_RUNTIME_DIR");
    const std::string saved = previous != nullptr ? previous : "";

    ::setenv("XDG_RUNTIME_DIR", "/tmp/xdg-probe-mirage", 1);
    MIRAGE_CHECK(ipc::default_socket_path() == "/tmp/xdg-probe-mirage/mirage/mirage-service.sock");

    // An empty value must fall back like an unset one.
    ::setenv("XDG_RUNTIME_DIR", "", 1);
    MIRAGE_CHECK(ipc::default_socket_path() ==
                 "/tmp/mirage-" + std::to_string(::getuid()) + "/mirage-service.sock");

    ::unsetenv("XDG_RUNTIME_DIR");
    MIRAGE_CHECK(ipc::default_socket_path() ==
                 "/tmp/mirage-" + std::to_string(::getuid()) + "/mirage-service.sock");

    if (previous != nullptr) {
        ::setenv("XDG_RUNTIME_DIR", saved.c_str(), 1);
    } else {
        ::unsetenv("XDG_RUNTIME_DIR");
    }
}

// --- transport --------------------------------------------------------------

ipc::IpcStream accept_pending(ipc::IpcListener &listener) {
    const auto deadline = std::chrono::steady_clock::now() + kBudget;
    for (;;) {
        std::string diagnostic;
        ipc::IpcStream peer = listener.accept(diagnostic);
        if (peer.valid()) {
            return peer;
        }
        if (std::chrono::steady_clock::now() >= deadline ||
            !mirage::testing::poll_fd(listener.handle(), POLLIN, std::chrono::milliseconds{50})) {
            return peer;
        }
    }
}

void scenario_transport_echo_round_trip() {
    TempDir dir;
    const std::string path = (dir.root() / "echo.sock").string();
    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(path, diagnostic);
    MIRAGE_CHECK(listener.valid());
    MIRAGE_CHECK(std::filesystem::exists(path));

    std::string connect_diagnostic;
    ipc::IpcStream client_side = ipc::connect_stream(path, kBudget, connect_diagnostic);
    MIRAGE_CHECK(client_side.valid());
    ipc::IpcStream server_side = accept_pending(listener);
    MIRAGE_CHECK(server_side.valid());

    std::string client_buffer;
    std::string server_buffer;
    MIRAGE_CHECK(mirage::testing::write_all(client_side, ipc::make_frame("ping-payload"), kBudget));
    const FrameRead request = mirage::testing::read_frame(server_side, server_buffer, kBudget);
    MIRAGE_CHECK(request.status == FrameRead::Status::Message);
    MIRAGE_CHECK(request.message == "ping-payload");

    MIRAGE_CHECK(mirage::testing::write_all(server_side, ipc::make_frame("pong-payload"), kBudget));
    const FrameRead reply = mirage::testing::read_frame(client_side, client_buffer, kBudget);
    MIRAGE_CHECK(reply.status == FrameRead::Status::Message);
    MIRAGE_CHECK(reply.message == "pong-payload");
}

/// Drives one oversized frame across the socket by pumping both endpoints
/// from the calling thread. A frame larger than the kernel socket buffer
/// cannot cross in one sequential push (the writer would stall in poll while
/// nobody drains the peer), so writer and reader alternate small steps — the
/// same interleaving the real service loop performs from its side.
bool pump_frame(ipc::IpcStream &writer, ipc::IpcStream &reader, const std::string &frame,
                std::string &reader_buffer, std::string &received) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    std::size_t sent = 0;
    received.clear();
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        if (sent < frame.size() &&
            mirage::testing::poll_fd(writer.handle(), POLLOUT, std::chrono::milliseconds{20})) {
            const ipc::IoResult result =
                writer.write_some(frame.data() + sent, frame.size() - sent);
            if (result.status == ipc::IoStatus::Ok) {
                sent += result.bytes;
            } else if (result.status == ipc::IoStatus::Closed ||
                       result.status == ipc::IoStatus::Error) {
                return false;
            }
        }
        if (mirage::testing::poll_fd(reader.handle(), POLLIN, std::chrono::milliseconds{20})) {
            char chunk[8192];
            const ipc::IoResult result = reader.read_some(chunk, sizeof(chunk));
            if (result.status == ipc::IoStatus::Ok) {
                reader_buffer.append(chunk, result.bytes);
            } else if (result.status != ipc::IoStatus::WouldBlock) {
                return false;
            }
        }
        const ipc::FrameExtraction extraction = ipc::try_extract_frame(reader_buffer);
        if (extraction.status == ipc::FrameExtract::Message) {
            received = extraction.message;
            return true;
        }
        if (extraction.status == ipc::FrameExtract::ProtocolError) {
            return false;
        }
    }
}

void scenario_transport_large_message_fragmentation() {
    TempDir dir;
    const std::string path = (dir.root() / "bulk.sock").string();
    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(path, diagnostic);
    MIRAGE_CHECK(listener.valid());

    std::string connect_diagnostic;
    ipc::IpcStream client_side = ipc::connect_stream(path, kBudget, connect_diagnostic);
    MIRAGE_CHECK(client_side.valid());
    ipc::IpcStream server_side = accept_pending(listener);
    MIRAGE_CHECK(server_side.valid());

    // 300 KiB: far beyond one socket buffer, so both directions must chunk
    // through WouldBlock/retry instead of a single syscall.
    std::string payload;
    for (int block = 0; block < 30000; ++block) {
        payload += "0123456789";
    }
    MIRAGE_CHECK(payload.size() == 300000);
    MIRAGE_CHECK(payload.size() < ipc::kMaxFrameBytes);

    std::string client_buffer;
    std::string server_buffer;
    std::string received;
    MIRAGE_CHECK(
        pump_frame(client_side, server_side, ipc::make_frame(payload), server_buffer, received));
    MIRAGE_CHECK(received == payload);

    MIRAGE_CHECK(
        pump_frame(server_side, client_side, ipc::make_frame(payload), client_buffer, received));
    MIRAGE_CHECK(received == payload);
}

void scenario_transport_accept_without_pending_connection() {
    TempDir dir;
    const std::string path = (dir.root() / "quiet.sock").string();
    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(path, diagnostic);
    MIRAGE_CHECK(listener.valid());

    std::string accept_diagnostic;
    const ipc::IpcStream peer = listener.accept(accept_diagnostic);
    MIRAGE_CHECK(!peer.valid()); // WouldBlock semantics: nothing pending
}

void scenario_transport_double_bind_rejected() {
    TempDir dir;
    const std::string path = (dir.root() / "busy.sock").string();
    std::string diagnostic;
    ipc::IpcListener first = ipc::IpcListener::bind(path, diagnostic);
    MIRAGE_CHECK(first.valid());
    MIRAGE_CHECK(ipc::endpoint_has_listener(path, std::chrono::milliseconds{500}));

    std::string second_diagnostic;
    ipc::IpcListener second = ipc::IpcListener::bind(path, second_diagnostic);
    MIRAGE_CHECK(!second.valid());
    MIRAGE_CHECK(second_diagnostic.find("already listening") != std::string::npos);

    // The live listener is untouched by the failed takeover attempt.
    MIRAGE_CHECK(first.valid());
    MIRAGE_CHECK(std::filesystem::exists(path));
}

void scenario_transport_listener_close_removes_socket() {
    TempDir dir;
    const std::string path = (dir.root() / "gone.sock").string();
    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(path, diagnostic);
    MIRAGE_CHECK(listener.valid());
    MIRAGE_CHECK(std::filesystem::exists(path));

    listener.close();
    MIRAGE_CHECK(!std::filesystem::exists(path));
    MIRAGE_CHECK(!ipc::endpoint_has_listener(path, std::chrono::milliseconds{500}));
}

void scenario_transport_stale_regular_file_takeover() {
    TempDir dir;
    const std::string path = (dir.root() / "leftover.sock").string();
    {
        std::FILE *file = std::fopen(path.c_str(), "w");
        MIRAGE_CHECK(file != nullptr);
        if (file != nullptr) {
            std::fputs("not a socket", file);
            std::fclose(file);
        }
    }
    MIRAGE_CHECK(!ipc::endpoint_has_listener(path, std::chrono::milliseconds{500}));

    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(path, diagnostic);
    MIRAGE_CHECK(listener.valid());
    MIRAGE_CHECK(std::filesystem::exists(path));
}

void scenario_transport_stale_orphan_socket_takeover() {
    TempDir dir;
    const std::string path = (dir.root() / "orphan.sock").string();

    // A socket file left behind by a crashed process: bound once, then the
    // fd was closed without unlinking. Nothing listens behind it.
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    MIRAGE_CHECK(fd >= 0);
    if (fd >= 0) {
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        ::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
        MIRAGE_CHECK(::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) ==
                     0);
        ::close(fd);
    }
    MIRAGE_CHECK(std::filesystem::exists(path));
    MIRAGE_CHECK(!ipc::endpoint_has_listener(path, std::chrono::milliseconds{500}));

    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(path, diagnostic);
    MIRAGE_CHECK(listener.valid());

    // A client can reach the new listener through the recreated file.
    std::string connect_diagnostic;
    ipc::IpcStream stream = ipc::connect_stream(path, kBudget, connect_diagnostic);
    MIRAGE_CHECK(stream.valid());
}

void scenario_transport_connect_missing_path_fails() {
    TempDir dir;
    const std::string path = (dir.root() / "never.sock").string();

    MIRAGE_CHECK(!ipc::endpoint_has_listener(path, std::chrono::milliseconds{500}));
    std::string diagnostic;
    const ipc::IpcStream stream = ipc::connect_stream(path, kBudget, diagnostic);
    MIRAGE_CHECK(!stream.valid());
    MIRAGE_CHECK(!diagnostic.empty());
}

void scenario_transport_creates_missing_socket_directory() {
    TempDir dir;
    const std::string path = (dir.root() / "created" / "late.sock").string();
    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(path, diagnostic);
    MIRAGE_CHECK(listener.valid());
    MIRAGE_CHECK(std::filesystem::exists(path));

    std::error_code error;
    const auto permissions = std::filesystem::status(dir.root() / "created", error);
    MIRAGE_CHECK(!error);
    MIRAGE_CHECK((permissions.permissions() & std::filesystem::perms::group_all) ==
                 std::filesystem::perms::none);
    MIRAGE_CHECK((permissions.permissions() & std::filesystem::perms::others_all) ==
                 std::filesystem::perms::none);
}

// --- IpcClient --------------------------------------------------------------

void scenario_client_unavailable_on_missing_endpoint() {
    TempDir dir;
    const std::string path = (dir.root() / "absent.sock").string();
    ipc::IpcClient client(path);

    const ipc::Response response = client.call(ipc::HelloRequest{}, std::chrono::seconds{2});
    MIRAGE_CHECK(!response.ok);
    MIRAGE_CHECK(response.error.code == "unavailable");
    MIRAGE_CHECK(!response.error.message.empty());
}

void scenario_client_call_against_live_listener() {
    TempDir dir;
    const std::string path = (dir.root() / "echo-service.sock").string();
    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(path, diagnostic);
    MIRAGE_CHECK(listener.valid());

    // Minimal echo responder as a forked process: the only concurrency the
    // harness needs, and the same fork shape the CLI itself uses to launch
    // mirage-service (DEC-007 item 6). The child shares the listening fd,
    // answers one hello frame and exits immediately.
    const pid_t child = ::fork();
    MIRAGE_CHECK(child >= 0);
    if (child == 0) {
        ::pollfd listen_descriptor{};
        listen_descriptor.fd = listener.handle();
        listen_descriptor.events = POLLIN;
        if (::poll(&listen_descriptor, 1, 3000) == 1) {
            std::string accept_diagnostic;
            ipc::IpcStream peer = listener.accept(accept_diagnostic);
            if (peer.valid()) {
                ::pollfd peer_descriptor{};
                peer_descriptor.fd = peer.handle();
                peer_descriptor.events = POLLIN;
                if (::poll(&peer_descriptor, 1, 3000) == 1) {
                    char scratch[512];
                    (void)peer.read_some(scratch, sizeof(scratch));
                    // IpcClient always correlates with id 1.
                    const std::string response =
                        ipc::make_frame(R"({"v":1,"id":1,"ok":true,"service":"ipc-echo",)"
                                        R"("mirage_version":"test","mira_core_version":"0.0.0",)"
                                        R"("host_status":"Running","protocol":1})");
                    std::size_t sent = 0;
                    while (sent < response.size()) {
                        const ipc::IoResult result =
                            peer.write_some(response.data() + sent, response.size() - sent);
                        if (result.status == ipc::IoStatus::Ok) {
                            sent += result.bytes;
                            continue;
                        }
                        if (result.status == ipc::IoStatus::WouldBlock) {
                            continue;
                        }
                        break;
                    }
                }
            }
        }
        ::_exit(0);
    }
    if (child < 0) {
        return;
    }

    ipc::IpcClient client(path);
    const ipc::Response response = client.call(ipc::HelloRequest{}, std::chrono::seconds{5});
    MIRAGE_CHECK(response.ok);
    MIRAGE_CHECK(response.id == 1);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&response.payload);
    MIRAGE_CHECK(identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->name == "ipc-echo");
        MIRAGE_CHECK(identity->protocol == 1);
    }

    int child_status = 0;
    MIRAGE_CHECK(::waitpid(child, &child_status, 0) == child);
    MIRAGE_CHECK(WIFEXITED(child_status));
    MIRAGE_CHECK(WEXITSTATUS(child_status) == 0);
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[ipc_protocol_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("framing_round_trip", scenario_framing_round_trip);
    run_scenario("framing_little_endian_header", scenario_framing_little_endian_header);
    run_scenario("framing_empty_payload", scenario_framing_empty_payload);
    run_scenario("framing_partial_feeds", scenario_framing_partial_feeds);
    run_scenario("framing_two_frames_back_to_back", scenario_framing_two_frames_back_to_back);
    run_scenario("framing_oversized_length_rejected", scenario_framing_oversized_length_rejected);
    run_scenario("framing_cap_length_accepted", scenario_framing_cap_length_accepted);
    run_scenario("request_round_trips", scenario_request_round_trips);
    run_scenario("request_rejects_malformed_payloads", scenario_request_rejects_malformed_payloads);
    run_scenario("response_round_trips", scenario_response_round_trips);
    run_scenario("response_rejects_malformed_payloads",
                 scenario_response_rejects_malformed_payloads);
    run_scenario("step_kind_names", scenario_step_kind_names);
    run_scenario("step_permission_wire_compatibility", scenario_step_permission_wire_compatibility);
    run_scenario("socket_directory_components", scenario_socket_directory_components);
    run_scenario("default_socket_path_follows_xdg", scenario_default_socket_path_follows_xdg);
    run_scenario("transport_echo_round_trip", scenario_transport_echo_round_trip);
    run_scenario("transport_large_message_fragmentation",
                 scenario_transport_large_message_fragmentation);
    run_scenario("transport_accept_without_pending_connection",
                 scenario_transport_accept_without_pending_connection);
    run_scenario("transport_double_bind_rejected", scenario_transport_double_bind_rejected);
    run_scenario("transport_listener_close_removes_socket",
                 scenario_transport_listener_close_removes_socket);
    run_scenario("transport_stale_regular_file_takeover",
                 scenario_transport_stale_regular_file_takeover);
    run_scenario("transport_stale_orphan_socket_takeover",
                 scenario_transport_stale_orphan_socket_takeover);
    run_scenario("transport_connect_missing_path_fails",
                 scenario_transport_connect_missing_path_fails);
    run_scenario("transport_creates_missing_socket_directory",
                 scenario_transport_creates_missing_socket_directory);
    run_scenario("client_unavailable_on_missing_endpoint",
                 scenario_client_unavailable_on_missing_endpoint);
    run_scenario("client_call_against_live_listener", scenario_client_call_against_live_listener);
    return mirage::testing::finish("ipc_protocol_test");
}
