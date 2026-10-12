# DEC-071：凭据清理幂等与会话应用网址入口

> 状态：Accepted
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 冻结里程碑：M7
> 工作项：M7-14 第三增量
> 替代/被替代：无（补充 DEC-042、DEC-068、DEC-070）
> 依据：维护者授权修复重启凭据提示、保存清理误报及最新浏览器任务失败

## 背景与问题

最新会话模型请求和桌面激活成功，但会话没有应用启动/网址入口，模型激活了无关窗口
后要求用户手动打开浏览器。活动服务凭据仍有 unlocked 条目；另一个保存的引用不存在。
设置保存提交后清理不存在的旧条目被误报失败。只读 metadata 进一步确认系统默认 alias
指向 GNOME Keyring 临时 session 集合，而持久 login 集合存在且 unlocked。旧写入
使用默认 alias，不能保证注销桌面/钥匙环重启后持久化；不等同于单独重启 Mirage 必然丢失。不能由此断言 API Key 被远端拒绝或
确定上一次启动丢失凭据的原因，不迁移或覆盖用户配置、钥匙环与历史。

## 决策

Secret Service 搜索明确区分唯一可读、不存在、锁定、歧义及服务失败；删除不存在的
凭据是成功，锁定/歧义/服务失败保持失败。读取缺失和锁定有不同提示；模型通用错误
说明读取失败，不能称密钥失效。保存与清理仍遵循 DEC-042 先提交后清理。GNOME 默认集合为已知临时 session 路径
时，新密钥保存到服务枚举出的 login 集合；其不存在/锁定则明确失败，不降级到临时集合
或文件。不自动改动已有密钥，旧 session 密钥需维护者在新版本设置中重新保存一次。
其他 Secret Service 正常持久默认集合继续使用；不能宣称未知实现的持久性。

会话增加 application.launch / application.open_uri，通过现有 Application Provider、
application.launch 权限及 ActionCoordinator 执行。URI 仅允许有 host、无 userinfo 的
HTTP/HTTPS，最多 2048 字节，无控制符或空白；固定已安装 desktop entry 接收一个 URI，
不接收任意命令。Linux GIO 承载平台映射，其他后端默认不暴露 open_uri。启动入口仍
拒绝重复实例；网址交付允许运行中的应用，所有交付子进程有容量限制并按既有方式回收。

应用列表单独颁发会话 scope / epoch / 五秒时效，授权后再次检查并获取同一动作 lease。
应用动作固定后条件 application_running：仅证明新查询发现进程及平台接受交付，不证明
网页加载或业务目标完成。后续必须新窗口列表、截图/语义或 OCR 验证网页；不得用无关
窗口替代目标应用。失败沿用 DEC-070 有界回执；派发后取消、异常或不确定结果继续封锁。
URI 不复制到产品持久历史，动作目标以摘要留证。

复用 pinned Mira canonical 工具注册与验证链，无新增并发 owner、线程或调度器。
平台方法为既有 Executor 工作单元中的有界同步工作。Mira/Mirador 无需修改。

## 备选方案和影响

任意 shell 启动扩大权限且绕过动作验证；自动重试不确定启动可能产生重复窗口；仅增加
提示不能解决缺少工具。选择 Provider 扩展、明确局部后条件及真实平台交付测试。
Linux /proc 运行状态沿用现有启发式，不能当作窗口归属或页面身份的权威证明。

## 验证方式

独立 D-Bus fixture 验证凭据跨进程读取、重复删除、缺失/锁定/重复项；不触碰用户密钥。
私有 desktop entry 验证 URI 原样 argv、已运行交付、非法 URI 拒绝与回收。会话验证
新 scope、权限拒绝、取消、epoch、局部后条件及不确定封锁，运行相关 sanitizer。
真实用户在线任务由维护者在重启新版本后补跑，不以离线 fixture 声称完成。

## 关联文档和工作项

[M7 计划](../plans/m7-visual-tools-and-input.md)、
[桌面动作边界](../security/desktop-actions.md)、
[第三增量验收](../compatibility/credential-and-application-uri-20261011.md)。
