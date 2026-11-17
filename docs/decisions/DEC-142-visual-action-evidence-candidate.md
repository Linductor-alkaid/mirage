# DEC-142：动作前后视觉证据的 Episode 候选

> 状态：Accepted（M15-04/06 的适配器增量，产品默认关闭）
> 日期：2026-11-12
> 负责人：Mirage 维护者
> 工作项：M15-13；M15-04/06 的整体生命周期仍未冻结

## 背景

DEC-132 时间账只有当前 turn 的供图元数据；Mira 的 WorkflowTrajectory 不含原始截图。
不能从旧 Run 补造前后图。已有 IArtifactStore、IMemory、SqliteMemoryStore 可承载正文、
作用域、TTL 与擦除，但产品还没有统一的长期视觉存储 owner/全局预算。先交付宿主显式
调用的候选适配器与隔离存储验收，再接自动采集与产品生命周期。

## 决策

1. integration 层编码同一任务、操作、授权代次和环境代次的两张宿主提供的 BGRA8 图。
   必须有非 nil session/task/operation/provenance event、task epoch、应用/页面描述、
   recorded_at 与 expires_at；只声明观察到这些字节，不声明操作成功或跨页面可复用。
   OperationContext 必须匹配原始 owner；取消/过期拒绝，前后时刻有序且在调用方新鲜度
   窗口内。没有历史重建、模型调用、捕获、输入或后台工作。
2. 一个自含工件包含 8 字节 little-endian manifest 长度、canonical JSON manifest 和
   两段紧密 BGRA8 像素。逐行剔除 padding，避免保存未使用字节。manifest 记录版本、
   来源、应用/页面文字、尺寸、偏移、各段 digest、观察间隔、有效期与敏感度；没有可执行坐标、
   @e/@v、registry scope、窗口句柄或授权 token。source_sequence 不作为持久引用。
   工件整体 digest 同时覆盖元数据与像素；所有解析采用闭集并拒绝未知版本/尾随数据。
3. 适配器只准备字节和 MemoryRecord，不拥有或创建 Store，不调用 apply、写磁盘或做
   回收。宿主将字节交给自己已有的 ArtifactStore，再经适配器核对实际内容后形成 Task
   scope 的 Observed Episode。statement 只含内容摘要；敏感应用/页面/像素都在单一
   evidence 工件中，Sensitivity 必须为 Sensitive，manifest 与工件标签一致且受摘要
   保护。Mira 的 MemoryRecord 禁止 Secret，适配器在准备/读取阶段拒绝其进入，绝不降级；
   宿主仍负责内容分类，本适配器没有秘密检测器。不新增 MemoryKind、记忆库或图片索引。
4. 读取记忆必须再次校验 record 的 scope、状态、有效期、敏感度、statement 与 evidence
   digest、工件实际 digest、manifest/帧身份。跨 scope、缺失/擦除/篡改/过期、导入来源
   或验证等级提升都拒绝；不把只剩文字的记录降格为可用视觉证据。返回图像仅供历史
   审阅，不带行动引用，不能绕过现有新观察/定位/授权门禁。
5. 调用方必须显式给出单帧/总工件字节、最大前后间隔与新鲜度；约束在既有 8 MiB
   capture/帧、16 MiB 工件、8 KiB manifest、120 秒证据窗口的操作上界内。拒绝零、
   越界、溢出或不完整输入，不静默截断。它们不是产品总量/保留期预算；产品启用仍须
   M15-04/06 与 DEC-119 的全局、主体、任务、并发峰值及物理占用验收。
6. 一个 Episode 只引用一个完整工件，不产生嵌套图片引用。外部 owner 使用 Mira 的
   apply/get/query/erase/close，保留共享引用与失败 Pending 语义；适配器不尝试失败时
   私自 erase 可能被共享的工件。准备成功到实际接纳之间的孤儿/重启回收由未来 owner
   负责，尚未装配到 Runtime Service。测试仅在独立临时目录中显式写入/删除。
7. 无新生产并发。适配器为有界同步编码/读取；未来磁盘接线必须经现有 Executor worker。
   测试的 SQLite worker 由唯一测试 Executor 管理，显式 close 后 owner shutdown，沿
   pinned kairo-integration by-api / blocking-io 指南，不创建私有线程或队列。

## 验证

受控小图验证 padding 剔除、确定性、来源/时刻/权限代次、取消/期限、预算/溢出、严格
解码、摘要/内容/记录篡改与敏感度拒绝。实际 FileArtifactStore + SqliteMemoryStore
写入后关闭重开，Task scope 查询/按 ID 读取、历史图还原、跨 scope 不可见、显式擦除
后原图/记录不可再用；总量拒绝来自 Mira Store。Debug/ASAN/UBSAN/TSAN、格式与公共
头检查记录在 M15；真实桌面自动采集、磁盘故障/断电、共享证据隐私删除和产品收益另验。

## 关联

[M15](../plans/m15-supervised-rpa-experience.md)、
[专项设计](../design/supervised-rpa-experience-and-visual-memory.md)、
[DEC-119](DEC-119-bounded-memory-and-session-retention.md)、
[Mira ArtifactStore](../../third_party/mira/include/mira/artifact_store.hpp)、
[Mira Memory](../../third_party/mira/include/mira/memory_contracts.hpp)。
