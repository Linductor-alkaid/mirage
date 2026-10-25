# 原生观察修复与 M9 产品接线验证

> 状态：In Progress
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M9-05/06/07/11/12/18，关联 M7-18/25/26
> 基线：a4c73d7，feat/continuous-perception-cycle
> 契约：[原生连续感知接线](../design/native-continuous-perception-wiring.md)

## 最新会话与修复

只读检查 2026-10-22 17:35～17:48 的最近会话。7 轮正常结束；计量为推理
79 次/319,883 ms，取帧阶段（含内部守卫）64 次/3,717 ms，OCR 9 次/10,597 ms。
这不是配对基准，不声明相对速度提升。未复制用户目标、识别文本、截图、命令输出
或认证配置到仓库。3 次失败分别是 ROI 字节预算、裁剪图坐标越界和 OCR 校验失败。

按维护者确认的顺序实施：

1. AT-SPI 原生窗口 Provider 遍历 desktop -> application -> 顶层 frame/window/dialog，
   兼容直接窗口；4096 个访问节点预算独立于 max_windows，超额/取消清空部分结果。
   不下降到控件树，不声称取得 compositor 激活能力。
2. 应用 Exec 身份解析有界解开 env 的赋值、-i、-u/--unset 与 --，未知选项不作为
   进程身份。清单展示采用 ASCII 大小写等值匹配；选择终止 PID、launch 去重等
   控制面继续使用精确匹配。未采用模糊包含或扫描 launcher 脚本内容。
3. 预览按边长和原始像素预算共同缩小，修复 1420×1560 BGRA（8,860,800 字节）
   在边长不超过 1600 时漏过缩放的问题；不扩张 8 MiB 上限，保留精确坐标比例。
4. OCR 在融合前及产品边界区分 invalid_result_text/confidence/geometry，并报告
   无文本的区域下标。拒绝非法结果，不裁剪猜测或放宽校验；未保留原故障画面，
   不能据此宣称已确认/修复原 OCR 数据异常的具体原因。
5. 能力提示明确只有请求观察验证时回执才带新图；shared_frame_observed 只证明新帧，
   不证明业务完成。文本入口、图片输入授权和历史查询分别说明。

## M9 生产路径

产品默认 PP-OCR pipeline 启用同一个 VisualSessionHost worker 内的独立 image
session/coordinator。复用原有后端、取消、latest-wins admission 和 future；不新增
模型副本、后台采样 timer、私有线程或队列。显示与图像空间互不污染，原 display
模板与可执行引用继续走原流程。Mirador master 经 fetch 确认仍为 2a9f483，pin/lock
均未修改。

Runtime 注入带可信 task/authority/源几何身份的 OCR callback 和无帧查询 callback。
原生 desktop.vision.ocr 2.1.0 保留旧空参数行为，新增 text_only=true：直接分析
受权预览，不编码 PNG/生成 artifact/供图，退休旧图片输入引用；图片细节和输入
仍须显式 capture。返回的文本是未可信输入，区域/JSON 前缀预算保持。

desktop.vision.state 1.0.0 返回同一任务/来源的最后分析序号、年龄和 advisory 状态
摘要；不取图、不 OCR、不推进观察、不授权输入，也不作为动作后核验证据。检查
调用前及交付前 authority/取消，来源缺失、过期或关闭显式拒绝。既有会话/IPC 的
过程 JSON 保留 text_only/continuous 摘要，不持久化 OCR 正文。

预算化 OCR 保留五秒、K=64、deferred/partial 契约，连续状态不登记完整覆盖采纳或
确认事件。后台采样、变化驱动零 forward、ROI 证据复用、焦点协议、订阅/gap 恢复
和无图动作引用仍开放，不能把本次按需消费者等同于 M9 全部完成。

## 实机只读核验

用当前 Debug 静态库链接临时探针，仅调用窗口/应用清单，不输出窗口标题、用户
内容或进程参数。结果为 windows_ok=1、windows=13、apps_ok=1、chatgpt_found=1、
chatgpt_running=1，进程退出 0。探针与日志只留在 /tmp/mirage-native-provider-probe*。
当前运行产品尚未重启，因此不把探针结果称为运行中的旧托盘已经应用修复。

## 自动化验证

专项覆盖真实隔离 AT-SPI 协议、env/大小写清单与控制面隔离、像素预算、OCR 三类
错误、文本入口不供图和旧引用退休、无帧查询不调用 backend、来源切换、display
隔离、预算化 OCR 不采纳部分覆盖，以及同一 companion 的异常/取消/期限/关闭。

首轮 ASan 定向构建漏了 m205_process_helper，测试失败原因是 helper 不存在，未作
通过记录；已补 application_backend_test 的 target dependency 后重跑。新增 display
隔离测试首轮漏填 oriented -> display transform，修正夹具后通过；未放宽生产验证。
收尾 TSan 大 ROI 夹具在并行编译负载下触发原生编码路径硬编码的五秒取消。
该路径现遵循既有 acquire_budget 参数，默认仍为生产五秒；TSan 夹具显式使用
60 秒采集/图片年龄，不放宽产品默认值，补跑通过，无 sanitizer/race 报告。

全仓格式检查存在基线 output-limit 分类代码的格式问题（model_layer.cpp 与
protocol.cpp）；本轮保留原格式，单独检查实际变更区段。不把无关格式修正混入提交。

真实用户任务重放、Windows、compositor/pet 像素、低端设备配对资源与长时间 soak
未执行；负责人 Mirage 维护者，补跑条件为最终产品重启、目标设备与授权桌面、
受控样本和预登记门槛就绪。M9 各相关工作项保持 In Progress。


## 最终结果

- 最终 Debug 全量 **110/110，318.17 s**；此前全量 110/110，304.50 s。
  专项 9/9 包含新 producer/consumer、窗口/应用及 PP-OCR 边界。
- ASan/UBSan/TSan 专项各 **8/8**；最后编码预算/来源键/模型文本供给收尾后，
  image supply 及隔离夹具各 **3/3**。TSan 仅测试进程 setarch -R，无新增 suppression。
- 最终 native-release 完整 mirage-native-product 重建，专项 **10/10，23.10 s**，
  含 native agent 和真实 ncnn 的受控 PP-OCR fixture，后者断言产品连续状态可查询、
  查询不增加 backend 调用。五个可执行程序及动态库解析检查通过；未重启旧实例。
- 补充 1600×1000 BGRA 预览到 RGB 后端的真实转换路径，证明 1 MiB compact
  cache 不会错误拒绝正常预览；Debug/ASan/UBSan/TSan/Release 均 **3/3**（含夹具）。
- 65 个公开头边界检查零违规，保留 DEC-096 两条既有 seam；变更 C++ 格式与
  model 提示词区段检查、Markdown 本地链接和 git diff --check 通过。全仓格式仍受
  上述两个 HEAD 基线问题限制，未记为通过。

复现使用 cmake --build --preset 配置、ctest --preset debug；三个 sanitizer 定向
匹配 atspi_backend_test、application_backend_test、desktop_image_supply_test、
visual_observation_e2e_test、continuous_perception_consumer_test、visual_session_budgeted_test。
Release 再加入 ppocr_pipeline_test、native_agent_integration_test；收尾只补跑实际变更的
image supply。测试隔离 setup/cleanup 计入 CTest 数量。原始日志前缀为
/tmp/mirage-native-wire-，结果和最终二进制 SHA 见
[机器摘要](data/native-observation-and-continuous-wiring-20261022.json)。
