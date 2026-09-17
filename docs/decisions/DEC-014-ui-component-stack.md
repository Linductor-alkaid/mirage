# DEC-014：UI 组件栈与前端素材定选

> 状态：Accepted
> 日期：2026-09-18
> 负责人：Mirage 维护者
> 冻结里程碑：随 `M1.5-07`「任务控制台」harness 前端重写落地并冻结；后续升级走本记录修订
> 替代/被替代：细化 [DEC-006](DEC-006-ui-web-frontend-packaging.md) 决策 6 中"组件框架按实现评审定选"一项

## 背景与问题

`M1.5-08` 交付时视图层为 vanilla TS，组件框架按计划留待"实现评审时定选"。`M1.5-07`
统一壳升级为完整 harness 前端（会话线程流、工具调用卡、思维链、审批流、工作流编辑、
八类设置、命令面板），纯 vanilla 路线需要自研全部 headless 交互层（焦点/键盘/无障碍/
虚拟化/thread/composer 状态机），长期成本显著高于引入框架；而 agent chat 组件生态
（assistant-ui、cmdk、AI Elements 等）压倒性绑定 React。

## 决策

1. **框架层：React 19**（经现有 Vite，无 meta-framework）。CEF 桌面语境下无
   SSR/SEO/首屏网络约束，react + react-dom 的体积代价（~45KB min+gzip）可忽略。
   不引入 Solid/Vue 路线：headless 与 chat 生态不可用，等于用框架切换换回自研负担。
2. **基础组件基座：Base UI（`@base-ui/react`）**：无头可访问原语，MUI 团队维护、
   2026-07 起 shadcn/ui 新项目默认基座；样式 100% 走自有三层 token（CSS 变量），
   不引入 CSS-in-JS。
3. **素材获取纪律："copy-in + token 重写"**：Vercel AI Elements / prompt-kit /
   Origin UI 仅作素材源按需抄写；抄入代码样式一律重写为 `var(--mir-*)` 语义 token。
   不引入成品视觉库（daisyUI/HeroUI/Ant Design）以免主题层冲突。
4. **内容渲染**：markdown-it + DOMPurify（消息体）；Shiki（代码高亮，CSS 变量主题
   映射多套主题，按语言子集注册）；工具调用卡/审批卡/快照卡不走 markdown 通道。
5. **图标：lucide-react**（ISC，24px 细线条，按需 tree-shake）；**字体：
   @fontsource-variable 本地打包**（Saira Variable wdth 轴 = 仪表 caps；
   Chivo Mono = 遥测/代码；正文 CJK 走系统栈），禁止网络字体。
6. **动效：CSS 原生为主**（transition/@keyframes/@layer，token 见
   `ui/app/src/theme/primitives.ts` 的 `--mir-dur-*`/`--mir-lamp-*`）+
   `motion` 按需引入；`prefers-reduced-motion` 全降级为测试锁定项。
7. **assistant-ui 暂不引入**：其 runtime 抽象假设 chat-completion 流式协议，与
   Mirage 的事件投影（DEC-012）模型不匹配且 0.x API 波动；线程层以自有
   事件投影 store 实现（与 opencode/DeepSeek harness 的共同架构决策一致）。
   若后续协议面演化出消息流事件集，可重新评估。

## 影响与风险

- 前端依赖进入 `package-lock.json` 锁定与 SBOM 范围（DEC-006 决策 6）。
- React 19 + CEF：需 CEF Chromium ≥ 130（当前构建线满足）；View Transitions/
  `@starting-style` 仅作渐进增强，统一走 CSS 降级。
- `ui/app/src` 现为 React 视图层；`ui/contracts` 保持零运行时依赖、框架无关。

## 验证方式

- `ui` vitest 全量（含 contracts golden vectors、theme 门槛、style-scan 私定颜色
  清零）通过；`npm run check`（tsc）与 `vite build` 通过。
- 浏览器 mock 全流程验收截图存 `.impeccable/review/`。

## 关联文档和工作项

- [DEC-006](DEC-006-ui-web-frontend-packaging.md)（UI 技术路线与分发形态）
- [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)（事件订阅语义）
- [DEC-013](DEC-013-frontend-ia-harness-first.md)（harness 优先信息架构）
- `M1.5-07`（[M1.5 计划](../plans/m1.5-ui-parallel-track.md)）
- 实现事实源：[DESIGN.md](../../DESIGN.md)、`.impeccable/design.json`
