# 任务连续感知与短提示验收（2026-10-23～24）

工作项 M9-21（In Progress），依据 DEC-115。负责人：Mirage 维护者。
本记录覆盖生产接线、私有显示和脚本模型；不宣称实际聊天任务的完成率。

## 会话调查

最新本地留存任务为 failed：100 次模型调用（516701 ms）、99 次采集阶段调用、
15 次 OCR 阶段调用；显式工具包括 capture 36、OCR 13、click 9、key 13、exec 20、
write 2，fill/type_text 均为零。可见失败含 arrival_target_changed、target_pixels_changed、
临时文件写入范围拒绝、子进程超时及语义快照失败。过程展示有省略，不能据此断言
所有失败原因、完整参数、当时注册的工具或实际运行二进制；未将加载旧版本当作已证实根因。
源摘要/去标识化工具计数见[数据](data/task-continuous-perception-20261023.json)，
不保存联系人、任务正文或脚本内容；没有重放真实消息发送。

## 接线及失败修复

- 首次已授权 native capture 成功后，包装原 sharing session；后台不另开授权，前台
  整个 capture/pointer/input/IME/OCR 步骤优先。取消/撤权/关闭停止并回收。
- 现有 Executor capture worker/timer 和 Mirador analysis worker 复用；后台 coordinator
  与显式 ROI/image 状态隔离。默认 OCR/轻量变化，图标语义仍显式按需。事件采用 Mirador
  稳定 ID、instance generation 和确认状态；单帧首见/像素区域变化分别明确未确认。
- 事件缓存 256 条/256 KiB 逻辑量，未读 120 秒、已读后 60 秒清理；ACK FIFO 容量 64，
  拒绝不假推进，深度/拒绝来自 kairo CommStats。目标焦点外纯更新过滤，关键生命周期
  与首见保留。预览最多 8 张/8 MiB，每张至多 640 边/1 MiB、最长 120 秒；消费后退休。
- 每轮自动附最多 16 个未读事件，已读摘要最多 8 个实体/1 KiB，总供给至多 8 KiB。
  pinned supplier 输入不进入历史，摘要补足跨轮关联。仅成功主响应且实际请求包含完整
  事件块才确认游标；内层拼音规划、失败或预算裁剪不假读。后台不调用 VLM、不编码 PNG。
- 成功消费的同一截图不重复供给；显式 capture 仍可要求再次供图。
  desktop.vision.event_frame 按需读历史预览，不能产生输入引用；淘汰/过期/已消费为
  available=false 的正常结果。旧图片不能替代新输入证据或完成核验。
- 文本任务优先当前暴露的 fill/type_text，捕获回执列出实际输入路线。确实不支持的
  文本可自主选择其他已授权路线；没有放开接管、释放或系统权限屏障。

实测前的失败均保留在数据中，没有合并进最终成功样本：

1. 初版漏传 detector.min_confidence，YOLO 使用零阈值，候选预算失败：18 次分析全部
   失败。短诊断重现 `proposal candidates exceed max_candidates`，按公开 API 传入模型
   阈值修复；没有扩大预算或修改 pinned 依赖。这不是依赖缺口。
2. 阈值修复但后台 OCR+YOLO 串行：18 次分析无失败，100/500/1500 ms 提示分别捕获
   1/6、2/6、6/6，未完成两帧出现确认。切换为默认 OCR/轻量变化、图标检测按需。
3. 静态筛查与 Refresh 强制识别冲突、相邻帧指纹对亮度变化提前判无变化，曾导致静态
   不退避或第二确认帧被跳过。后台独立筛查保持 Refresh 证据，变化后强制独立复查；
   显式 OCR 的 Refresh 不被跳过。
4. TSAN 并发提交压力抓到优先级检查读取正在被 settle 清理的 query。改为创建请求时
   固定的 const routing 标志。随后专项通过，并追加同一压力场景 5 次通过；未新增 suppression。

## 真实短提示

`tests/manual/continuous_perception_acceptance.py` 创建独立 Xvfb、D-Bus、GTK 窗口，
不操作用户显示。实际 Linux ScreenProvider → ContinuousCaptureHost → PpOcrPipeline
→ pinned PP-OCR/ContinuousPerception → 有限事件流。使用产品清单（含已配置 YOLO，
默认后台不执行它），真实采集和 OCR。私有 X11 session shim 不代表 Portal 授权验收。

同一轻量感知 acceptance 二进制三轮，每轮 18 个不同英文通知，交替位置，展示后空白
700 ms；每种时长各 6 个。每轮 fresh process，已读游标由 consumer 确认。下表检测
包含明确 unconfirmed 首见，出现确认需两个独立有效帧，二者不混称。

| 展示时长 | 捕获通知 | 确认出现 | 捕获后的投递延迟中位数 / 最大值 |
| --- | --- | --- | --- |
| 100 ms | 6/18（33.3%） | 0/18 | 334.7 / 385.2 ms |
| 500 ms | 18/18 | 14/18 | 394.0 / 586.6 ms |
| 1500 ms | 18/18 | 18/18 | 400.7 / 557.1 ms |

延迟从 GTK 请求显示到消费者读取事件测量，包含 OCR/轮询，非 compositor 时间戳；
不含漏检样本，不能把它当所有提示的延迟分布。100 ms 的样本说明不能保证任意短
事件必达，250 ms 尝试周期不是固定识别帧率。没有把“已捕获未确认”当生命周期成功。

三轮后台共采集 339 帧，实际分析 173、静态跳过 166，分析失败 0。事件逻辑峰值
16020～17452 字节；预览像素峰值均 7864320 字节（7.5 MiB），没有逐帧 PNG 或
后台 VLM。各进程峰值 RSS 529792～530820 KiB，含现有模型/ncnn 分配，不能用预览
8 MiB 预算冒充进程内存上限。此为约半分钟/轮的窗口，长时 RSS 与低端设备仍待验收。

该三轮测量的 acceptance SHA-256（随后补流实例/帧 ID 隔离，识别路径不变）：
`cc6b74b38f0aafbec81c0baf70740fc34050a9e439a583f2cc9e4877fda123a6`。
另三轮相同轻量策略的开发样本各 18 个及初始失败/慢方案均原样保留，未与最终表合并。

流实例/帧 ID 隔离、迟到暂停提交保护及显式 X11 launcher 完成后，再单独运行 18 个
提示：100/500/1500 ms 分别捕获 2/6、6/6、6/6，出现确认 0/6、5/6、6/6；
113 后台帧中分析 58、静态跳过 55、分析失败 0，预览峰值仍为 7.5 MiB。
此追加轮未并入上表，完整记录和中间补跑见数据；当前 acceptance 二进制与此轮一致：
`d7c38f919404544593c537443fc3a40787a7e91188ed1191dd085125b3dda7d8`。


复现（每条命令一轮；GTK 依赖使用系统 Python）：

```bash
cmake --build --preset native-release --target mirage-service mirage-native continuous_perception_acceptance -j2
/usr/bin/python3 tests/manual/continuous_perception_acceptance.py --binary build/native-release/tests/continuous_perception_acceptance --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json --output /tmp/mirage-continuous-acceptance.json
```

## 自动化验证

Debug 全量 113/113 通过（322.91 s），随后修复上述竞态和上下文摘要，追加对应专项。
最终 Debug、ASAN、UBSAN、TSAN 聚焦六个业务测试（加 setup/cleanup，8/8）通过；
摘要、短期限、流实例和已提交事件用例又在四个配置分别补跑通过；
最后 capability-honest 提示词调整另跑 Debug ModelLayer 回归通过。TSAN 使用本机要求的
`setarch $(uname -m) -R`；不增加 suppression。具体执行/日志摘要和二进制摘要见数据。

覆盖缓存容量/已读 60 秒/未读 120 秒/119 秒才读/弱帧释放/焦点过滤/源切换，正常/
异常/准入拒绝/运行中取消/短期限后恢复/shutdown，后台不抢前台、显式 ROI 不重置
背景身份、实际 detector 阈值与大帧、ACK 背压/CommStats、输入全步骤暂停，
已提交事件不被迟到的前台暂停丢弃（撤权/任务取消仍清理）。

完整 ModelLayer 脚本模型验证：推理过程中新增的事件自动进下一轮，已读摘要仍在，
不重放完整已读事件；1 次平台捕获、4 次模型请求、2 次供图（初始图+按需历史预览），
重复轮不附图片。淘汰帧变体只有 1 次供图并正常 available=false。该脚本没有执行
消息提交，不证明真实模型一定选择 fill 或实际业务任务完成率。

native-release service、native UI、acceptance 构建成功；公开头文件边界 67 个零违规，
改动格式/空白检查通过。原 IME Controller/ModelLayer 回归继续通过。

## 未关闭项

M9-21 保持 In Progress。负责人 Mirage 维护者；在对应环境/任务可用后补跑：真实
Wayland Portal/pet/多屏、Windows、在线模型实际打字选择与业务完成率、复杂首屏/
高频噪声的注意分配、低端设备长时 CPU/RSS。既有对象单帧内容变化、纯图标语义与
极短提示仍有边界；后续 ROI 增量与注意策略按 M9-07/18/19 推进。前台完整输入/OCR 暂停后台采样期间也有瞬态漏检边界；后续需考虑复用前台帧而
不破坏 capture/pointer/input 的原子步骤。本轮无依赖变更、
无安装/重启/真实联系人任务重放、无 commit/push。
