# 依赖问题反馈台账

> 状态：Active
> 维护规则：[工程规范第 9.4 节](../project/project-standards.md)
> 更新日期：2026-09-22

本台账登记 Mirage 对两个直接 pinned 依赖 —— `third_party/mira` 与
`third_party/mirador` —— 的能力缺口反馈与获批例外。编号按依赖区分：
`MIRA-YYYYMMDD-NNN`、`MIRADOR-YYYYMMDD-NNN`。"依赖不支持"不构成有效记录，条目必须附
可复现证据、影响范围、期望语义与可验收结果。

executor（`third_party/mira/third_party/executor`）由 mira pin 并交付，属于 mira 的
集成面：Mirage 发现的 executor 层缺口以 `MIRA-*` 条目登记，由 Mira 上游经其自身的
executor 反馈流程消化；Mirage 不直接向 executor 反馈，也不在台账中单列 executor 条目。

| 编号 | 日期 | 主题 | 分级 | 状态 | 关联实现/测试 |
| --- | --- | --- | --- | --- | --- |
| MIRA-20260922-001 | 2026-09-22 | executor 的 MinGW-w64 交叉构建失败（`std::thread::native_handle_type` 假设 win32 线程模型） | build 缺口（不阻塞 MSVC 主工具链） | Open | M4-02 交叉构建预检；阻塞 M4-06 的全树 MinGW 交叉门禁 |

## MIRA-20260922-001：executor 的 MinGW-w64 交叉构建失败

- **现象与可复现证据**：MinGW-w64 x86_64 GCC 13.2.0（posix 线程模型，用户前缀
  `~/.local/mirage-mingw`，DEC-017 决策 2 引导方式）交叉构建 pinned executor：

  ```
  third_party/mira/third_party/executor/src/executor/blocking_io_executor.cpp:157:28:
  error: invalid 'static_cast' from type 'HANDLE' {aka 'void*'} to type
  'std::thread::native_handle_type' {aka 'long long unsigned int'}
      157 |         auto self_handle = static_cast<std::thread::native_handle_type>(GetCurrentThread());
  ```

  复现命令：`MIRAGE_MINGW_PREFIX=$HOME/.local/mirage-mingw/usr cmake --preset win64-cross
  && cmake --build build/win64-cross`（默认 all 目标；2026-09-22，M4-02 交叉构建预检）。
  MSVC（windows-latest，VS 18 2026）不受影响：win32 线程模型的
  `native_handle_type` 即 `void*`，同语句合法（M4-01 CI 已实测 executor 随全树
  configure 通过、子集构建成功）。
- **根因**：executor 源码假定 `std::thread::native_handle_type` 可从 `HANDLE`
  static_cast 而来；MinGW-w64 的 posix 线程模型下它是整数类型，与 `void*` 之间无
  隐式转换关系。这是可移植性缺陷，不是 Mirage 用法错误（已核对 pinned 版本源码，
  无 API 选型或配置替代）。
- **影响范围**：MinGW-w64 交叉门禁只能构建不含 executor 的目标子集（platform /
  desktop / integration / 各测试——M4-01 起的既定门禁形态）。不阻塞 MSVC 主工具链
  与当前任何工作项；M4-06（产品进程 Windows 化）若要求全树 MinGW 交叉构建则被此
  缺陷阻塞，届时期望以 MSVC 门禁 + 本条目状态评估是否放行。
- **期望语义与建议的最小能力**：executor 源码不依赖具体线程模型的
  `native_handle_type` 表达（移除该 cast，或经 `native_handle()`/条件编译适配
  posix 模型）；修复后 MinGW-w64 posix 与 MSVC 双工具链均可构建。
- **可验收结果**：以 pinned executor 在 MinGW-w64 GCC（posix）下 `ninja`/`make`
  构建零诊断为验收；Mirage 侧验证方式 = `cmake --build build/win64-cross`（默认
  all）成功。
- **Mirage 侧临时措施**：无（不修改 pinned 代码；Windows 门禁在 M4-06 前按
  DEC-017 决策 9 只构建 platform 子集，子集不含 executor）。
