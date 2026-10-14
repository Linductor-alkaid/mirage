# M8-11：本机 native-release 与对话气泡推进

> 状态：Completed（本机部署与MiniMax Messages公开算术范围）
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


## 最终交付与验收

产品源commit为88abbd7（功能commit 803ecf2，Mira pin 22d9577）；五个可执行文件、
native/pet assets及vision bundle已复制到本机快捷方式使用的native-release路径，
校验receipt位于 /home/linductor/mirage/build/native-release/bubble-release-receipt.json。
旧产品文件在同目录.bubble-backup-*中保留；用户配置/凭据/会话及主工作树不变。
新打开桌面入口即可运行更新，无需修改入口或手动切换Git分支。

最终Debug全量80/80（并行44.8秒），Release专项6/6；ASAN/UBSAN/TSAN各4/4（bubble与Runtime集成+隔离夹具），
ASAN detect_leaks=0、TSAN关闭ASLR。Public headers 58个0违规及全仓format通过。
前一个已交付head 6ce42d1的Mirage CI八项全部通过，包括Windows和native EUI；
本增量补推后CI以当前checks为准，不继承旧head的通过结论。

本机已部署路径由GIO启动与安装入口等价的生成desktop文件；真实MiniMax-M3 Messages
单轮公开算术端到端确认10/10：启动、模型/bubble能力、规范结算前思考预览、实际文字
视口变化、核心短句、原生UI定时消失（先拍图，再查状态）、服务状态过期、规范历史、
无raw preview字段和产品shutdown。最终短句“计算结果是42”，remaining_ms=5000；
观察到17个thinking快照、3个model_preview、4个dialog、1个answer_summary。
没有传入用户历史、截图或桌面工具，两个请求均为公开算术（初验+一次确认）。

第一次在线请求已取得真实思考/对话截图，但RGBA差异图的相同alpha通道令getbbox返回
None，导致movement断言失败；不是生产滚动缺失。改为比较RGB文本视口后一次确认通过。
初验日志 /tmp/mirage-bubbles-live-release.log 和报告保留，不改写成通过；最终结果为
/tmp/mirage-bubbles-live-release-confirm/online-results.json。DBus/GVfs的私有session清理
警告不影响各项明确结果，未据此声明真实桌面compositor验收。

物理DPI/多屏、真实compositor alpha、其他在线供应商和M8-06整组避让仍保持开放；
本记录Completed只指本次授权的部署与Messages在线增量，不关闭整个M8-11/M8。
