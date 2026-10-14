# M8-11：本机 native-release 与对话气泡推进

> 状态：In Progress
> 日期：2026-10-14
> 负责人：Mirage维护者
> 授权：更新本机native-release、继续对话气泡，并明确授权合并Mira侧更新。
> 基础证据：[实时气泡](mira-pet-bubbles-runtime-20261014.md)、[DEC-089](../decisions/DEC-089-mira-pet-bubbles.md)。

## 依赖和概括修正

Mira PR#94已授权squash合并到22d95776d701c92656ecd583f85982d5b0d303fe。
合并前head ea720a6的24项checks全部通过，两个源码树无差异；gitlink与lock已同步。
不合并Mirage PR#90/#91，不改变其他任务的工作树或依赖pin。

对话气泡从完整句/行抽取核心短句，流式半句保持提示或最近可靠短句；规范终态允许
无标点短回复。补充英文终止标点/逗号/大小写、列表标题、没有/尚未/未来计划和英文
not/never/unverified等限定。保留负号，42/OK等简短规范回答不丢失。
前16KiB有界扫描，预算切点不当作完整结论；不增加摘要模型或并发路径。
新增语义回归包括半句逐步闭合、旧开场与新核心、条件/否定/代码、数字/负数和终态覆盖。

## Release构建与本机交付方式

实际快捷方式Exec指向 /home/linductor/mirage/build/native-release/apps 下的launcher、
tray和native frontend。主工作树有其他在途修改，因此从气泡独立worktree编译完整Release，
只发布五个产品可执行文件、native/pet资源和已验证vision bundle到该快捷方式使用的路径。
原有桌面入口保持，复制前备份产品文件，文件逐个stage+原子replace，并写SHA-256交付receipt。
主工作树源文件、Git分支和CMakeCache不替换；重建源为气泡worktree的native-release。

构建命令：cmake --preset native-release -DMIRAGE_ENABLE_PET_PREVIEW=ON
-DMIRAGE_VISION_SOURCE_DIR=/tmp/mirage-ppocrv5-source-20261009；再构建mirage、mirage-service、
mirage-tray、mirage-native、mira_pet及专项测试。1216个初始构建步骤成功。
Release专项6/6通过（golden、bubble、Runtime集成、native view及setup/cleanup）。
日志：/tmp/mirage-bubbles-release-{configure,build,final-build,test}.log。
最终交付、在线结果及sanitizer记录完成后追加，不预填通过。

## 在线验收范围

新增可选tests/manual/pet_bubbles_online_acceptance.py：一次公开算术请求、真实MiniMax
Messages、私有Xvfb/DBus/用户目录，GIO启动与快捷方式相同的生成desktop入口。
密钥仅在子进程环境供生产SecretRef解析，不写日志/报告/参数；不使用用户历史或桌面动作。
核验实时思考→IPC→native pet、文字视口变化、最终短句、5秒消失、规范历史和关闭。
JSON只写计数/合成短句；截图仅包含公开算术内容的私有窗口。真实compositor alpha/
物理DPI/多屏和M8-06整组避让仍不由Xvfb结果证明，负责人/补跑条件沿基础证据记录。
