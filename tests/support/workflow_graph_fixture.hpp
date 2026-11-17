#pragma once

namespace mirage::testing {
inline constexpr const char *workflow_loop_ir = R"({
            "schema_version":{"major":1,"minor":0},
            "workflow_id":"dddddddddddddddddddddddddddddddd","name":"批量发票核对","summary":"有界回环示例",
            "steps":[
                {"step_id":"11111111111111111111111111111111","name":"读取下一份发票","kind":"tool_call","loop_head":true,"arguments":{"tool":"desktop.filesystem.read_text","path":"/invoices/next"}},
                {"step_id":"22222222222222222222222222222222","name":"核对发票内容","kind":"tool_call","arguments":{"tool":"desktop.filesystem.read_text","path":"/invoices/check"}},
                {"step_id":"33333333333333333333333333333333","name":"继续处理 · 最多 4 次","kind":"control","jump_to":"11111111111111111111111111111111","max_iterations":4}
            ],"default_policy":"strict","allowed_policies":["strict","dry_run"]
        })";
} // namespace mirage::testing
