# Native Release 更新验证（2026-10-27）

> 状态：Completed（所列工作区快照的 Linux 构建、全量测试和 DEB 交付）
> 负责人：Mirage 维护者
> 工作项：M9-13/20/21、M7-35 本机构建交付跟进
> 依据：DEC-091/109/114/115；当前功能分支的 DEC-116
> 源码：`feat/continuous-semantic-binding`，`f42d878` 加本轮源码清单记录的工作区改动

维护者要求继续更新 native-release，并纳入仓库后续工作的现有实现：连续对象语义
绑定、模型标定与版本失效，以及桌面共享瞬时失败恢复、观察与输入恢复隔离。
功能分支及其未提交改动保留，构建源通过文件清单与摘要追溯；本记录不把这些工作
宣称为已合入 master，也不关闭原有功能、真机、在线模型或长期资源门禁。

## 源码与配置

Linux amd64 / Ubuntu 24.04.5 LTS，GCC 13.3.0、CMake 3.28.3、Release / Ninja。
保留 native frontend、pet preview、PP-OCR、YOLO、bundled vision 和 packaging。
依赖仍为 Mira `68a8bf1`、Mirador `2a9f483`、EUI `19acab6`，没有升级依赖或模型。
628 项产品、测试与构建文件的 SHA-256 清单，工作区改动和环境快照，均保存在
`out/native-package-20261027/`；交付前必须复核源码未在构建期间变动。

初始构建误启动了重叠进程，已中止并清理全部中间产物；不将该轮算作通过。
现有代码一处格式检查失败，经修改文件的 clang-format 归一化后全仓格式检查通过，
随后进行单一构建进程的完整 clean build。初始日志和源码摘要仍保留用于追溯。

## 命令

```bash
cmake --build --preset native-release --target clean
cmake --preset native-release -DMIRAGE_ENABLE_PACKAGING=ON \
  -DMIRAGE_WITH_YOLO=ON \
  -DMIRAGE_YOLO_MODEL_DIR=/home/linductor/mirage-yolo-conversion/model-dir
cmake --build --preset native-release -j4
cmake --build --preset native-release --target mirage-native-product -j4
LP_NUM_THREADS=2 ctest --preset native-release -j2 \
  --output-junit /home/linductor/mirage/out/native-package-20261027/ctest.xml
cpack --config build/native-release/CPackConfig.cmake -G DEB \
  -B /home/linductor/mirage/out/native-package-20261027
python3 out/native-package-20261027/verify_package.py
python3 out/native-package-20261027/deliver_package.py
```

## 验收与交付

- [x] 完整 native-release 与产品聚合目标构建通过。
- [x] 当前工作区的全量 CTest 通过，失败和跳过均为零。
- [x] DEB 的程序、UI/视觉资源、模型 manifest/lock 和许可证逐项匹配。
- [x] 动态库、控制脚本、desktop entry 和隔离 CLI 冒烟通过。
- [x] 源码清单复核、旧包备份、交付包原子替换和 SHA-256 校验通过。

本轮不安装系统包或重启产品会话；负责人 Mirage 维护者，真实 Portal/Windows、
在线模型标定质量及长期可靠性仍按原功能计划在目标环境补验。本轮 Release
结果不替代完整模型 sanitizer、在线或真机证据。

## 最终结果

完整 native-release 和 `mirage-native-product` 构建成功，包含 launcher、service、
tray、native 与本地 pet；全量 CTest **124/124 通过，0 failed、0 skipped，148.64 s**，
其中新增 `perception_semantics_test` 确实执行并成功。没有禁用测试或放宽期限。

DEB 的 4 个产品 ELF、18 项 UI 资源和 17 项视觉资源逐字节匹配构建源；
OCR 五项、YOLO 两项模型文件的大小/SHA-256、manifest/lock、来源及许可证均通过。
动态库无缺失，控制脚本语法、desktop entry、图标和隔离 CLI `--version` 冒烟通过。

构建前后 628 项源码和文件清单完全一致，依赖提交也再次复核。规范 JSON 摘要为
`e6e34db81ff6cf5180613bb1930fa685c40d5ef385a26de29b1d47eaa1a77303`。
当前未提交工作的完整构建源和相关工作区文档另存于
`out/native-package-20261027/source-overlay.tar.gz`，639 个文件，
29,590,836 字节，SHA-256
`f04e2a87bd2b7802447b37d20e6a678701f44f3b4a34dfb894b796a6a76cd9ae`；归档内全部 628 项构建源再次逐项核对。
结合基线完整提交 `f42d878774383f8f2991c9b4066110a78c7ee126`、依赖 pin 与既有模型锁即可追溯本次源码；
不能仅以基线 HEAD 当作含新增功能的已发布源码。

[结构化记录](data/native-package-refresh-20261027.json)包含环境、工作区状态、
依赖、构建产物/资源摘要、全量测试及源码归档信息。原始 stdout、JUnit、
解包目录和校验脚本保存在上述 Git 外证据目录，编译产物和源归档不进入 Git。

交付包：`out/packages/mirage_0.1.0_amd64.deb`，126,759,464 字节，
SHA-256 `ec2de8713b7708698a528be4c8c9352605e3e46185006676173bf4580c420bf2`。
相邻 `.deb.sha256` 已通过 `sha256sum -c`。原 2026-10-26 包备份于
`out/native-package-20261027/previous-012d60e944b8.deb`；
本轮包包含连续对象语义绑定与桌面共享瞬时失败恢复的现有实现。

验收记录通过独立文档 PR 收录；该 PR 的 CI 针对记录及其 master 基线，功能源码
仍由 `feat/continuous-semantic-binding` 工作区及其后续发布流程管理。本次本机
124 项结果不替代功能源码发布后的跨平台 CI，也不关闭 M9-13/20/21 或 M7-35。
