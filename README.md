# Mirage

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

## 运行 Runtime Service 与 CLI（M1）

后台 Runtime Service 经 Local IPC 服务于 CLI / GUI（[DEC-007](docs/decisions/DEC-007-local-ipc-and-runtime-service.md)）：

```bash
./build/debug/apps/mirage service start                 # 拉起 mirage-service 并等待就绪
./build/debug/apps/mirage service status
TASK=$(./build/debug/apps/mirage task submit \
    --goal "read a file and run a shell command" \
    --read /etc/hostname --exec "printf hello-mirage" | cut -d' ' -f2)
./build/debug/apps/mirage task list
./build/debug/apps/mirage task inspect "$TASK"
./build/debug/apps/mirage service shutdown
```

M1 任务由 Service 内的宿主侧驱动循环按提交的有序 steps（`--read` / `--exec`）确定性
推进；Provider 收紧前仅限开发与测试拓扑（[DEC-008](docs/decisions/DEC-008-m1-environment-binding-and-reference-providers.md)）。

## 开发流程

计划与进度见 [docs/plans/mirage-implementation-plan.md](docs/plans/mirage-implementation-plan.md)，
架构与产品默认值决策见 [docs/decisions/](docs/decisions/)，Executor 能力缺口反馈见
[docs/executor_feedback/ledger.md](docs/executor_feedback/ledger.md)。提交与 MR 规范、
验证证据要求见 [docs/project/project-standards.md](docs/project/project-standards.md)。
