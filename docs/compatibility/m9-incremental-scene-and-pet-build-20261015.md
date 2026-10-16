# M9 首增量与 M8-12 产品构建验证

> 状态：In Progress（本地首增量已验证；里程碑与发布门禁仍开放）
> 日期：2026-10-15
> 负责人：Mirage 维护者
> 工作项：M9-01/05/13、M8-12
> 依据：DEC-090/091/092；当前工作树叠加既有并行改动，未创建提交或修改依赖 pin。

## 交付范围

1. 新增同步、单 owner 的 `desktop::VisualScene`。完整输入或增量均只生成变化/
   删除对象；保留未变化条目及语义；支持移动继承、内容失效、pending/身份不确定
   挂起、恢复、实例替换和场景重置。版本/容量错误整次拒绝。
2. 新增 Mirage 所有的对象语义绑定，原 OCR/text、detector label 与模型语义分开；
   用源/场景/实例/内容版本及引入帧拒绝陈旧绑定。每对象当前仅一个语义标签；
   模型请求来源鉴权须由上游可信 owner 完成，本层不是 IPC/model tool 的鉴权面。
3. Mirador mapper 提供对象映射，保留 stable_id、原始 text/label；调用方显式给
   incarnation、content_revision 和身份确认，不将 fusion ID 自动视为已确认身份。
4. Native presets 显式开启 pet，`mirage-native-product` 聚合五个产品进程；
   README 与 native CI 使用同一入口。`ServiceConfig.pet_expected` 区分缺 pet
   和无 pet 的 headless 用途。缺可执行文件时产品状态/启用请求给出明确诊断，
   Agent 状态投影与服务继续可用。
5. 修正反馈编号碰撞：已发布 UTF-8 锚点问题保留 MIRA-20261015-001，持续循环
   缺口改为唯一编号 MIRA-20261015-002。当前 pin 560da5ec 仍无无累计次数模式。

没有接入 native 语义观察/LLM 标定入口、连续帧调度、ObjectTracker 或本地等待；
没有删除 16/32 等累计停止条件。状态层的逻辑存量测试不代表采集链路 RSS/GPU
或端到端 token/延迟收益。现有 native-release 实例未重建、替换或重启。

## 契约与容量

见[对象状态层设计](../design/visual-scene-incremental-state.md)与
[资源/进度清单](../design/m9-resource-and-progress-policy.md)。默认 256 对象、
512 KiB 逻辑存量；无像素、后台任务、共享可变状态或私有队列。输入和工作区
有界，旧基线/模型标定在拒绝后不改变，显式快照之外不复制完整输出。

## 环境和自动化

Linux x86_64，GCC 13.3.0，CMake 3.28.3；pinned Mira 560da5ec、Mirador ae8cf87、
EUI 89c2d0f。仅消费当前依赖，不把其他任务先前的升级归为本轮操作。

```bash
cmake --build build/debug --target visual_scene_test visual_scene_mapping_test \
  tray_runtime_test visual_observation_loop_test observation_assembler_test \
  desktop_observation_test -j 4
ctest --test-dir build/debug --output-on-failure \
  -R '^(visual_scene(_mapping)?_test|tray_runtime_test|visual_observation_loop_test|observation_assembler_test|desktop_observation_test)$'

cmake --build build/asan --target visual_scene_test visual_scene_mapping_test tray_runtime_test -j 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir build/asan --output-on-failure \
  -R '^(visual_scene(_mapping)?_test|tray_runtime_test)$'

cmake --build build/ubsan --target visual_scene_test visual_scene_mapping_test tray_runtime_test -j 4
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/ubsan --output-on-failure \
  -R '^(visual_scene(_mapping)?_test|tray_runtime_test)$'

cmake --build build/tsan --target visual_scene_test tray_runtime_test -j 4
setarch x86_64 -R env TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/tsan --output-on-failure \
  -R '^(visual_scene_test|tray_runtime_test)$'
```

- Debug：六个功能目标加隔离 setup/cleanup **8/8**；新状态层 **4087 checks**、
  mapper **14 checks**。包括四种视觉来源、2000 次完整未变输入下零变化输出/
  固定逻辑存量、拒绝原子性、晚到/错源/实例复用及标定容量拒绝。
- ASAN **5/5**（含夹具，detect_leaks=1）、UBSAN **5/5**，无 sanitizer 报告。
- TSAN **4/4**（对象状态与 tray 生命周期/缺载体场景加夹具；setarch 关闭 ASLR），
  无 sanitizer 报告。本层未创建新并发设施，仍用既有 Runtime/kairo 路径验证关闭。
- 公共头边界：**59 个头、0 违规**。本轮全部改动 C++ 文件的 clang-format 与
  git diff --check 通过。全仓 format 失败于开工前已有、非本轮修改的
  `tests/integration/desktop_context_builder_test.cpp` 格式；保留该并行改动，不修饰
  成全仓通过，也不将其额外测试增量纳入本轮归属。
- 日志：`/tmp/mirage-m9-{debug,asan,ubsan}-test.log`、
  `/tmp/mirage-m9-{asan,ubsan,tsan}-build.log`、`/tmp/mirage-m9-tsan-test.log`；
  状态层最后一次避免重复拷贝的复验构建日志使用 `*-scene-build.log`。

初验失败保留：对象预算用例先命中内容版本守卫，已修正夹具使其真正测试预算；
新 tray 测试曾误用 report.ok，改为既有 report.clean；UBSAN 曾在构建尚未完成
时提前启动，mapper 标为 Not Run，待两个目标构建成功后复跑为 5/5。均未把前次
失败或未运行覆盖为“初次通过”。

## Native 产品与私有桌面验证

执行 `cmake --build build/native-debug --target mirage-native-product -j 4` 成功。
这验证现有 Debug 目录的完整目标，不宣称全新目录所有编译单元已重新构建。
另在全新临时目录用 native-debug preset 配置（离线复用已锁定 ncnn/vision 源），
确认无旧 cache 下 native/pet/vision 均为 ON，聚合依赖含 launcher、service、
tray、native、pet 五目标。fresh 配置记录：
`/tmp/mirage-m9-native-fresh-configure.log`，目录在
`/tmp/mirage-m9-native-config-path.txt`；未改用户快捷方式。

```bash
python3 tests/manual/pet_preview_acceptance.py --build build/native-debug \
  --output /tmp/mirage-m9-pet-20261015
```

独立 Xvfb、私有 DBus/XDG 目录、ICCCM/compositor-selection fixture；搬迁 tray/
pet/字体和九套素材后从 `/` 启动，**32/32 通过**。覆盖独立受管 PID、ready、
透明和角色 RGBA 像素、动画推进、气泡预留及输入穿透、拖动/位置恢复、独立启停、
崩溃诊断与恢复、隐藏退出及 owner 异常失联。没有模型请求或读取用户窗口。

这是软件渲染 fixture 的运行取证，不是用户 compositor、硬件 GPU、九态完整
性能、原生 Wayland/Windows 或正式安装验收。机器样本 VmHWM 178620 kB、可见
28.66% 单核 CPU、隐藏 1.0%，不作跨设备产品预算。工件为上述目录的
`pet-results.json`、`pet-rgba.png`、进程日志；总日志
`/tmp/mirage-m9-pet-acceptance.log`。

## 尚未关闭的工作

- M9-02：MIRA-20261015-002；负责人 Mira/Mirage 维护者，上游公开能力交付并
  明确授权消费后补持续循环/历史/取消/准入验证。没有改 third_party 绕行。
- M9-05/13：native/IPC/模型请求来源校验、多字段语义依赖和真正 LLM 标定接线
  未实施；负责人 Mirage 维护者，按已建 Core 契约继续交付后补端到端。
- M9-06～10：帧复用/门控/跟踪/动作/等待仍待实施与性能矩阵，不能从纯状态层
  验收推出实时能力或工具往返下降。
- M8-12：实际 native-release 发布、运行版本诊断、真实桌面及完整九态门禁
  未完成；正式安装/许可/偏好仍归 M8-08，其他平台归 M8-10。
