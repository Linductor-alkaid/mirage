# Native Release 更新验证（2026-10-26）

> 状态：Completed（Linux amd64 本机构建、全量测试与 DEB 校验交付）
> 负责人：Mirage 维护者
> 工作项：M7-35、M9-21 本机构建交付跟进；功能和真机门禁保持原状态
> 依据：DEC-037、DEC-068、DEC-097、DEC-114、DEC-115
> 源码：master `877984435302df67a648acf221e6fbf742165b79`

维护者要求将本地 native-release 更新到工作分支集成后的 master，包含 Mira
`68a8bf1` 及注册拒绝诊断。本轮只重建和交付现有产品，不更改产品代码、依赖 pin、
模型、配置默认值或既有平台验收结论。

## 环境与方法

Ubuntu 24.04.5 LTS / Linux x86_64，GCC 13.3.0 / CMake 3.28.3 / Ninja。
保留 native frontend、pet preview、PP-OCR、YOLO、bundled vision 和 packaging，
YOLO 使用既有离线模型目录并按锁文件校验。

构建前工作树干净；626 项产品、测试、CMake 和打包文件的 SHA-256 清单、
依赖提交及环境信息保存在 `out/native-package-20261026/`。交付前重新验证
源码摘要；该 Git 外目录同时保留构建、测试、打包日志和解包核对结果。

```bash
cmake --preset native-release -DMIRAGE_ENABLE_PACKAGING=ON \
  -DMIRAGE_WITH_YOLO=ON \
  -DMIRAGE_YOLO_MODEL_DIR=/home/linductor/mirage-yolo-conversion/model-dir
cmake --build --preset native-release -j4
cmake --build --preset native-release --target mirage-native-product -j4
LP_NUM_THREADS=2 ctest --preset native-release -j2 \
  --output-junit /home/linductor/mirage/out/native-package-20261026/ctest.xml
cpack --config build/native-release/CPackConfig.cmake -G DEB \
  -B /home/linductor/mirage/out/native-package-20261026
python3 out/native-package-20261026/verify_package.py
python3 out/native-package-20261026/deliver_package.py
```

## 验收与交付

- [x] 完整构建和 `mirage-native-product` 成功。
- [x] native-release 全量 CTest 成功，无失败或跳过。
- [x] DEB 中产品程序、UI/视觉资源、OCR/YOLO 锁文件和许可证逐项校验。
- [x] 动态库、控制脚本、desktop entry 和隔离 CLI 冒烟通过。
- [x] 备份旧包、原子替换交付包并校验 SHA-256。

本轮不安装系统包或重启用户产品会话。真实 IME/Portal/Windows/在线任务、
长时可靠性与完整模型 sanitizer 门禁继续保留；负责人 Mirage 维护者，按各功能
计划规定的目标环境补验。本轮产物不能关闭 M7-35 或 M9-21。

## 验证结果

完整构建与产品聚合目标通过，包含 launcher、service、tray、native 和本地 pet。
native-release 全量 **123/123 通过，0 failed、0 skipped，147.93 s**。
本轮没有修改或禁用测试，也未复现 `BUG-20261026-001` 的注册拒绝；该问题根因
仍按[分支集成记录](../plans/branch-integration-20261026.md)保持待定位。

新 DEB 的 4 个产品 ELF、18 项 UI 资源、17 项视觉资源与构建源逐字节匹配；
OCR 五项与 YOLO 两项模型文件的大小/摘要、manifest/lock、来源和完整许可证
均核对通过。动态库无缺失，控制脚本语法、desktop entry、图标摘要和隔离
XDG 目录下 CLI `--version` 冒烟通过。

产品源码与测试/打包文件的规范 JSON 摘要为
`269c98b195f02070d168915fe2bc0737679beda5a241ae59fe10dd669bfd4bae`；交付前 626 项文件摘要再次核对，
没有产品源码在构建期间变动。源码从干净 master `8779844` 构建，只有本轮
验收文档在构建期间新增；依赖分别为 Mira `68a8bf1`、Mirador `2a9f483`、
EUI `19acab6`。完整提交、程序/资源摘要和条件见
[机器可读记录](data/native-package-refresh-20261026.json)。

交付包为 `out/packages/mirage_0.1.0_amd64.deb`，126,628,396 字节，
SHA-256：`012d60e944b89b92dba9936e14fb89286f0c9b982f19a927301af77ae733d79a`。
相邻 `.deb.sha256` 校验成功。旧包 `6373615d252f7bfd43203dd04be121490717e851acbb8a195e2f752c8f724621`
保存在 `out/native-package-20261026/previous-6373615d252f.deb`。
源码 PR #121 的 ASAN/UBSAN/TSAN CI 已成功；本轮未重新运行完整 OCR/YOLO
sanitizer 矩阵，不能将普通 Release 结果作为其完成证明。
