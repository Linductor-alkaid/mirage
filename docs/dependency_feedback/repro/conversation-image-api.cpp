// API-shape evidence for MIRA-20261009-001; no runtime or platform actions.
#include <mira/conversation_loop.hpp>
#include <mira/model_contracts.hpp>
#include <mira/tool_executor.hpp>

#include <type_traits>

template <typename Loop>
constexpr bool has_model_input_supplier = requires { &Loop::set_model_input_supplier; };
#ifdef MIRAGE_EXPECT_MODEL_INPUT_SUPPLIER
static_assert(has_model_input_supplier<mira::ConversationLoop>);
static_assert(std::is_same_v<mira::ModelInputSupplier::result_type,
                             mira::Result<std::vector<mira::ModelInputItem>>>);
static_assert(std::is_same_v<decltype(&mira::ConversationLoop::set_model_input_supplier),
                             void (mira::ConversationLoop::*)(mira::ModelInputSupplier,
                                                              mira::ModelInputSupplyOptions)>);
#else
static_assert(!has_model_input_supplier<mira::ConversationLoop>);
#endif

static_assert(std::is_same_v<mira::BuiltinToolHandler::result_type, mira::Result<mira::JsonValue>>);
static_assert(!std::is_convertible_v<mira::ModelInputItem, mira::JsonValue>);
static_assert(std::is_same_v<decltype(mira::AgentLoopSpec::goal), std::string>);
static_assert(std::is_same_v<decltype(mira::ToolResultPart::result), mira::JsonValue>);

// ImagePart is expressible in a ModelRequest, but not in the handler result.
static_assert(std::is_constructible_v<mira::ModelContentPart, mira::ImagePart>);
