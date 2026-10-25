#include "../../integration/mira/src/conversation_capture.hpp"
#include "../../integration/mira/src/desktop_ocr_tool.hpp"
#include "../../integration/mirador/src/ppocr_pipeline.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <kairo/executor.hpp>
#include <mira/event_store.hpp>
#include <mira/model_provider.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/runtime_service.hpp>
#include <poll.h>

namespace {
using Json = mira::JsonValue;
void put(const std::filesystem::path &p, const std::string &s) {
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}
void add_float(std::string &s, float f) {
    char b[sizeof(f)];
    std::memcpy(b, &f, sizeof(f));
    s.append(b, sizeof(f));
}
std::filesystem::path fixture(const std::filesystem::path &dir, bool detect_text = true) {
    std::filesystem::create_directories(dir);
    std::map<std::string, std::string> data;
    data["det_param"] = "7767517\n2 2\nInput data 0 1 x 0=32 1=32 2=3\nConvolution conv 1 1 x out "
                        "0=1 1=1 5=1 6=3\n";
    data["det_bin"] = std::string(4, '\0');
    for (int n = 0; n < 3; ++n)
        add_float(data["det_bin"], 0);
    add_float(data["det_bin"], detect_text ? .9F : 0.F);
    data["rec_param"] = "7767517\n3 3\nInput data 0 1 x 0=16 1=16 2=3\nConvolution conv 1 1 x tmp "
                        "0=9 1=16 11=16 5=1 6=6912\nReshape reshape 1 1 tmp out 0=3 1=1 2=3\n";
    data["rec_bin"] = std::string(4, '\0');
    for (int n = 0; n < 6912; ++n)
        add_float(data["rec_bin"], 0);
    for (int n = 0; n < 9; ++n)
        add_float(data["rec_bin"], n == 1 || n == 3 || n == 8 ? 9.0F : 0.0F);
    data["charset"] = "中\nA\n";
    Json::Object artifacts;
    for (const auto &[name, bytes] : data) {
        put(dir / name, bytes);
        artifacts.emplace_back(
            name, Json{Json::Object{{"path", name},
                                    {"sha256", mira::digest_string(bytes).to_string()},
                                    {"bytes", static_cast<std::int64_t>(bytes.size())}}});
    }
    put(dir / "manifest.json",
        mira::to_json_string(Json{Json::Object{{"schema", std::int64_t{1}},
                                               {"model_id", "synthetic-contract-only"},
                                               {"artifacts", Json{std::move(artifacts)}},
                                               {"det_side", std::int64_t{32}},
                                               {"rec_width", std::int64_t{32}},
                                               {"rec_height", std::int64_t{16}}}}));
    return dir / "manifest.json";
}

void yolo_contract(kairo::Executor &executor, const std::filesystem::path &dir) {
    using Pipeline = mirage::integration::PpOcrPipeline;
    mirage::testing::FakeDesktopEnvironment env;
    env.displays = {{"d1", {-25, 53, 64, 64}, true}};
    auto manifest = fixture(dir, false);
    // One image-independent known box: this tests actual runtime/decode/mapping,
    // while real-model quality is a separate manual acceptance.
    // A negative OCR map isolates detector coordinate recovery; real mixed
    // OCR/detector containment fusion is checked with the reviewed UI corpus.
    const std::string param = "7767517\n4 4\nInput data 0 1 x 0=64 1=64 2=3\n"
                              "Pooling pool 1 1 x pooled 0=1 4=1\n"
                              "Convolution conv 1 1 pooled tmp 0=6 1=1 5=1 6=18\n"
                              "Reshape reshape 1 1 tmp out 0=6 1=1 2=1\n";
    std::string bin(4 + 18 * 4, '\0');
    for (float v : {32.F, 32.F, 16.F, 16.F, .9F, .8F})
        add_float(bin, v);
    put(dir / "yolo.param", param);
    put(dir / "yolo.bin", bin);
    Json::Object artifacts;
    for (const auto &[name, bytes] :
         std::map<std::string, std::string>{{"param", param}, {"bin", bin}})
        artifacts.emplace_back(
            name, Json{Json::Object{{"path", std::string("yolo.") + name},
                                    {"sha256", mira::digest_string(bytes).to_string()},
                                    {"bytes", static_cast<std::int64_t>(bytes.size())}}});
    const Json detector{Json::Object{{"model_id", "synthetic-yolo-contract"},
                                     {"artifacts", Json{artifacts}},
                                     {"class_names", Json{Json::Array{Json{"icon"}}}},
                                     {"input_side", std::int64_t{64}},
                                     {"preprocessing", "bilinear114"},
                                     {"confidence_milli", std::int64_t{250}}}};
    auto write_manifest = [&](Json block, bool roi = false) {
        fixture(dir, false);
        std::ifstream file(manifest);
        auto root = *mira::parse_json(std::string(std::istreambuf_iterator<char>(file), {}))
                         .value()
                         .as_object();
        root.emplace_back("detector", std::move(block));
        root.emplace_back("roi_capture", roi);
        put(manifest, mira::to_json_string(Json{std::move(root)}));
    };
    write_manifest(detector);
    std::string error;
    auto pipeline = Pipeline::create(executor, *env.screen(), manifest, error);
#ifndef MIRAGE_TEST_WITH_YOLO
    MIRAGE_CHECK(!pipeline && error.find("YOLO unavailable") != std::string::npos);
#else
    if (!pipeline)
        std::cerr << "YOLO pipeline setup failed: " << error << '\n';
    MIRAGE_CHECK(pipeline);
    if (!pipeline)
        return;
    const auto deadline = [] {
        return std::chrono::steady_clock::now() + std::chrono::seconds(10);
    };
    auto plain = pipeline->refresh(true, {}, deadline());
    MIRAGE_CHECK(plain.analyzed && pipeline->detector_calls() == 0);
    for (const auto &r : plain.publish.snapshot.regions)
        MIRAGE_CHECK(r.source != mirage::desktop::VisualRegionSource::kDetector);
    auto marked = pipeline->mark({}, {}, deadline());
    MIRAGE_CHECK(marked.analyzed && pipeline->detector_calls() == 1);
    int detectors = 0;
    std::string detector_ref;
    for (const auto &r : marked.publish.snapshot.regions) {
        if (r.source != mirage::desktop::VisualRegionSource::kDetector)
            continue;
        ++detectors;
        detector_ref = r.ref;
        MIRAGE_CHECK(r.bounds.x == -1 && r.bounds.y == 77 && r.bounds.width == 16 &&
                     r.bounds.height == 16);
        const auto mark = std::find_if(marked.marks.begin(), marked.marks.end(),
                                       [&](const auto &m) { return m.ref == r.ref; });
        MIRAGE_CHECK(mark != marked.marks.end());
        if (mark != marked.marks.end())
            MIRAGE_CHECK(mark->anchor.x == 7 && mark->anchor.y == 85 && mark->selectable);
        MIRAGE_CHECK(pipeline->registry().resolve(r.ref).has_value());
    }
    MIRAGE_CHECK(detectors == 1);
    // Same size pixels, different desktop origin: no old global coordinate reuse.
    env.displays.front().geometry.x = 101;
    auto moved = pipeline->mark({}, {}, deadline());
    MIRAGE_CHECK(moved.analyzed && pipeline->detector_calls() == 2);
    for (const auto &r : moved.publish.snapshot.regions)
        if (r.source == mirage::desktop::VisualRegionSource::kDetector)
            MIRAGE_CHECK(r.bounds.x == 125);
    mirage::desktop::CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(!pipeline->mark({}, cancel, deadline()).analyzed &&
                 pipeline->registry().size() == 0);
    MIRAGE_CHECK(!pipeline->registry().resolve(detector_ref));
    MIRAGE_CHECK(!pipeline->mark({}, {}, std::chrono::steady_clock::now() - std::chrono::seconds(1))
                      .analyzed);
    pipeline->stop();
    MIRAGE_CHECK(!pipeline->running() && pipeline->registry().size() == 0);
    pipeline.reset();
    // Explicit ROI capture maps local results to ROI origin, including negative x.
    env.displays = {{"d1", {-100, 53, 128, 96}, true}};
    write_manifest(detector, true);
    pipeline = Pipeline::create(executor, *env.screen(), manifest, error);
    MIRAGE_CHECK(pipeline);
    if (pipeline) {
        auto crop =
            pipeline->mark(mirage::desktop::WindowGeometry{-80, 61, 32, 32}, {}, deadline());
        MIRAGE_CHECK(crop.analyzed);
        int found = 0;
        for (const auto &r : crop.publish.snapshot.regions)
            if (r.source == mirage::desktop::VisualRegionSource::kDetector) {
                ++found;
                MIRAGE_CHECK(r.bounds.x == -68 && r.bounds.y == 73 && r.bounds.width == 8 &&
                             r.bounds.height == 8);
            }
        MIRAGE_CHECK(found == 1);
        pipeline->stop();
        pipeline.reset();
    }
    // A backend output mismatch must fail the mark, never publish OCR-only success.
    auto wrong = *detector.as_object();
    for (auto &[name, value] : wrong)
        if (name == "class_names")
            value = Json{Json::Array{Json{"icon"}, Json{"other"}}};
    write_manifest(Json{std::move(wrong)});
    pipeline = Pipeline::create(executor, *env.screen(), manifest, error);
    MIRAGE_CHECK(pipeline);
    if (pipeline) {
        MIRAGE_CHECK(pipeline->refresh(true, {}, deadline()).analyzed);
        auto failed = pipeline->mark({}, {}, deadline());
        MIRAGE_CHECK(!failed.analyzed && !failed.analysis_error.code.empty() &&
                     pipeline->registry().size() == 0);
        pipeline->stop();
        pipeline.reset();
    }
    for (const char *key :
         {"preprocessing", "input_side", "class_names", "artifacts", "confidence_milli"}) {
        auto block = *detector.as_object();
        for (auto &[name, value] : block)
            if (name == key)
                value = Json{nullptr};
        write_manifest(Json{std::move(block)});
        MIRAGE_CHECK(!Pipeline::create(executor, *env.screen(), manifest, error));
    }
    write_manifest(detector);
    put(dir / "yolo.bin", "corrupt");
    MIRAGE_CHECK(!Pipeline::create(executor, *env.screen(), manifest, error) &&
                 error.find("mismatch") != std::string::npos);
#endif
}

void tool_contract(kairo::Executor &executor) {
    mirage::integration::DesktopToolContext context;
    context.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    int calls = 0;
    bool allow = false;
    int mode = 0;
    context.gate = [&](const auto &cap, const auto &, const auto &) {
        return cap == "screen.capture" && allow;
    };
    context.ocr = [&](const auto &, const auto &) {
        ++calls;
        mirage::integration::DesktopOcrResult r;
        r.ok = mode != 1;
        if (mode == 2)
            throw std::runtime_error("injected OCR failure");
        if (mode == 3)
            context.actions->takeover();
        r.display_id = "d1";
        r.snapshot.scope_ref = "@vs1";
        r.snapshot.regions = {{"@v1",
                               mirage::desktop::VisualRegionSource::kOcr,
                               {0, 0, 10, 10},
                               "private-ocr",
                               {},
                               1.0}};
        if (mode == 4)
            r.snapshot.regions.resize(129);
        return r;
    };
    mira::BuiltinToolRegistry registry;
    mirage::integration::detail::register_ocr_tool(registry, context, {}, {});
    const auto spec = registry.exposed_tools().front();
    const auto invoke = [&] {
        mira::ToolProposal call;
        call.tool_id = spec.tool_id;
        call.tool_version = spec.version;
        call.wire_name = spec.wire_name;
        call.arguments = Json{Json::Object{}};
        call.arguments_digest = mira::canonical_json_digest(call.arguments);
        call.operation_id = mira::OperationId::generate();
        call.provider_call_id.value = "ocr";
        return registry.execute(call, mira::make_control_context());
    };
    auto denied = invoke();
    MIRAGE_CHECK(denied && denied.value().failed && calls == 0);
    allow = true;
    auto success = executor.submit_auto(invoke).get();
    MIRAGE_CHECK(success && !success.value().failed && calls == 1);
    mirage::integration::detail::ConversationCapture capture({});
    mirage::conversation::Part part;
    part.kind = "tool";
    part.name = "desktop.vision.ocr";
    capture.parts.push_back(part);
    capture.finish_tool(&capture.parts.front(), success.value().result);
    MIRAGE_CHECK(capture.parts.front().output.find("private-ocr") == std::string::npos);
    MIRAGE_CHECK(capture.parts.front().output.find("region_count") != std::string::npos);
    for (const auto failure_mode : {1, 2, 3, 4}) {
        mode = failure_mode;
        const auto failed = executor.submit_auto(invoke).get();
        MIRAGE_CHECK(failed && failed.value().failed);
        if (failure_mode == 3)
            MIRAGE_CHECK(context.actions->resume());
    }
}

// DEC-062: a configured capture scope limits OCR text to the focused scope
// window and labels the output with its window id.
void ocr_scope_contract(kairo::Executor &executor) {
    mirage::integration::DesktopToolContext context;
    context.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->displays = {{"d1", {0, 0, 200, 100}, true}};
    env->windows = {{"w1", "Scoped", {10, 20, 80, 60}, true},
                    {"w2", "Foreign", {110, 0, 40, 30}, false}};
    context.environment = env;
    context.gate = [](const auto &cap, const auto &, const auto &) {
        return cap == "screen.capture";
    };
    context.ocr = [](const auto &, const auto &) {
        mirage::integration::DesktopOcrResult read;
        read.ok = true;
        read.display_id = "d1";
        read.snapshot.scope_ref = "@vs1";
        read.snapshot.regions = {{"@vin",
                                  mirage::desktop::VisualRegionSource::kOcr,
                                  {20, 30, 40, 10},
                                  "inside-text",
                                  {},
                                  1.0},
                                 {"@voutside",
                                  mirage::desktop::VisualRegionSource::kOcr,
                                  {0, 0, 50, 10},
                                  "outside-text",
                                  {},
                                  1.0},
                                 {"@voverlap",
                                  mirage::desktop::VisualRegionSource::kOcr,
                                  {50, 55, 50, 10},
                                  "overlap-text",
                                  {},
                                  1.0}};
        return read;
    };
    context.capture_scope = "w1";
    const auto invoke_on = [](mira::BuiltinToolRegistry &target) {
        const auto spec = target.exposed_tools().front();
        mira::ToolProposal call;
        call.tool_id = spec.tool_id;
        call.tool_version = spec.version;
        call.wire_name = spec.wire_name;
        call.arguments = Json{Json::Object{}};
        call.arguments_digest = mira::canonical_json_digest(call.arguments);
        call.operation_id = mira::OperationId::generate();
        call.provider_call_id.value = "ocr-scope";
        return target.execute(call, mira::make_control_context());
    };
    mira::BuiltinToolRegistry registry;
    mirage::integration::detail::register_ocr_tool(registry, context, {}, {});
    const auto scoped = executor.submit_auto([&] { return invoke_on(registry); }).get();
    MIRAGE_CHECK(scoped && !scoped.value().failed);
    const auto *window_id = scoped.value().result.find("window_id");
    MIRAGE_CHECK(window_id && window_id->as_string() && *window_id->as_string() == "w1");
    const auto *regions = scoped.value().result.find("regions");
    MIRAGE_CHECK(regions && regions->as_array() && regions->as_array()->size() == 1);
    const auto &only = regions->as_array()->front();
    MIRAGE_CHECK(only.find("text") && only.find("text")->as_string() &&
                 *only.find("text")->as_string() == "inside-text");
    MIRAGE_CHECK(*only.find("x")->as_integer() == 20 && *only.find("y")->as_integer() == 30);
    MIRAGE_CHECK(*only.find("width")->as_integer() == 40 &&
                 *only.find("height")->as_integer() == 10);
    // A different focused window refuses the whole read.
    env->windows.front().focused = false;
    env->windows.back().focused = true;
    const auto unfocused = executor.submit_auto([&] { return invoke_on(registry); }).get();
    MIRAGE_CHECK(unfocused && unfocused.value().failed &&
                 unfocused.value().safe_error_summary.find("not focused") != std::string::npos);
    env->windows.front().focused = true;
    env->windows.back().focused = false;
    // Without a window provider the scope cannot be confirmed: fail closed.
    mira::BuiltinToolRegistry headless;
    mirage::integration::DesktopToolContext missing = context;
    missing.environment.reset();
    mirage::integration::detail::register_ocr_tool(headless, missing, {}, {});
    const auto unconfirmed = executor.submit_auto([&] { return invoke_on(headless); }).get();
    MIRAGE_CHECK(unconfirmed && unconfirmed.value().failed &&
                 unconfirmed.value().safe_error_summary.find("not focused") != std::string::npos);
}
// Native capture has no display/window enumeration, while the real synthetic
// PP-OCR/ncnn resources and product Runtime remain unchanged.
class NativeOcrEnvironment final : public mirage::desktop::DesktopEnvironment,
                                   public mirage::desktop::ScreenProvider {
  public:
    int opened = 0, captured = 0, closed = 0;
    mirage::desktop::EnvironmentInfo info() const override {
        return {"native OCR fixture", "test"};
    }
    mirage::desktop::ScreenProvider *screen() override { return this; }
    bool desktop_observation_only() const override { return true; }
    struct Session : mirage::desktop::DesktopCaptureSession {
        NativeOcrEnvironment &env;
        explicit Session(NativeOcrEnvironment &v) : env(v) {}
        ~Session() override { ++env.closed; }
        bool reusable() const override { return true; }
        mirage::desktop::CaptureOutcome capture(const mirage::desktop::CaptureLimits &,
                                                const mirage::desktop::CancelToken &) override {
            ++env.captured;
            return {true,
                    false,
                    {mirage::desktop::ImageFormat::Bgra8, 32, 32, 128,
                     std::vector<std::uint8_t>(4096, 255)},
                    {}};
        }
    };
    std::unique_ptr<mirage::desktop::DesktopCaptureSession>
    create_desktop_capture_session() override {
        ++opened;
        return std::make_unique<Session>(*this);
    }
    mirage::desktop::DisplayListOutcome
    list_displays(const mirage::desktop::DisplayListLimits &,
                  const mirage::desktop::CancelToken &) override {
        return {false, {}, {"unsupported_capability", "no global display mapping"}};
    }
    mirage::desktop::CaptureOutcome capture_display(const std::string &,
                                                    const mirage::desktop::CaptureLimits &,
                                                    const mirage::desktop::CancelToken &) override {
        return {};
    }
    mirage::desktop::CaptureOutcome capture_window(const std::string &,
                                                   const mirage::desktop::CaptureLimits &,
                                                   const mirage::desktop::CancelToken &) override {
        return {};
    }
    mirage::desktop::CaptureOutcome capture_roi(const mirage::desktop::WindowGeometry &,
                                                const mirage::desktop::CaptureLimits &,
                                                const mirage::desktop::CancelToken &) override {
        return {};
    }
};
class OcrScript final : public mira::IModelProvider {
  public:
    explicit OcrScript(mira::ModelProfile profile) : profile_(std::move(profile)) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    bool exposed = false, saw_result = false, result_failed = false, saw_text = false;
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.requested_model = profile_.model_selector;
        response.status = mira::ModelCompletionStatus::Completed;
        if (step_++ == 0) {
            for (const auto &tool : request.tools) {
                if (tool.wire_name != "desktop_vision_ocr")
                    continue;
                exposed = true;
                mira::ToolCallOutput call;
                call.tool_id = tool.tool_id;
                call.provider_name = tool.wire_name;
                call.provider_call_id.value = "ocr-test";
                call.arguments = Json{Json::Object{}};
                call.arguments_digest = mira::canonical_json_digest(call.arguments);
                response.output.emplace_back(std::move(call));
                return response;
            }
            mira::Error error;
            error.code = mira::ErrorCode::UnsupportedCapability;
            error.safe_message = "OCR tool missing";
            return error;
        }
        for (const auto &item : request.input)
            for (const auto &part : item.content)
                if (const auto *result = std::get_if<mira::ToolResultPart>(&part)) {
                    saw_result = true;
                    result_failed = result->failed;
                    saw_text =
                        mira::to_json_string(result->result).find("中A") != std::string::npos;
                }
        mira::MessageOutput message;
        mira::OutputTextPart text;
        text.text = "read complete";
        message.content.emplace_back(std::move(text));
        response.output.emplace_back(std::move(message));
        return response;
    }

  private:
    mira::ModelProfile profile_;
    unsigned step_ = 0;
};
void service_contract(const std::filesystem::path &dir, const std::filesystem::path &manifest) {
    using namespace mirage;
    for (const int mode : {0, 1, 2, 3, 4}) {
        const bool deny = mode == 1;
        runtime::ServiceConfig config;
        const auto base = dir / (mode == 0   ? "allowed"
                                 : mode == 1 ? "denied"
                                 : mode == 2 ? "failed"
                                 : mode == 3 ? "invalid-model"
                                             : "native");
        std::filesystem::create_directories(base);
        config.socket_path = (base / "service.sock").string();
        config.recovery_directory = base / "recovery";
        config.session_state_directory = base / "sessions";
        config.ocr_manifest = manifest;
        config.persist_settings = false;
        config.model.enabled = true;
        config.model.model_selector = "fixture";
        config.model.endpoint_origin = mode == 3 ? "invalid-origin" : "https://fixture.example";
        std::shared_ptr<OcrScript> script;
        config.model_provider_override =
            std::make_shared<integration::ModelProviderOverride>([&](const auto &profile) {
                script = std::make_shared<OcrScript>(profile);
                return script;
            });
        if (deny)
            config.permission_policy
                .rules[static_cast<std::size_t>(runtime::permission::Capability::ScreenCapture)] =
                runtime::permission::Rule::Deny;
        auto env = std::make_shared<testing::FakeDesktopEnvironment>();
        env->displays = {{"d1", {-25, 0, 32, 32}, true}};
        env->windows = {{"w1", "OCR fixture", {0, 0, 32, 32}, true}};
        env->active_window_id = "w1";
        env->failures.capture_error = mode == 2;
        auto native = std::make_shared<NativeOcrEnvironment>();
        std::shared_ptr<desktop::DesktopEnvironment> bound =
            mode == 4 ? std::static_pointer_cast<desktop::DesktopEnvironment>(native)
                      : std::static_pointer_cast<desktop::DesktopEnvironment>(env);
        runtime::RuntimeService service(config);
        const auto started =
            service.start(std::make_shared<integration::MiraEnvironmentBinding>(bound));
        MIRAGE_CHECK(started.ok == (integration::PpOcrPipeline::supported() && mode != 3));
        if (!started.ok) {
            MIRAGE_CHECK(
                started.error.code ==
                (integration::PpOcrPipeline::supported() ? "invalid_argument" : "unavailable"));
            continue;
        }
        runtime::ipc::IpcClient client(config.socket_path);
        auto observed =
            client.call(runtime::ipc::DesktopObserveRequest{false, true}, std::chrono::seconds{5});
        MIRAGE_CHECK(observed.ok == (mode == 0));
        if (deny) {
            MIRAGE_CHECK(observed.error.code == "permission_denied");
        } else if (mode == 2) {
            MIRAGE_CHECK(observed.error.code == "unavailable");
        }
        const auto sessions =
            client.call(runtime::ipc::ListSessionsRequest{}, std::chrono::seconds{2});
        MIRAGE_CHECK(sessions.ok);
        const auto id = std::get<runtime::ipc::SessionList>(sessions.payload).sessions.front().id;
        MIRAGE_CHECK(client
                         .call(runtime::ipc::SessionChatRequest{id, "OCR fixture", true},
                               std::chrono::seconds{2})
                         .ok);
        bool settled = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!settled && std::chrono::steady_clock::now() < deadline) {
            const auto history =
                client.call(runtime::ipc::ChatHistoryRequest{id, 10}, std::chrono::seconds{1});
            if (const auto *view = std::get_if<runtime::ipc::DialogHistory>(&history.payload);
                history.ok && view && !view->turns.empty() &&
                view->turns.back().status != "pending") {
                if (view->turns.back().status != (mode == 0 || mode == 4 ? "ok" : "failed"))
                    std::cerr << "mode=" << mode << " status=" << view->turns.back().status
                              << " error=" << view->turns.back().error << '\n';
                MIRAGE_CHECK(view->turns.back().status ==
                             (mode == 0 || mode == 4 ? "ok" : "failed"));
                for (const auto &part : view->turns.back().parts)
                    if (part.kind == "tool" && part.name == "desktop.vision.ocr")
                        MIRAGE_CHECK(part.output.find("中A") == std::string::npos);
                settled = true;
            }
            if (!settled)
                (void)::poll(nullptr, 0, 1);
        }
        MIRAGE_CHECK(settled);
        service.request_shutdown();
        MIRAGE_CHECK(service.run().clean);
        MIRAGE_CHECK(env->capture_calls == (mode == 4 || deny ? 0 : 2));
        if (mode == 4) {
            // DEC-115 can sample the authorized session in the background.
            // Preserve the single session/close contract without fixing the
            // number of captures to the foreground OCR request alone.
            MIRAGE_CHECK(native->captured >= 1);
            MIRAGE_CHECK(native->opened == 1 && native->closed == 1);
        }
        MIRAGE_CHECK(script && script->exposed && script->saw_result);
        MIRAGE_CHECK(script->result_failed == (mode != 0 && mode != 4));
        MIRAGE_CHECK(script->saw_text == (mode == 0 || mode == 4));
    }
}
} // namespace
int main(int argc, char **argv) {
    kairo::Executor executor;
    MIRAGE_CHECK(executor.initialize({}).ok);
    tool_contract(executor);
    ocr_scope_contract(executor);
    mirage::testing::FakeDesktopEnvironment env;
    env.displays = {{"d1", {-25, 0, 32, 32}, true}};
    const auto dir = std::filesystem::temp_directory_path() /
                     ("mirage-ocr-test-" + mira::OperationId::generate().to_string());
    const auto manifest = argc > 1 ? std::filesystem::path(argv[1]) : fixture(dir);
    if (argc > 2) {
        std::ifstream f(argv[2], std::ios::binary);
        std::string magic;
        int width = 0, height = 0, max = 0;
        f >> magic >> width >> height >> max;
        f.get();
        if (magic != "P6" || width <= 0 || height <= 0 ||
            static_cast<std::int64_t>(width) * height > 2000000 || max != 255)
            return 2;
        std::string rgb(static_cast<std::size_t>(width) * height * 3, '\0');
        if (!f.read(rgb.data(), static_cast<std::streamsize>(rgb.size())))
            return 2;
        env.displays = {{"d1", {0, 0, width, height}, true}};
        env.capture_frame_edit = [rgb](auto &frame) {
            for (std::size_t n = 0; n < rgb.size() / 3; ++n) {
                frame.pixels[n * 4] = static_cast<std::uint8_t>(rgb[n * 3 + 2]);
                frame.pixels[n * 4 + 1] = static_cast<std::uint8_t>(rgb[n * 3 + 1]);
                frame.pixels[n * 4 + 2] = static_cast<std::uint8_t>(rgb[n * 3]);
            }
        };
    }
    std::string error;
    auto pipeline =
        mirage::integration::PpOcrPipeline::create(executor, *env.screen(), manifest, error);
    if (!mirage::integration::PpOcrPipeline::supported()) {
        MIRAGE_CHECK(!pipeline && error.find("unavailable") != std::string::npos);
    } else {
        if (!pipeline) {
            std::cerr << error << '\n';
            return 1;
        }
        auto first = pipeline->refresh(true, {},
                                       std::chrono::steady_clock::now() + std::chrono::seconds{20});
        MIRAGE_CHECK(first.analyzed);
        std::string recognized;
        for (const auto &r : first.publish.snapshot.regions) {
            recognized += r.text;
            if (argc > 2)
                std::cout << "REGION " << r.bounds.x << "," << r.bounds.y << "," << r.bounds.width
                          << "," << r.bounds.height << " confidence=" << r.confidence
                          << " text=" << r.text << "\n";
        }
        if (argc > 2)
            std::cout << "OCR=" << recognized << "\n";
        if (argc > 3)
            MIRAGE_CHECK(recognized.find(argv[3]) != std::string::npos);
        if (argc == 1)
            MIRAGE_CHECK(recognized == "中A");
        if (argc == 1) {
            // DEC-075: already-authorized pixels do not enumerate a display
            // and cannot publish globally actionable visual references.
            auto image = env.screen()->capture_display("d1", {}, {});
            MIRAGE_CHECK(image.ok);
            auto displays = std::move(env.displays);
            const auto registry_scope = pipeline->registry().current().scope_ref;
            const auto captures = env.capture_calls;
            auto direct = executor
                              .submit_auto([&] {
                                  return pipeline->read_image_ocr(image.frame, {},
                                                                  std::chrono::steady_clock::now() +
                                                                      std::chrono::seconds(5),
                                                                  "ppocr-native-test");
                              })
                              .get();
            MIRAGE_CHECK(direct.ok && direct.regions.size() == 1 &&
                         direct.regions[0].text == "中A");
            MIRAGE_CHECK(!direct.model_revision.empty() && direct.backend_calls > 0);
            MIRAGE_CHECK(direct.continuous && direct.continuous->ok);
            const auto calls_before_query = pipeline->backend_calls();
            const auto state = pipeline->query_image_state("ppocr-native-test", {}, {});
            MIRAGE_CHECK(state.ok && state.analysis_age_ms && state.frame_sequence > 0);
            MIRAGE_CHECK(pipeline->backend_calls() == calls_before_query);
            MIRAGE_CHECK(env.capture_calls == captures);
            MIRAGE_CHECK(pipeline->registry().current().scope_ref == registry_scope);
            auto cancelled_image = mirage::desktop::CancelToken{};
            cancelled_image.request_cancel();
            MIRAGE_CHECK(pipeline->read_image_ocr(image.frame, cancelled_image, {}).cancelled);
            MIRAGE_CHECK(
                pipeline
                    ->read_image_ocr(image.frame, {},
                                     std::chrono::steady_clock::now() - std::chrono::seconds(1))
                    .error.code == "deadline_exceeded");
            image.frame.width = 1601;
            MIRAGE_CHECK(!pipeline->read_image_ocr(image.frame, {}, {}).ok);
            env.displays = std::move(displays);
        }
        const auto count = pipeline->backend_calls();
        auto second = pipeline->refresh(
            true, {}, std::chrono::steady_clock::now() + std::chrono::seconds{20});
        MIRAGE_CHECK(second.analyzed && pipeline->backend_calls() == count + 1 &&
                     count == (argc == 1 ? 2u : 1u));
        if (argc == 1) {
            env.set_pixel_value(0x77);
            auto changed = pipeline->refresh(
                true, {}, std::chrono::steady_clock::now() + std::chrono::seconds{5});
            MIRAGE_CHECK(changed.analyzed && pipeline->backend_calls() == count + 2);
            auto marked =
                pipeline->mark({}, {}, std::chrono::steady_clock::now() + std::chrono::seconds{5});
            MIRAGE_CHECK(marked.analyzed && !marked.marks.empty());
            MIRAGE_CHECK(marked.marked_frame.width == 32 && marked.marked_frame.height == 32);
            MIRAGE_CHECK(marked.marks.front().ref == marked.publish.snapshot.regions.front().ref);
            MIRAGE_CHECK(marked.frame.pixels != marked.marked_frame.pixels);
            env.capture_frame_edit = [&](auto &) { env.displays.front().geometry.x += 1; };
            auto moved = pipeline->refresh(
                true, {}, std::chrono::steady_clock::now() + std::chrono::seconds{5});
            MIRAGE_CHECK(!moved.analyzed && pipeline->registry().size() == 0);
            env.capture_frame_edit = {};
        }
        const auto captures = env.capture_calls;
        mirage::desktop::CancelToken cancelled;
        cancelled.request_cancel();
        MIRAGE_CHECK(pipeline->refresh(true, cancelled, {}).capture_cancelled);
        auto expired =
            pipeline->refresh(true, {}, std::chrono::steady_clock::now() - std::chrono::seconds{1});
        MIRAGE_CHECK(!expired.analyzed && env.capture_calls == captures);
        pipeline->stop();
        MIRAGE_CHECK(!pipeline->read_image_ocr(first.frame, {}, {}).ok);
        MIRAGE_CHECK(!pipeline->running() && pipeline->registry().size() == 0);
        pipeline.reset();
        if (argc == 1) {
            for (const Json &value :
                 {Json{"true"}, Json{std::int64_t{1}}, Json{nullptr}, Json{Json::Object{}}}) {
                fixture(dir);
                std::ifstream f(manifest);
                const auto document =
                    mira::parse_json(std::string(std::istreambuf_iterator<char>(f), {})).value();
                auto fields = *document.as_object();
                fields.emplace_back("geometry_marks", value);
                put(manifest, mira::to_json_string(Json{std::move(fields)}));
                MIRAGE_CHECK(!mirage::integration::PpOcrPipeline::create(executor, *env.screen(),
                                                                         manifest, error));
                MIRAGE_CHECK(error.find("boolean") != std::string::npos);
            }
            for (const Json &value :
                 {Json{"true"}, Json{std::int64_t{1}}, Json{nullptr}, Json{Json::Object{}}}) {
                fixture(dir);
                std::ifstream f(manifest);
                const auto document =
                    mira::parse_json(std::string(std::istreambuf_iterator<char>(f), {})).value();
                auto fields = *document.as_object();
                fields.emplace_back("roi_capture", value);
                put(manifest, mira::to_json_string(Json{std::move(fields)}));
                MIRAGE_CHECK(!mirage::integration::PpOcrPipeline::create(executor, *env.screen(),
                                                                         manifest, error));
                MIRAGE_CHECK(error.find("boolean") != std::string::npos);
            }
            for (int mode = 0; mode < 15; ++mode) {
                fixture(dir);
                const std::string raw(256, '\0');
                put(dir / "icon.bgra", raw);
                Json::Object entry{{"id", "icon"},
                                   {"format", "bgra8"},
                                   {"path", "icon.bgra"},
                                   {"sha256", mira::digest_string(raw).to_string()},
                                   {"bytes", std::int64_t{256}},
                                   {"width", std::int64_t{8}},
                                   {"height", std::int64_t{8}}};
                const auto replace = [&](const std::string &key, Json value) {
                    for (auto &[name, field] : entry)
                        if (name == key)
                            field = std::move(value);
                };
                if (mode == 4)
                    replace("id", "bad identity");
                if (mode == 5)
                    replace("format", "rgba8");
                if (mode == 6)
                    replace("width", std::int64_t{257});
                if (mode == 7)
                    replace("height", std::int64_t{7});
                if (mode == 8)
                    replace("bytes", std::int64_t{255});
                if (mode == 9)
                    replace("sha256", "incorrect");
                if (mode == 10)
                    replace("path", "missing.bgra");
                if (mode == 11)
                    entry.emplace_back("role", "button");
                if (mode == 12)
                    put(dir / "icon.bgra", std::string(257, '\0'));
                if (mode == 13)
                    put(dir / "icon.bgra", std::string((256u << 10) + 1, '\0'));
                Json declared{Json::Array{Json{entry}}};
                if (mode == 0)
                    declared = Json{nullptr};
                if (mode == 1)
                    declared = Json{Json::Array{}};
                if (mode == 2)
                    declared = Json{Json::Array(17, Json{entry})};
                if (mode == 3)
                    declared = Json{Json::Array{Json{entry}, Json{entry}}};
                std::ifstream f(manifest);
                auto root = *mira::parse_json(std::string(std::istreambuf_iterator<char>(f), {}))
                                 .value()
                                 .as_object();
                root.emplace_back("geometry_marks", mode != 14);
                root.emplace_back("templates", declared);
                put(manifest, mira::to_json_string(Json{std::move(root)}));
                const auto calls = env.capture_calls;
                MIRAGE_CHECK(!mirage::integration::PpOcrPipeline::create(executor, *env.screen(),
                                                                         manifest, error));
                MIRAGE_CHECK(!error.empty() && env.capture_calls == calls);
            }
            fixture(dir);
            put(dir / "det_bin", "bad");
            MIRAGE_CHECK(!mirage::integration::PpOcrPipeline::create(executor, *env.screen(),
                                                                     manifest, error));
            MIRAGE_CHECK(error.find("mismatch") != std::string::npos);
            fixture(dir);
            auto invalid = mira::parse_json([&] {
                               std::ifstream f(manifest);
                               return std::string(std::istreambuf_iterator<char>(f), {});
                           }())
                               .value();
            auto root = *invalid.as_object();
            root.emplace_back("model_revision", "caller-controlled-old-revision");
            put(manifest, mira::to_json_string(Json{std::move(root)}));
            MIRAGE_CHECK(!mirage::integration::PpOcrPipeline::create(executor, *env.screen(),
                                                                     manifest, error) &&
                         error.find("unknown") != std::string::npos);
            fixture(dir);
            put(dir / "manifest.json", std::string(32769, 'x'));
            MIRAGE_CHECK(!mirage::integration::PpOcrPipeline::create(executor, *env.screen(),
                                                                     manifest, error) &&
                         error.find("budget") != std::string::npos);
        }
    }
    if (argc == 1 && mirage::integration::PpOcrPipeline::supported())
        yolo_contract(executor, dir / "yolo");
    executor.shutdown(true);
    bool rejected = false;
    try {
        executor.submit_auto([] {}).get();
    } catch (...) {
        rejected = true;
    }
    MIRAGE_CHECK(rejected);
    if (argc == 1) {
        fixture(dir); // Restore the intentionally corrupted weight before service startup.
        service_contract(dir, manifest);
    }
    std::filesystem::remove_all(dir);
    return mirage::testing::finish("ppocr_pipeline_test");
}
