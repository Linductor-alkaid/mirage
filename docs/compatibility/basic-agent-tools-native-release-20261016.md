# 基础工具 native-release 重建与本机产品更新

> 日期：2026-10-16
> 负责人：Mirage 维护者
> 工作项：M10-09（开放项复查）、M10-11（部分：操作说明与 Linux 安装回归）
> 状态：构建、目标回归与隔离真实产品 E2E 通过；Windows 部分仍开放
> 源提交：`3b0aabd`（产物与探针均由该源构建；测试脚本与文档改动不影响二进制）

维护者要求以当前 M10 分支更新本机产品供人工体验。本轮从既有
`build/native-release` 增量重建；源码为 `3b0aabd`，未修改 pinned 依赖
（mira `a29374a`、mirador `ae8cf87`、eui `89c2d0f` 与 lock 均未动）。
构建前确认无运行中的 Mirage 产品进程；桌面入口
`org.mirage.native.desktop` 指向 `build/native-release/` 固定路径，二进制
原位替换即完成产品更新，无需重新注册。

## 构建和产物

```bash
cmake --preset native-release -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build --preset native-release --target mirage-native-product -j 6
cmake --build --preset native-release --target m10_basic_tools_e2e -j 6
```

两条 build 均 exit 0。六个可执行文件存在、可执行且 `ldd` 无缺失依赖：

| 路径（相对 build/native-release） | SHA-256 |
| --- | --- |
| `apps/mirage` | `c592bac95945875f9965e6877ea56d98b5a7d601ba0fac340a0c32439e385e67` |
| `apps/mirage-service` | `393620aee48cf506b04f322392e6f6c4776b826cbaf341a360a96c339ea4c73a` |
| `apps/tray/mirage-tray` | `8a0e84402888c2adb21912ce6a2b2bdf1982162a0ecc53504738bc6fe772f2c5` |
| `apps/native/mirage-native` | `d32dd6b972088591a0a519c72973261f0e3b46ca32e90e4a6c53fb1a98db0a78` |
| `apps/pet/mira_pet` | `c2c4e4ebce46da1c6f3fdef2e6f3ba7d9573998fb27d69a99991f9b7703ed7be` |
| `tests/m10_basic_tools_e2e` | `217fe70de07171ee041bb67c10894bafd2d879eb02d93482b306e6b4dc112f59` |

`mira_pet` 与 2026-10-15 记录逐字节一致（M10 未触及宠物面，符合预期）。
构建日志：`/tmp/m10-native-release-{configure,build,probe-build}.log`。

## 回归与真实产品 E2E

```bash
ctest --test-dir build/native-release -R '^(conversation_budget_test|
  native_chat_model_test|native_agent_integration_test|pet_animation_test|
  pet_resource_loader_test|pet_frames_test|pet_frame_presentation_test|
  tray_runtime_test)$' --output-on-failure
BUILD_DIR=build/native-release tests/manual/m10_basic_tools_e2e_run.sh
```

- 目标回归集 **10/10 通过**（工作流/桌面工具/M9 历史压缩/宠物/tray 面），
  日志 `/tmp/m10-native-release-tests.log`。
- 真实产品 E2E 三场景（deepseek 经默认设置接入，隔离 socket/state，
  产品旗标 `--enable-basic-tools --enable-exec --workspace/--write-root/
  --read-root --perm …=allow --confirm deny`）在 native-release 二进制上
  **全部通过**：四工具链逐字节断言、exec 运行中 IPC 取消无残留进程、
  turn 间外部修改后 stale 检测与恢复。日志 `/tmp/m10-native-release-e2e2.log`。

## 本轮发现并修复的测试隔离缺陷

首轮 E2E 场景三报"会话名额已满（24）"。根因是**驱动缺陷而非产品缺陷**：
`--state-dir` 只迁移任务恢复文件（M1-07），M5-08 会话状态存储始终位于
`default_state_directory()`，于是 E2E 水合并回写了用户真实
`~/.local/state/mirage/session-state.json`。量化确认：24 个会话中 23 个为
M10-09 以来各轮 E2E 累积的测试残留（工作区路径均含 `/tmp/m10-e2e.*`），
真实会话仅 1 个；容量 25（DEC-021）被测试残留挤满。

处理：驱动改为对服务进程注入 `XDG_STATE_HOME=$WORK/state` 隔离会话存储与
basic-output 留存目录（`tests/manual/m10_basic_tools_e2e_run.sh`）；用户真实
状态文件清理为仅存真实会话，清理前备份于
`/tmp/session-state.backup-20261016.json`，测试期间校验和保持不变。
复跑三场景全通过且真实文件逐字节未动。

## 范围与未执行项

- Windows/MSVC 安装回归未执行（M10-10 前置，环境无 Windows）。
- native-release 未跑全量 ctest 与消毒器（沿用 2026-10-15 记录口径：
  消毒器在 debug/asan/ubsan/tsan preset 上已有 M10 全量证据）。
- 操作说明已交付：`docs/examples/basic-agent-tools.md`（启用、权限、预算、
  错误恢复、留存清理、崩溃语义、验证步骤）。

## 追加（同日）：产品体验暴露的配置装配缺陷与修复

本机产品人工体验发现模型报"没有基础工具"。逐层定位到两个缺陷并当日修复：

1. `apps/service/host.cpp`：设置应用存在 `--config` 与默认文件两条手写分支，
   M10-08 只在 `--config` 分支应用了 `basic_tools` 块；产品托盘恰走默认分支
   （读 `~/.config/mirage/service.json`），工具被静默丢弃——同文件内的
   read_roots/permission 规则却生效，形成"读根有了、工具没有"的表象。
   修复：默认分支补齐同语义应用块；并在启动输出新增
   `basic tools: …` 状态行，使配置分叉无法再无声发生。
2. `runtime/persistence/src/settings.cpp`：`confirmation` 字段拒绝 `"ipc"`，
   持久化配置无法启用 DEC-020 确认枢纽。修复：decode 接受
   `"allow"/"deny"/"ipc"`。

伴随发现：原生 UI 未实现 `permission.respond` 应答面（M5-03 枢纽无产品消费者），
产品里 `confirm` 规则恒 fail-closed。未在本次强行接 UI（应答面属 UI 增量，
按规范另行立项）；用户配置以 allow 规则运行，写域由 write_root 锁定工作区。

修复后验证（本机产品实例，托盘托管 Runtime，非隔离状态）：设置文件启用
`basic_tools`（workspace=~/桌面/mirage-workspace）重启后，对产品 IPC 端点
发起真实模型 turn，模型实际调用 read 工具逐字读出工作区文件（tool part
complete、版本指纹与分页窗口正确），turn ok——基础工具在产品内可达。
设置文件修复前备份 `/tmp/service.json.backup-20261016`。
