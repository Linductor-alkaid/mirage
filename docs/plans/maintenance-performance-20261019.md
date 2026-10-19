# 前端、感知后端与桌宠性能优化

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[实施总计划](mirage-implementation-plan.md)
> 更新日期：2026-10-19

## 目标与范围

按维护者要求降低 native 前端的渲染时间和内存、后端的 CPU 与本地推理开销、pet 的
CPU 和内存。以当前未提交的 observation-efficiency 工作树为基线，保留已有改动。
不变更 pinned 依赖，不降低图片分辨率、播放帧数或权限/验证要求。
远程供应商生成时延与本地 OCR 分开计量，不以本地缓存结果宣称远程模型加速。

## 工作项与依据

- [ ] `M6-47` In Progress：保留历史行几何，仅为视口附近消息构建正文/控件；滚动、
  流式跟随、选区、展开与窗口变化无回归。依据 DEC-101、DEC-108。
- [ ] `M7-24` In Progress：相同已授权图像复用成功 OCR，保持新帧、epoch、授权和
  模型边界。依据 DEC-107、DEC-108；仅复用同一 DesktopImageTurn 的实际图像结果。
- [ ] `M8-13` In Progress：无损压紧透明边缘、按实际播放节奏呈现，停止静态帧的持续
  重绘；保留取消、隐藏恢复和关闭路径。依据 DEC-084、DEC-107、DEC-108。

## 依赖与承载

前端复用 EUI 公共 ScrollView/scrollState；其 VirtualList 只支持定高行，不适用于可变
高度消息。GPU 图像使用 EUI 公共导入接口，由 UI 线程持有/上传/回收纹理。
pet 定时仍使用进程唯一 Executor 的 TimerHandle；加载仍为原有 cancellable task。
OCR 使用 pinned mirador 后端；其感知指纹缓存不能提供产品已授权 PNG 的精确身份，
复用在已有 DesktopImageTurn 边界内完成，不重建通用视觉缓存。

## 验证与退出条件

- [x] Release 构建、相关回归与完整测试通过。
- [x] 原生视图、pet 像素/拖动/隐藏恢复/失联关闭回归通过。
- [x] 同机基线/优化性能与内存取证，注明软件渲染与采样限制。
- [x] ASAN/UBSAN；定时/取消路径涉及跨上下文变更时 TSAN。
- [x] 文档、设计与结果同步；真实 compositor、高 DPI、Windows 未验项留开放。

## 验证记录

2026-10-19：开始基线取证；命令、环境、结果及限制将在
[性能记录](../benchmarks/native-runtime-pet-20261019.md)汇总。未重启用户运行实例，
不提交用户会话或模型凭据。未执行平台验证由 Mirage 维护者在取得相应环境后补跑。


### 2026-10-19：Linux 本地实现与性能验收

M6-47：仅视口附近正文/控件构建；40 轮/16 KB 多段整帧 p50/p95 从 16.18/21.71
降至 4.19/6.74 ms，单段从 20.18/27.11 降至 9.62/13.31 ms。三轮配对输入进程
峰值 RSS 降 4.1%–4.4%；无历史短输入未证明加速，全部 case 留在性能记录。
正常/最小、明/暗四张截图与已有基线逐字节一致，滚动/跟随/选区/过程展开回归通过。

M7-24：同图 41 次 OCR 请求仅一次实际分析；动作 epoch 改变即重新分析，ROI
和失败/撤权回归通过。未改远程模型配置，也未测在线首 Token/生成速率，不外推为
供应商推理加速。现有工作树的 ActionWorker 空闲 CPU 回归也通过（2 秒墙钟内 0 ms
CPU），该修复早于本次新增代码，不能算作本轮新增收益。

M8-13：九态全部帧与独立 decoder 的原始 RGBA 逐字节一致，缓存减少 19%–53%；
公开 GPU 导入、原生纹理回收、按源时长替换 Executor timer、反复设定不饿死计时器、
静态停止与失败清图像素/同纹理恢复已验证。真实私有进程性能及恢复/退出取证见
[性能记录](../benchmarks/native-runtime-pet-20261019.md)。

Release 全量 103/103；ASAN/UBSAN 各 9/9、TSAN 6/6（含 fixture）；后补 cadence
四预设各 3/3，GPU 清图像素 Release/ASAN/UBSAN 各 3/3。格式、公开头边界和 diff
空白检查通过。原始失败、字体目录误用及 TSAN 宿主映射诊断保留在性能记录。

三个工作项本地子门禁完成；整体仍为 In Progress。真实 GNOME compositor、物理高
DPI/硬件 GPU 与 Windows 未执行，负责人 Mirage 维护者；取得环境后按同负载补跑。
未替换或重启用户当前运行实例，未修改 pinned 依赖，未提交同期无关工作树改动。

### 2026-10-19：native-release 本地更新

按维护者要求完成 native-release 重建与全量 CTest 复验（103/103，66.04 s）。
更新前无运行实例；已启动本次 tray、native、pet。服务 running、frontend ready，
native/pet 窗口可见，三个运行进程的可执行文件摘要均与本次构建一致。启动证据见
[性能记录](../benchmarks/native-runtime-pet-20261019.md#native-release-更新与启动验收)。
此项仅关闭本地部署和启动冒烟子项，平台性能门禁继续开放；未发起模型请求。
