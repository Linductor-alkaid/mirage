# 原生前端 PR 栈 CI 验收与清理

> 状态：In Progress
> 日期：2026-10-07
> 负责人：Mirage 维护者 / Codex
> 工作项：M6-28；所属：[M6](m6-native-frontend.md)
> 依据：维护者明确授权修复 CI、合并相关 PR、清理工作分支及过期 build；沿用既有产品契约。

- [ ] 原生前端基础分支与 Mirage #67–72 同步主干，修复 Windows CRT/坐标转换及历史 native CI 失败；所有实际门禁通过后合并。
- [ ] Mira #81 同步当前 master（kairo），模型思考变更在该合并基础重新验收后合并。Mirage 继续固定已批准 pre-kairo 源码，不以 CI 收尾静默迁移并发设施。
- [ ] EUI #88 四项 CI 复核；当前账户仅有 pull 权限，无上游合并权限。保留其分支、pin 和 PR，由上游维护者合并。
- [ ] 记录各 PR 精确 head/检查结果/合并结果，删除已合并的本轮本地/远程分支，保留未合并依赖工作。
- [ ] 保留 desktop entry 指向的 native-release 和在用进程映射，删除确认过期的其他 build 与本轮临时构建；不删除历史、配置、凭据、未跟踪用户证据。

## 初步证据

Mirage #67–71 Windows MSVC full-tree 失败，最新日志明确为新增 getenv 的 C4996 /WX 和
UI 指针 double、条件整数字面量到 float 的 C4244。保留 warnings-as-errors，使用有所有权的
环境值和显式布局坐标转换，不关闭警告。#67 native configure 日志确认缺少 CURL_LIBRARY/CURL_INCLUDE_DIR；native job 补装 libcurl4-openssl-dev 和 Xvfb，保留原有配置门禁。
Mira #81 原基础全部 CI 成功；当前主干已经迁移 kairo，需在新合并 head 重新取证。
EUI #88 OpenGL/Vulkan × GLFW/SDL2 四项成功，仓库权限 push=false、maintain=false。
