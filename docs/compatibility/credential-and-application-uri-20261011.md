# 凭据持久保存与应用网址交付验收（M7-14 第三增量）

> 状态：本地实现与隔离验收；真实用户在线任务待复验
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 决策：[DEC-071](../decisions/DEC-071-credential-cleanup-and-application-uri.md)
> 工作树：feat/m7-conversation-desktop-tools，基于 c786df7；未提交/推送/安装

## 诊断与边界

用户报告重启后需要配置 Key、设置清理失败和最新任务未完成。最新记录的六次工具调用
均成功，两次焦点核验成功，模型请求也成功；会话缺少应用启动/网址工具，模型激活无关
窗口后要求用户手动打开浏览器。既有 DEC-070 拒绝恢复在此记录中没有再次失败。

只读 Secret Service metadata 确认活动服务引用有一个 unlocked 条目，另一保存服务的
引用不存在；默认 alias 指向 GNOME 临时 session 集合，持久 login 集合存在且已解锁。
活动 Key 的 item 路径确认属于临时 session；未读取或复制真实 Key。旧写入使用默认 alias，不能保证注销桌面或钥匙环重启后保存；
不能据此断言本次仅重启 Mirage 的具体丢失过程，也不能称供应商 Key 过期。

配置保存提交成功后清理不存在的旧条目被误报失败。现删除缺失条目幂等成功；缺失、
锁定、重复及服务不可用不同。新密钥遇到 GNOME 临时默认集合时写入已枚举的 login；
login 缺失/锁定拒绝，不降级到临时集合/文件。其他正常默认集合继续使用。已有临时 Key
没有自动迁移，用户在新版本设置中重新保存一次；本轮未改变真实配置、密钥或历史。

## 实现与证据

应用 Provider 可选 open_uri/supports_open_uri，Linux GIO 固定 desktop entry 接收一个
有 host、无 userinfo/控制符/空白、至多 2048 字节的 HTTP(S) 参数，不经过 shell。已运行
应用仍可接收 URI；交付子进程与普通启动共用 256 项资源登记及机会式回收。Provider
失败显式说明可能副作用，第三方错误不复制 URI。Windows/stub 不暴露 URI 能力。

会话 application.list v2 的独立 scope / 会话 / epoch / 五秒时效，授权后重验并获取既有
ActionCoordinator；能力为 application.launch。固定 application_running 后条件仅
证明新查询看到进程，平台交付成功不能证明网页或业务目标完成。后续重新 window.list、
激活真实目标并观察页面；缺少目标时不能激活无关窗口。派发后取消/异常/不确定结果仍
封锁重试。工具卡片持久输入移除 uri，保留 digest/字节数，卡片目标显示应用进程。

Python 外部测试 owner 管理独立 D-Bus，无自动激活目录，无真实钥匙环访问。Cpp
平台调用在 Executor 工作单元中执行，跨多个进程保存/读取，临时集合清空后再次读取，
重复删除、locked/missing/duplicate/service failure 负例均验。私有 desktop entries 和
独立 helper 验证已运行交付两次、URI 原样 argv（含 shell 元字符）、拒绝/取消/预算与
终止回收。真实 Runtime IPC 会话验证交付、重新列出新窗口、激活和权限拒绝，另有
canonical local verification、交错观察、epoch、取消、异常、不确定和 unsupported 负例。

## 验证结果

| 检查 | 最终结果 |
| --- | --- |
| Native Release 全量 | 71/71 |
| Debug 全量 | 65/65 |
| ASAN + UBSAN 定向 | 9/9，泄漏与错误即失败 |
| TSAN 定向 | 9/9，setarch x86_64 -R；系统 GLib/GIO 既有抑制；应用夹具不加载宿主模块 |
| Application Provider | 141 checks，0 failures |
| canonical 后条件 / Harness | 465 / 553 checks；包括 4 个真实 X11 任务及 Runtime IPC 应用路径 |
| 实际 Release / 新 DEB 解包产品 | 各 37/37；各 11 个 workflow、6 次物理点击、2 次 Runtime 正常退出 |
| 公共头边界 / 格式 / diff | 53 头零违规；本次 19 个 C++ 文件格式及原生标签局部格式通过；diff 通过 |


实际命令、源码与日志摘要、包摘要见 [机器证据](data/credential-and-application-uri-20261011/summary.json)。
Release 启用 native/PP-OCR/ncnn/bundled vision；Debug 为原有构建；sanitizer 为现有
ocr-asan / ocr-tsan。每套 CTest 包括两个用户状态隔离 fixture，测试目录独立且回收。

首次全量的 schema 金样仍为 8 个工具，新增两个应用工具造成 action_contract_test
失败。同步实际 required 字段与 10 个注册工具后复验；没有放宽 schema 校验。
首次 TSAN 在宿主 libgvfscommon.so 报告 unlock mutex；选择 local VFS 后仍加载模块。
最后夹具在首次 GIO 调用前设置独立空 GIO_MODULE_DIR、清除 GIO_EXTRA_MODULES，并
保留 local VFS 与 halt_on_error=1。该隔离方式依据 [GIO 官方运行文档](https://gnome.pages.gitlab.gnome.org/gtk/gio/overview.html)。
未增加 GVFS 抑制，也不宣称系统 GVFS 的 sanitizer 验证通过；生产环境变量未改动。
TSAN 中止留下的一个本轮私有 helper 已按唯一 fixture 路径回收，没有终止用户应用。

实际 Release 产品离线 workflow 37 项回归；本地 DEB 用解包后的真实二进制、固定资源
与包内字体验证同一矩阵。第一次包夹具字体链接误指 share/mirage/fonts，实际资产在
usr/bin/assets；修正测试布局后重跑，未更改包或用工作树字体替代包资产。

## 使用与剩余验收

从托盘退出 Mirage 后，原桌面入口启动已重建的 native-release。设置中重新保存一次
当前服务的 Key，写入持久 login；留空保存仅保留旧引用，不能迁移旧临时 Key。随后
新建会话复跑原浏览器任务。无需重新配置 OCR/ncnn 运行环境；随包固定资源保持。

真实用户浏览器、网页加载、选视频及供应商在线执行尚未复跑；由 Mirage 维护者在新版
运行后验收。Windows 新应用会话路径/URI、其他 Secret Service 和 GVFS 完整 sanitizer
保持未验，不以 Linux fixture 证明。M7-14、M7-11 整体维持 In Progress；提交/评审未闭合。
