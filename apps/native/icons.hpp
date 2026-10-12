#pragma once

#include <string_view>

namespace mirage::native_ui::icons {
// Phosphor Icons Regular v2.1.1（MIT），随应用交付 assets/Phosphor-Regular.ttf。
// 每个视觉元素在这里登记独立图标；码点逐一经字体 cmap 与上游 2.1.1 regular
// 样式表核对（M6-40），不得在调用点内联新码点。同表允许跨不相交菜单复用同形
// （如 eye 同时承担"显示密钥"与"视觉模式"），同表内同码点必须同语义。
inline constexpr unsigned sidebar_panel = 0xec24;     // sidebar-simple 侧栏切换底形
inline constexpr unsigned caret_left = 0xe138;        // caret-left 侧栏收起箭头
inline constexpr unsigned caret_right = 0xe13a;       // caret-right 侧栏展开/过程折叠
inline constexpr unsigned caret_down = 0xe136;        // caret-down 下拉尾箭头
inline constexpr unsigned theme_dark = 0xe330;        // moon 深色主题
inline constexpr unsigned theme_light = 0xe472;       // sun 浅色主题
inline constexpr unsigned provider_fallback = 0xe1da; // cube 无预设 logo 的服务商/模型
inline constexpr unsigned refresh = 0xe094;           // arrows-clockwise 刷新/重新读取
inline constexpr unsigned add = 0xe3d4;               // plus 新建/添加
inline constexpr unsigned more = 0xe1fe;              // dots-three 服务商更多操作
inline constexpr unsigned show_secret = 0xe220;       // eye 显示密钥；视觉模式同形
inline constexpr unsigned hide_secret = 0xe224;       // eye-slash 隐藏密钥
inline constexpr unsigned remove = 0xe4a6;            // trash 删除条目/会话
inline constexpr unsigned close = 0xe4f6;             // x 关闭/移除
inline constexpr unsigned edit = 0xe3ae;              // pencil 重命名/编辑
inline constexpr unsigned copy = 0xe1ca;              // copy 复制过程/消息
inline constexpr unsigned failure = 0xe4e0;           // warning 失败与退出警示
inline constexpr unsigned jump_latest = 0xe03e;       // arrow-down 回到底部
inline constexpr unsigned quote = 0xe660;             // quotes 引用选段/引用来源
inline constexpr unsigned attach_file = 0xe39a;       // paperclip 添加文本附件
inline constexpr unsigned attachment = 0xe23a;        // file-text 附件与文件类附件
inline constexpr unsigned image = 0xe2ca;             // image 图片类附件
inline constexpr unsigned selected = 0xe182;          // check 当前选中模型
inline constexpr unsigned settings = 0xe270;          // gear 设置/管理模型
inline constexpr unsigned back = 0xe058;              // arrow-left 返回对话
inline constexpr unsigned desktop_tools = 0xe32e;     // monitor 桌面工具
inline constexpr unsigned models = 0xe762;            // robot 模型
inline constexpr unsigned appearance = 0xe6c8;        // palette 外观
inline constexpr unsigned session = 0xe168;           // chat-circle 会话行/空态
inline constexpr unsigned session_running = 0xeb44;   // circle-notch 运行中的会话
inline constexpr unsigned about = 0xe2ce;             // info 关于
inline constexpr unsigned minimize = 0xe32a;          // minus 最小化
inline constexpr unsigned maximize = 0xe1d0;          // corners-out 最大化
inline constexpr unsigned restore = 0xe1ce;           // corners-in 还原
inline constexpr unsigned send = 0xe08e;              // arrow-up 发送
inline constexpr unsigned stop = 0xe46c;              // stop 停止运行
inline constexpr unsigned mode_chat = 0xe17a;         // chat-text 对话模式
inline constexpr unsigned mode_full = 0xe412; // shield-warning 完全访问（可申请键鼠）
inline constexpr unsigned authorize = 0xe40c; // shield-check 授权并记住
inline constexpr unsigned forget_authorization = 0xe410; // shield-slash 忘记授权
inline constexpr unsigned open_manifest = 0xe256;        // folder-notch-open 选择视觉清单
inline constexpr unsigned save = 0xe248;                 // floppy-disk 保存
inline constexpr unsigned fetch_models = 0xe20c;         // download-simple 获取模型
inline constexpr unsigned clear_history = 0xec54;        // broom 清空对话
inline constexpr unsigned discard = 0xe038;              // arrow-counter-clockwise 放弃修改
inline constexpr unsigned font_size = 0xe6ee;            // text-aa 文字大小
inline constexpr unsigned code_font = 0xe1bc;            // code 代码字号
inline constexpr unsigned part_thinking = 0xe74e; // brain 思考过程（同 reasoning 开启）
inline constexpr unsigned part_desktop = 0xe7c8;  // cursor-click 桌面动作
inline constexpr unsigned part_wait = 0xe2b2;     // hourglass 等待
inline constexpr unsigned part_tool = 0xe5d4;     // wrench 其他工具

// 思考深度：关闭→最高以"亮度递增"阶梯各配独立图标；默认沿用 sparkle。
inline unsigned reasoning(std::string_view value) {
    return value == "none"       ? 0xe3de // prohibit 关闭
           : value == "adaptive" ? 0xe74e // brain 开启（模型自适应）
           : value == "minimal"  ? 0xe2dc // lightbulb 最少
           : value == "low"      ? 0xe58e // moon-stars 低
           : value == "medium"   ? 0xe652 // planet 中
           : value == "high"     ? 0xe6a4 // star-four 高
           : value == "xhigh"    ? 0xe2de // lightning 极高
           : value == "max"      ? 0xe472 // sun 最高
                                 : 0xe6a2;     // sparkle 默认
}

// 桌面访问权限：对话/视觉/完全访问各配独立图标；完全访问用警示盾提示键鼠风险。
// 附件按扩展名区分图片与文件；当前附件仍限 UTF-8 文本，扩展名仅决定字形。
inline unsigned attachment_type(std::string_view name) {
    auto has_suffix = [&](std::string_view suffix) {
        return name.size() >= suffix.size() && name.substr(name.size() - suffix.size()) == suffix;
    };
    for (std::string_view suffix : {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".svg"})
        if (has_suffix(suffix))
            return image;
    return attachment;
}

// 流程部分按任务类型配独立图标：思考、桌面动作、等待，其余工具用扳手兜底。
inline unsigned part_kind(std::string_view kind, std::string_view name, bool desktop_evidence) {
    return kind == "thinking"                                   ? part_thinking
           : (desktop_evidence || name.starts_with("desktop.")) ? part_desktop
           : name == "wait"                                     ? part_wait
                                                                : part_tool;
}
} // namespace mirage::native_ui::icons
