# DEC-031：Windows 安装器生成器定案（NSIS）

> 状态：Accepted
> 日期：2026-09-30
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-11` 落地）
> 替代/被替代：无（落实 [DEC-006](DEC-006-ui-web-frontend-packaging.md) 决策 4
> 遗留的"NSIS / WiX-exe 等具体生成器在 M5 里程碑计划确定"）

## 背景与问题

[DEC-006](DEC-006-ui-web-frontend-packaging.md) 决策 4 冻结了分发形态
（Linux `.deb` / Windows `exe` 安装包），把安装器生成器留给 M5 确定，候选
NSIS / WiX-exe。约束面：

1. 产物是 `exe` 安装包（非 MSI）；无 MSIX/商店分发需求（DEC-006 已定）。
2. 双工具链纪律（[DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md)）：
   打包工具链必须可进入 CI/发布流水线，不绑定单一交互机器。
3. 发布机隔离（shell-binary-locking 既定）：签名证书/AUMID 密钥类资产不入
   仓库（[DEC-018](DEC-018-windows-notification-carrier.md) AUMID 载体、
   Authenticode 证书）。
4. M5 交付物边界：本决策只定安装器生成器与载荷；更新清单验签脚本与演练
   属 M5-11 交付（DEC-006 决策 5），应用内更新器本体属 `M5-12`。

## 决策

1. **生成器 = NSIS**（`.nsi` 脚本入 `packaging/windows/mirage.nsi`，构建经
   `packaging/windows/build-installer.ps1`，fail closed：makensis 缺席/载荷
   缺文件即中止）。
2. **载荷 = 四产品进程 + （可选）桌面壳与 CEF payload**：mirage.exe /
   mirage-service.exe / mirage-tray.exe 恒打包；mirage-desktop.exe 与 CEF
   运行时在释放树携带（`MIRAGE_WITH_DESKTOP=1`）时打包。
3. **AUMID 载体 = 注册表 AppUserModelId 键**（HKCU
   `Software\Classes\AppUserModelId\Mirage.Desktop`，DisplayName），快捷方式
   自身的 System.AppUserModel.ID 属性需要 NSIS ApplicationID 插件（发布
   机），运行时亦可按进程设置 AUMID——边界如实声明（[DEC-018](DEC-018-windows-notification-carrier.md)
   触发条件的"安装器可交付 AUMID 载体"由此成立）。
4. **卸载器 + ARP 注册**（Uninstall 键 / DisplayName / DisplayVersion /
   UninstallString）。

## 备选方案

- **WiX / WiX-exe（MSI）**：否决。WiX 工具链绑定 .NET Framework/Windows
  （CI 需额外的 Windows 构建跳板），MSI 的特性（每机安装、组件竞态、升级
  编号管理）超出当前需要；WiX-exe（WiX 生成 bootstrapper exe）仍依赖 WiX
  工具链。NSIS 单二进制 makensis 可跨平台（Linux/CI 可构建并校验脚本），
  与本仓库"脚本化、可审计、最小依赖"的发布工程取向一致。
- **CPack NSIS**：否决为独立选项（CPack NSIS 仍依赖 makensis，且模板可控
  性差——AUMID 注册表/自定义卸载逻辑需要手写 .nsi，CPack 模板反而绕远）；
  `mirage.nsi` 为手写脚本，构建由 PowerShell 包装。

## 影响与风险

- NSIS 工具链（makensis）在发布/CI 机器需安装（choco install nsis）；
  Linux 开发机无 makensis——安装器构建是 Windows 发布机/CI 步骤，本地
  Linux 仅产出四进程二进制（载荷清单由 build-installer.ps1 fail closed
  校验）。
- 快捷方式 AUMID 属性的插件依赖留发布机（AppUserModelId 注册表键已由本
  安装器写入；DEC-018 触发条件的 AUMID 载体半边成立）。
- Authenticode 证书/私钥只在发布机 secret store（脚本 fail closed：环境
  变量缺失即退出；证书不入仓库）。

## 验证方式

- Linux 侧：`.deb` 由 CPack DEB 产出并经 dpkg-deb 检查（control 字段、
  shlibdeps 依赖、postinst/prerm 语法）；apt 仓库 + GPG 签名/验签与更新
  清单演练本地全过（M5-11 验证记录）。
- Windows 侧：makensis 构建 + 安装/升级/卸载 + Authenticode 签名在维护者
  Windows 机器/CI windows 作业取证（M5-11 验证记录补跑条件；DEC-017 证据
  分级）。

## 关联文档和工作项

- [DEC-006](DEC-006-ui-web-frontend-packaging.md)（决策 4/5）、
  [DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md)（双工具
  链）、[DEC-018](DEC-018-windows-notification-carrier.md)（AUMID 载体与
  toast 触发条件）。
- 工作项：[M5 计划](../plans/m5-desktop-product.md) `M5-11`。
