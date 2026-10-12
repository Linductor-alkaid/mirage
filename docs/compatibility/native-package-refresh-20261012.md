# 最新修复 Native Release 安装包（2026-10-12）

> 状态：Completed（Linux amd64 打包与产物校验）；用户安装及原会话实测待验收
> 负责人：Mirage 维护者
> 工作项：M6-04 / M6-22 / M6-39 / M6-40 交付跟进
> 依据：DEC-031、DEC-037、DEC-040、DEC-043、DEC-051

维护者要求将最近修复重新打包。构建基于 `adfe65b` 与工作树中已有的 DeepSeek
模型列表、工具名往返映射修复，包含最新网络阶段超时恢复、会话桌面能力及独立图标。
版本保持 0.1.0，交付路径为 `out/packages/mirage_0.1.0_amd64.deb`，大小
55,292,900 字节，SHA-256 为
`3746fe1b2935adfbcdb5a76714d62893804d6e249cc83792f398417614833790`。

本轮发现 Linux 安装清单缺少原生界面实际加载的 Phosphor 字体和厂商图标，已在
`packaging/linux/packaging-deb.cmake` 补入 `Phosphor-Regular.ttf`、MIT 许可证及
`assets/providers/`。未修改 pinned 依赖或其他业务代码。

验证环境为 Ubuntu 24.04.5 LTS / x86_64 / GCC 13.3.0，Release，PP-OCR、固定
视觉资源和 PipeWire 启用。准确命令：

```bash
cmake --preset native-release -DMIRAGE_ENABLE_PACKAGING=ON
cmake --build --preset native-release -j 4
ctest --test-dir build/native-release --output-on-failure
cpack --config build/native-release/CPackConfig.cmake -B build/native-release/update-package-20261012
dpkg-deb --extract build/native-release/update-package-20261012/mirage_0.1.0_amd64.deb build/native-release/update-package-20261012/verify
dpkg-deb --control build/native-release/update-package-20261012/mirage_0.1.0_amd64.deb build/native-release/update-package-20261012/control
sh -n build/native-release/update-package-20261012/control/postinst
sh -n build/native-release/update-package-20261012/control/prerm
python3 build/native-release/update-package-20261012/verify_package.py
```

Native Release 全量 76/76 通过，耗时 220.90 秒，无 skip。四个包内 ELF 与对应
Release 程序 SHA-256 一致，`ldd` 无缺失依赖，包内 CLI `--version` 成功。
17 项界面资源逐字节匹配源码/构建资源；五项 OCR 资源的大小和 SHA-256 与 manifest
及资源锁一致。控制字段为 `mirage / 0.1.0 / amd64`，postinst/prerm 语法通过。
构建至解包校验之间的自研源码与构建配置指纹仅有本轮安装清单预期变化。

校验后原子替换交付 DEB，并生成相邻 `.sha256` 文件。上一包保存在忽略目录
`build/native-release/update-package-20261012/previous-df90dbab9577.deb`。
产物、程序、资源与验证日志摘要见 [summary.json](data/native-package-refresh-20261012/summary.json)。

本轮未安装、启动或重启用户产品，未调用在线模型，未提交或推送。完整安装/升级/卸载、
真实桌面像素和原 DeepSeek 会话测试仍由 Mirage 维护者在安装新版后补跑；Windows
安装包需目标平台构建和运行取证。M6-04、M6-39、M6-40 及 M7 开放门禁不因此关闭。

## 2026-10-12：本机程序与资源直接更新

维护者随后明确要求直接更新本机。检查发现本机未安装 Mirage DEB，用户桌面入口指向
本仓库 `build/native-release`，且无 Mirage 产品进程运行。执行
`cmake --build --preset native-release --target mirage mirage-service mirage-tray mirage-native mirage-native-register-desktop -j 4`
成功，资源重新同步并刷新 `~/.local/share/applications/org.mirage.native.desktop`。
四程序与 17 项界面资源的 SHA-256 均与本次验证包一致；入口内容与生成文件一致，
`desktop-file-validate` 通过，CLI `--version` 成功。用户下次从应用菜单打开 Mirage
即使用最新构建。没有安装系统 DEB、启动产品或发起在线模型请求；原会话实测继续待验。
