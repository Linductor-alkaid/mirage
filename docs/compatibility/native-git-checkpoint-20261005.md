# 原生前端Git交付checkpoint

> 日期：2026-10-05
> 状态：Completed（本地checkpoint）
> 负责人：Mirage维护者
> 分支：codex/native-agent-workbench
> 基线：e1ffb97（master原起点）
> 关联：M6-01/02/03/05/06

## 提交边界

1. `build(deps): pin EUI-NEO dev for the native frontend`：EUI dev gitlink/锁文件、
   依赖使用边界、构建适配与依赖反馈；Mira/Mirador及内嵌Executor pin不变。
2. `feat(runtime): connect Mira harness and context usage`：模型配置、通用harness、
   协作取消、最近请求用量、native IPC bridge及故障注入测试；通过tests/CMakeLists
   暂存拆分保持运行时提交无需原生视图源码即可配置。
3. `feat(ui): add the native ZCode-style agent workbench`：原生EUI入口、Mira图标/Dock注册、
   侧栏/设置、会话输入/引用/Markdown/占用圆环，模型测试与相关设计、计划和验收证据。

未纳入原先已存在的apps/cli、apps/desktop、packaging/linux及M5计划改动；文件SHA256
在Git整理前后比对，不撤销、不混入本轮。无关未引用的旧临时截图/interaction JSON保留
本地，未入提交。未提交构建产物、日志或凭据；单个文件无超过5MiB项目，必要Mira
PNG/ICO资产保留来源记录，截图按验收/审查引用纳入，供fresh checkout复核。

## 验证与限制

- clang-format18.1.3只格式化本轮源码；格式化前确认所涉旧HEAD源码原本format-clean。
- Debug全树构建成功，CTest51/51通过（46.16s）；Release原生UI构建、模型1/1通过。
- 运行时索引快照以MIRAGE_FETCH_DEPENDENCIES=OFF、桌面壳OFF实际CMake配置通过，
  原生视图尚未纳入时运行时测试目标可配置。
- 运行时与最终暂存内容各导出独立索引快照，以仓库CheckFormat.cmake和BoundaryCheck.cmake
  检查；与未提交的旧CEF/CLI工作树隔离。公开头文件48个，0违规。
- Git staged diff/whitespace、依赖gitlink与锁文件一致、本人身份、凭据内容与大小检查通过。
  凭据扫描仅在内存比较已授权本机Mira测试模型凭据，未打印或记录值。
- 本轮只有格式及文档同步，不新增行为；ASAN/UBSAN、TSAN及实机视觉证据沿用对应M6验收。
- master未变更，未push/创建PR/合并，未声称远端CI或Windows原生GUI验收通过。
  后续远端交付在短分支继续进行；整体入口/托盘M6-04和Windows待验保持原状态。
