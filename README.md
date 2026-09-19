# Mirage

[![ci](https://github.com/Linductor-alkaid/mirage/actions/workflows/ci.yml/badge.svg)](https://github.com/Linductor-alkaid/mirage/actions/workflows/ci.yml)

Mirage 是基于 [Mira](https://github.com/Linductor-alkaid/mira) 构建的 Linux / Windows
桌面端产品：为 Mira Agent Harness 提供完整的 PC 运行环境、桌面交互能力与产品界面，并
以 [Mirador](https://github.com/Linductor-alkaid/mirador) 作为视觉基础设施处理桌面
截图与局部图像的 OCR、目标检测、几何结构与视觉 Cache。

设计文档：[docs/design/Mirage：Linux - Windows 桌面端设计方案.md](docs/design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md)。

## 仓库结构

```text
mirage/
├── apps/          # 可执行入口：cli、service（已有）、desktop / tray（M5）
├── runtime/       # Mira Host、Runtime Service、IPC、持久化、权限
├── desktop/       # 跨平台 Desktop Environment（Provider 接口与 Observation）
├── platform/      # Linux（AT-SPI2 / X11 / Wayland）与 Windows（UIA / Win32）后端
├── integration/   # pinned mira / mirador / mcp 的适配边界
├── ui/            # 桌面产品界面（M5）
├── tests/         # 测试（自研轻量 harness，ctest 标签驱动）
├── third_party/   # pinned 依赖：mira、mirador（git submodule）
└── docs/          # 设计、决策、计划、规范与验证记录
```

依赖方向固定为 `apps/ui -> runtime -> desktop -> platform`，`integration` 是 pinned
依赖的唯一边界层。规则详见根 [AGENTS.md](AGENTS.md) 与
[docs/project/project-standards.md](docs/project/project-standards.md)。

## 构建

依赖以 pinned submodule + `dependencies.lock.json` 锁定，configure 时自动校验
commit（[DEC-001](docs/decisions/DEC-001-dependency-pinning.md)）：

```bash
git submodule update --init --recursive   # 或依赖 configure 时的自动同步
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/apps/mirage --version
```

可用预设：`debug`、`release`、`asan`、`ubsan`、`tsan`（sanitizer 覆盖源码树内构建的
依赖目标）。注意：内核 ASLR 熵较高时 TSan 可能报 `unexpected memory mapping`，用
`cd build/tsan && setarch $(uname -m) -R ctest` 运行即可（见
[M1 验证记录](docs/plans/m1-mira-host.md)）。离线/CI 只校验模式：
`cmake --preset debug -DMIRAGE_FETCH_DEPENDENCIES=OFF`。

X11 Backend 测试（M2-02 起）需要本机有 Xvfb：`sudo apt install xvfb`，或无 root
时将发行包提取到用户前缀并导出 `MIRAGE_XVFB=<路径>`（定位顺序
`$MIRAGE_XVFB` → `$PATH` → `~/.local/mirage-sysroot/usr/bin/Xvfb`；缺失时
`x11_backend_test` 响亮失败而非跳过，[DEC-015](docs/decisions/DEC-015-linux-backend-dependencies-and-event-loop.md)）。

开发机注意事项：本机系统 libX11 的 `X_SetSelectionOwner` 请求字段序与 X 协议不符
（owner/selection 互换；字节捕获与反汇编取证见 [M2 计划](docs/plans/m2-desktop-environment.md)
M2-04 验证记录），selection 协议路径（M2-04 起的剪贴板测试）在本机需以协议正确序
interposition 运行（诊断 shim 仅存于本机 `/tmp`，不入库）；健康系统与 CI 不需要
任何 shim，直接 `ctest` 即可。

## 运行 Runtime Service 与 CLI（M1）

后台 Runtime Service 经 Local IPC 服务于 CLI / GUI（[DEC-007](docs/decisions/DEC-007-local-ipc-and-runtime-service.md)）：

```bash
mkdir -p /tmp/mirage-demo && printf hello-mirage > /tmp/mirage-demo/note.txt
./build/debug/apps/mirage service start \
    --read-root /tmp/mirage-demo           # 拉起 mirage-service 并等待就绪
./build/debug/apps/mirage service status
TASK=$(./build/debug/apps/mirage task submit \
    --goal "read a file and run a shell command" \
    --read /tmp/mirage-demo/note.txt --exec "printf hello-mirage" | cut -d' ' -f2)
./build/debug/apps/mirage task list
./build/debug/apps/mirage task inspect "$TASK"
./build/debug/apps/mirage task cancel "$TASK"   # 运行中任务可随时取消
./build/debug/apps/mirage service shutdown
```

M1 任务由 Service 内的宿主侧驱动循环按提交的有序 steps（`--read` / `--exec`）确定性
推进。文件读取强制 `--read-root` 读范围（未声明时拒绝一切读取，越界/symlink 逃逸
fail closed），命令执行有长度/时长/输出预算并支持协作取消；范围、预算与取消语义见
[DEC-009](docs/decisions/DEC-009-provider-scope-budget-cancellation.md)。

Desktop Permission 判定（`RULE-05`，[DEC-010](docs/decisions/DEC-010-m1-permission-framework.md)）
在每一步动作副作用前生效，默认策略为读取与执行放行、写入拒绝。可用
`service start --perm CAPABILITY=allow|confirm|deny`（可重复）调整，配
`--confirm allow|deny` 选择确认挂点结果（默认拒绝，确认 UI 属 M5）：

```bash
./build/debug/apps/mirage service start \
    --read-root /tmp/mirage-demo \
    --perm process.execute=confirm --confirm allow
```

被拒绝的步以 `permission_denied` 结算并在 `task inspect` 中显示
`perm=denied` / `perm=confirmation_rejected`；放行的步显示 `perm=allowed` /
`perm=confirmed`。M1 拓扑仍限开发与测试用途
（[DEC-008](docs/decisions/DEC-008-m1-environment-binding-and-reference-providers.md)）。

### 本地配置与任务恢复状态（M1-07）

Mirage 自有状态经 `runtime/persistence` 落盘
（[DEC-011](docs/decisions/DEC-011-m1-local-state-persistence.md)，schema v1
JSON + 原子写入）：

- **任务恢复状态**：`task-recovery.json`，默认在
  `$XDG_STATE_HOME/mirage/`。任务结算与停机时写入快照，服务重启后历史任务
  仍可 `task list` / `task inspect` 观察（对已恢复任务 `task cancel` 以
  `invalid_state` 拒绝）。`service start --state-dir DIR` 换目录，
  `--no-recovery` 关闭。
- **本地配置**：`service.json`，默认路径 `$XDG_CONFIG_HOME/mirage/`（M1 仅
  显式加载）。`service start --config PATH` 以文件为基线，命令行旗标逐项
  覆盖；文件损坏时启动失败（fail closed），恢复文件损坏时降级运行并告警。

持续集成见 [.github/workflows/ci.yml](.github/workflows/ci.yml)：格式与公共头
边界检查 + `debug` / `release` / `asan` / `ubsan` / `tsan` 预设矩阵（TSan 在
runner 上以 `setarch -R` 关闭 ASLR 运行），依赖以 verify-only 模式在
configure 阶段校验锁定。

## 开发流程

计划与进度见 [docs/plans/mirage-implementation-plan.md](docs/plans/mirage-implementation-plan.md)，
架构与产品默认值决策见 [docs/decisions/](docs/decisions/)，依赖（mira / mirador）问题反馈见
[docs/dependency_feedback/ledger.md](docs/dependency_feedback/ledger.md)。提交与 MR 规范、
验证证据要求见 [docs/project/project-standards.md](docs/project/project-standards.md)。
