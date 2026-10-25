# Native Release 更新验证（2026-10-24）

> 状态：Completed（Linux amd64 本机构建、回归、DEB 校验与交付）
> 负责人：Mirage 维护者
> 工作项：M7-35、M9-21 本机构建交付跟进；两项功能仍为 In Progress
> 依据：DEC-037、DEC-068、DEC-097、DEC-114、DEC-115

按维护者要求更新当前 `native-release` 与 DEB，包含已有 ROI OCR 输入工具、
任务连续事件及按需历史帧的工作树改动。未修改产品实现、依赖指针、模型锁或配置默认值。
构建发现一处旧测试假设，已修正，过程见下文。

## 环境与源码

Ubuntu 24.04.5 LTS，Linux 7.0.0-38-generic，amd64；
GCC 13.3.0，CMake 3.28.3，Release / Ninja。
源码为 `2af42c51b1f8f8804cf6ae156dc2822d885b5bae` 上的未提交工作树，分支
`feat/keyboard-ime-state`；不能以 HEAD 单独复现当前产物。625 项产品、测试、构建与
打包源文件的摘要清单保留在 `out/native-package-20261024/source-hashes.json`，
其规范 JSON SHA-256 为 `32807f3b48d108fd61e8d509733dbb46886bc293c722c5abd3d7350f07575ac9`。
交付前重新核对该清单，源码未在构建期间变动。工作树状态、依赖提交、环境及程序
摘要见[机器可读记录](data/native-package-refresh-20261024.json)。

保留 native frontend、pet preview、PP-OCR、YOLO、bundled vision 与 packaging；
打包配置验证 PipeWire SDK 可用。YOLO 使用既有离线模型目录，并与锁定摘要核对。

## 可复现命令

```bash
cmake --preset native-release -DMIRAGE_ENABLE_PACKAGING=ON \
  -DMIRAGE_WITH_YOLO=ON \
  -DMIRAGE_YOLO_MODEL_DIR=/home/linductor/mirage-yolo-conversion/model-dir
cmake --build --preset native-release -j4
cmake --build --preset native-release --target mirage-native-product -j4
LP_NUM_THREADS=2 ctest --preset native-release -j2 \
  --output-junit /home/linductor/mirage/out/native-package-20261024/ctest.xml
cpack --config build/native-release/CPackConfig.cmake -G DEB \
  -B /home/linductor/mirage/out/native-package-20261024
python3 out/native-package-20261024/verify_package.py
python3 out/native-package-20261024/deliver_package.py
```

原始 stdout/stderr、JUnit、解包目录与验证/交付脚本保留在上述 Git 外目录。

## 结果与失败处理

完整构建及 `mirage-native-product` 聚合目标通过，包含 launcher、service、tray、
native、pet。最终 Release CTest **123/123 通过**，0 失败、0 跳过，
149.63 s。

首次全量为 122/123：`ppocr_pipeline_test` 仍断言原生会话恰好采集一次，与 DEC-115
启用后的后台采样不符；单独重跑也复现。检查 shutdown 的 blocking worker stop/join
及会话释放路径后，仅修正测试：至少一次采集，同时严格要求只创建一个会话、关闭一次。
修正后该业务测试连续 10 轮通过，随后重新完整构建与全量回归通过；没有放宽期限、
添加 suppression 或修改生产行为。首次全量、首次复现与重复通过日志分别为
`ctest-initial.log`、`ppocr-before.log`、`ppocr-after.log`，最终为 `ctest.log` / `ctest.xml`。

DEB 解包后的 4 个产品 ELF、18 项 UI 资源、17 项视觉资源逐字节匹配构建源；
动态库依赖无缺失。5 项 OCR 文件和 2 项 YOLO 文件的大小/SHA-256 匹配 manifest
及锁文件；YOLO 来源、完整 AGPL-3.0 许可证和锁文件一致。控制脚本 `sh -n`、
desktop entry 校验、图标摘要和隔离 XDG 目录下 CLI `--version` 冒烟通过。
本次测试修正只影响测试程序，打包的产品 ELF 未改变。

## 交付与边界

交付包：`out/packages/mirage_0.1.0_amd64.deb`，126,622,334 字节，SHA-256：
`6373615d252f7bfd43203dd04be121490717e851acbb8a195e2f752c8f724621`。
相邻 `.deb.sha256` 经 `sha256sum -c` 验证通过；旧包备份在
`out/native-package-20261024/previous-fc928ac34471.deb`。

本轮仅更新构建与交付包，未安装到系统、启动或重启用户产品会话。没有新增并发实现，
未另跑 sanitizer；最近功能验证与 sanitizer 证据见
[ROI 输入验收](roi-ime-fill-20261023.md)及
[连续感知验收](task-continuous-perception-20261023.md)。
Windows、真实 Wayland 输入、在线模型长任务、安装升级及低端设备资源未补验；
负责人为 Mirage 维护者，取得目标环境及验收条件后按 M7-35/M9-21 原计划补跑。
本次包更新不关闭上述功能门禁，也不改变既有成功率与极短提示漏检结论。
