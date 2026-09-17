/// 设置页（八类，顺序固定，设计规范 §3.4）+ 资源页（M2+ 占位）。
/// 外观类接入真实主题系统（ThemeManager）；其余七类为 mock 先行的表单
/// 骨架（IPC 面为设计规范 §4 前瞻依赖），界面以「模拟」标注。

import { useMemo, useState } from 'react';
import {
    Boxes,
    Brain,
    KeyRound,
    Languages,
    MonitorCog,
    Palette,
    Plug,
    ServerCog,
    Sparkles,
} from 'lucide-react';

import { useHarness } from '../hooks.js';
import { hostStatusLabel } from '../lib/labels.js';
import { SETTINGS_CATEGORIES } from '../state/store.js';
import { themeManager } from '../theme/service.js';
import { BUILT_IN_THEMES } from '../theme/themes.js';
import type { ResolvedThemeMode } from '../theme/schema.js';
import type { ThemeModePref } from '../theme/schema.js';

const NAV_ITEMS: readonly { id: string; label: string; icon: React.ReactNode }[] = [
    { id: 'general', label: '常规', icon: <Languages size={15} /> },
    { id: 'appearance', label: '外观', icon: <Palette size={15} /> },
    { id: 'models', label: '模型', icon: <Sparkles size={15} /> },
    { id: 'memory', label: '记忆', icon: <Brain size={15} /> },
    { id: 'skills', label: '技能', icon: <Plug size={15} /> },
    { id: 'mcp', label: 'MCP', icon: <ServerCog size={15} /> },
    { id: 'permissions', label: '权限', icon: <KeyRound size={15} /> },
    { id: 'runtime', label: '运行时', icon: <MonitorCog size={15} /> },
];

function SimTag(): React.ReactElement {
    return (
        <span className="sim-note" title="该设置面尚无 IPC 契约（设计规范 §4 前瞻依赖），当前为 mock 先行骨架">
            模拟
        </span>
    );
}

function AppearanceSettings(): React.ReactElement {
    const appearance = themeManager.get();
    const [, force] = useState(0);
    const setTheme = (id: string): void => {
        themeManager.setTheme(id);
        force((n) => n + 1);
    };
    const setMode = (mode: ThemeModePref): void => {
        themeManager.setMode(mode);
        force((n) => n + 1);
    };
    const themes = BUILT_IN_THEMES;
    const previewMode: ResolvedThemeMode = themeManager.resolvedMode();
    return (
        <section className="settings-body" aria-label="外观">
            <h1>外观</h1>
            <p className="lead">主题库（{themes.length} 套内置）、明暗三态。切换即时生效并持久化。</p>
            <div className="card">
                <div className="card-title-row">
                    <h2>主题库</h2>
                    <span className="muted" style={{ fontSize: 12 }}>
                        当前：{themes.find((t) => t.id === appearance.themeId)?.name}
                    </span>
                </div>
                <div className="theme-grid" data-testid="theme-grid">
                    {themes.map((t) => {
                        // 卡片预览：以该主题当前模式的语义值着色（内联展示属允许例外）。
                        const v = t[previewMode];
                        const active = appearance.themeId === t.id;
                        return (
                            <button
                                type="button"
                                key={t.id}
                                className={`theme-card ${active ? 'is-active' : ''}`}
                                onClick={() => setTheme(t.id)}
                                data-testid={`theme-${t.id}`}
                            >
                                <span className="theme-swatch">
                                    <i style={{ background: v.background }} />
                                    <i style={{ background: v.card }} />
                                    <i style={{ background: v.primary }} />
                                    <i style={{ background: v['evidence-highlight'] }} />
                                </span>
                                <span className="t-name">
                                    {t.name}
                                    {active && <span className="badge is-success">使用中</span>}
                                </span>
                            </button>
                        );
                    })}
                </div>
            </div>
            <div className="card">
                <h2>明暗模式</h2>
                <div className="composer-modes" role="tablist" aria-label="明暗模式">
                    {(
                        [
                            ['light', '浅色'],
                            ['dark', '深色'],
                            ['system', '跟随系统'],
                        ] as const
                    ).map(([value, label]) => (
                        <button
                            key={value}
                            type="button"
                            role="tab"
                            aria-selected={appearance.mode === value}
                            className={`mode-btn ${appearance.mode === value ? 'is-active' : ''}`}
                            onClick={() => setMode(value)}
                        >
                            {label}
                        </button>
                    ))}
                </div>
            </div>
        </section>
    );
}

function GeneralSettings(): React.ReactElement {
    return (
        <section className="settings-body" aria-label="常规">
            <h1>常规</h1>
            <p className="lead">语言、开机自启、托盘与通知行为。 <SimTag /></p>
            <div className="card">
                <div className="form-grid">
                    <div className="form-row">
                        <label htmlFor="set-lang">界面语言</label>
                        <select id="set-lang" className="select" defaultValue="zh-CN" disabled>
                            <option value="zh-CN">简体中文</option>
                        </select>
                    </div>
                    <div className="form-row">
                        <label htmlFor="set-autostart">开机自启</label>
                        <select id="set-autostart" className="select" defaultValue="off">
                            <option value="off">关闭</option>
                            <option value="on">启动后最小化到托盘</option>
                        </select>
                    </div>
                    <div className="form-row">
                        <label htmlFor="set-tray">关闭窗口行为</label>
                        <select id="set-tray" className="select" defaultValue="tray">
                            <option value="tray">最小化到托盘</option>
                            <option value="quit">退出应用</option>
                        </select>
                    </div>
                    <div className="form-row">
                        <label htmlFor="set-log">日志级别</label>
                        <select id="set-log" className="select" defaultValue="info">
                            <option value="debug">debug</option>
                            <option value="info">info</option>
                            <option value="warn">warn</option>
                            <option value="error">error</option>
                        </select>
                    </div>
                </div>
            </div>
        </section>
    );
}

function ModelsSettings(): React.ReactElement {
    return (
        <section className="settings-body" aria-label="模型">
            <h1>模型</h1>
            <p className="lead">ModelProfile：提供方、端点、默认/回退模型与预算限额。 <SimTag /></p>
            <div className="card">
                <table className="table">
                    <thead>
                        <tr>
                            <th>名称</th>
                            <th>提供方</th>
                            <th>角色</th>
                            <th>上下文</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr>
                            <td>主力 · glm-4.7</td>
                            <td className="mono">openai 兼容</td>
                            <td><span className="badge is-success">默认</span></td>
                            <td className="mono">200k</td>
                        </tr>
                        <tr>
                            <td>回退 · 本地 qwen</td>
                            <td className="mono">localhost:8080</td>
                            <td><span className="badge is-muted">回退</span></td>
                            <td className="mono">128k</td>
                        </tr>
                    </tbody>
                </table>
                <p className="muted" style={{ fontSize: 12, marginTop: 8 }}>
                    密钥仅存于系统凭据库，界面不回显。管理面依赖 mira ModelProfile 经 IPC 暴露（前瞻依赖）。
                </p>
            </div>
        </section>
    );
}

function MemorySettings(): React.ReactElement {
    return (
        <section className="settings-body" aria-label="记忆">
            <h1>记忆</h1>
            <p className="lead">作用域（Task / Session / 全局）与整理策略。 <SimTag /></p>
            <div className="card">
                <div className="form-grid">
                    <div className="form-row">
                        <label>全局记忆</label>
                        <span>3 个条目 · 最近整理：1 天前</span>
                    </div>
                    <div className="form-row">
                        <label>会话记忆</label>
                        <span>随会话保留，删除会话时一并清除</span>
                    </div>
                    <div className="form-row">
                        <label>整理策略</label>
                        <select className="select" defaultValue="manual" style={{ maxWidth: 220 }}>
                            <option value="manual">手动整理</option>
                            <option value="auto">空闲时自动整理</option>
                        </select>
                    </div>
                </div>
            </div>
        </section>
    );
}

function SkillsSettings(): React.ReactElement {
    return (
        <section className="settings-body" aria-label="技能">
            <h1>技能</h1>
            <p className="lead">已装技能、来源与启用状态。 <SimTag /></p>
            <div className="card">
                <table className="table">
                    <thead>
                        <tr>
                            <th>技能</th>
                            <th>来源</th>
                            <th>状态</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr>
                            <td>executor-integration</td>
                            <td className="mono">mira (pinned)</td>
                            <td><span className="badge is-success">启用</span></td>
                        </tr>
                        <tr>
                            <td>desktop-observation</td>
                            <td className="mono">内置</td>
                            <td><span className="badge is-success">启用</span></td>
                        </tr>
                        <tr>
                            <td>workflow-authoring</td>
                            <td className="mono">插槽 P5</td>
                            <td><span className="badge is-muted">未安装</span></td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </section>
    );
}

function McpSettings(): React.ReactElement {
    const [tools, setTools] = useState<Record<string, 'allow' | 'ask' | 'deny'>>({
        'fs.read': 'allow',
        'shell.run': 'ask',
        'web.fetch': 'ask',
    });
    return (
        <section className="settings-body" aria-label="MCP">
            <h1>MCP</h1>
            <p className="lead">服务器连接与工具级权限。 <SimTag /></p>
            <div className="card">
                <h2>服务器</h2>
                <table className="table">
                    <tbody>
                        <tr>
                            <td className="mono">local-dev</td>
                            <td className="mono muted">stdio</td>
                            <td><span className="badge is-success">已连接</span></td>
                        </tr>
                    </tbody>
                </table>
            </div>
            <div className="card">
                <h2>工具权限</h2>
                <table className="matrix">
                    <thead>
                        <tr>
                            <th>工具</th>
                            <th>策略</th>
                        </tr>
                    </thead>
                    <tbody>
                        {Object.entries(tools).map(([tool, policy]) => (
                            <tr key={tool}>
                                <td className="mono">{tool}</td>
                                <td>
                                    <select
                                        className="select"
                                        value={policy}
                                        aria-label={`${tool} 权限`}
                                        onChange={(e) =>
                                            setTools({ ...tools, [tool]: e.target.value as 'allow' | 'ask' | 'deny' })
                                        }
                                    >
                                        <option value="allow">allow</option>
                                        <option value="ask">ask</option>
                                        <option value="deny">deny</option>
                                    </select>
                                </td>
                            </tr>
                        ))}
                    </tbody>
                </table>
            </div>
        </section>
    );
}

const PROVIDERS = ['Application', 'Window', 'Accessibility', 'Screen', 'Input', 'Clipboard', 'Filesystem', 'Process', 'Notification'] as const;

function PermissionsSettings(): React.ReactElement {
    const [matrix, setMatrix] = useState<Record<string, 'allow' | 'ask' | 'deny'>>(
        Object.fromEntries(PROVIDERS.map((p) => [p, p === 'Input' || p === 'Process' ? 'ask' : 'allow'])),
    );
    return (
        <section className="settings-body" aria-label="权限">
            <h1>权限</h1>
            <p className="lead">
                桌面权限策略矩阵（Provider × 默认模式，DEC-010）。运行中的 ask 请求进入批准中心。 <SimTag />
            </p>
            <div className="card">
                <table className="matrix" data-testid="permission-matrix">
                    <thead>
                        <tr>
                            <th>Provider</th>
                            <th>默认模式</th>
                        </tr>
                    </thead>
                    <tbody>
                        {PROVIDERS.map((p) => (
                            <tr key={p}>
                                <td>{p}</td>
                                <td>
                                    <select
                                        className="select"
                                        value={matrix[p]}
                                        aria-label={`${p} 默认权限`}
                                        onChange={(e) =>
                                            setMatrix({ ...matrix, [p]: e.target.value as 'allow' | 'ask' | 'deny' })
                                        }
                                    >
                                        <option value="allow">allow</option>
                                        <option value="ask">ask</option>
                                        <option value="deny">deny</option>
                                    </select>
                                </td>
                            </tr>
                        ))}
                    </tbody>
                </table>
            </div>
        </section>
    );
}

function RuntimeSettings(): React.ReactElement {
    const { state } = useHarness();
    const id = state.identity;
    return (
        <section className="settings-body" aria-label="运行时">
            <h1>运行时</h1>
            <p className="lead">服务身份、事件流诊断与关于。</p>
            <div className="card">
                <h2>服务身份（契约事实）</h2>
                {id === undefined ? (
                    <p className="muted">尚未连接运行时服务。</p>
                ) : (
                    <table className="table">
                        <tbody>
                            <tr><td>服务名称</td><td className="mono">{id.service}</td></tr>
                            <tr><td>Mirage 版本</td><td className="mono">{id.mirage_version}</td></tr>
                            <tr><td>Mira Core 版本</td><td className="mono">{id.mira_core_version}</td></tr>
                            <tr><td>协议版本</td><td className="mono">v{id.protocol}</td></tr>
                            <tr><td>主机状态</td><td>{hostStatusLabel(id.host_status)}（五态）</td></tr>
                            <tr><td>事件订阅</td><td>{id.events === true ? '支持' : '不支持（前端降级轮询）'}</td></tr>
                        </tbody>
                    </table>
                )}
            </div>
            <div className="card">
                <h2>诊断</h2>
                <p className="muted" style={{ fontSize: 'var(--mir-text-sm)' }}>
                    事件流：{state.eventsSupported ? `订阅中，最近 seq ${state.eventSeq}` : '服务端不支持订阅，前端以 task.inspect 轮询（M1.5-05 降级路径）'}。
                    诊断导出依赖 runtime IPC 面（前瞻依赖）。
                </p>
            </div>
        </section>
    );
}

export function SettingsPage({ category }: { category: string }): React.ReactElement {
    const { navigate } = useHarness();
    const active = useMemo(
        () => ((SETTINGS_CATEGORIES as readonly string[]).includes(category) ? category : 'general'),
        [category],
    );
    return (
        <div className="main-scroll">
            <div className="settings">
                <nav className="settings-nav" aria-label="设置分类">
                    {NAV_ITEMS.map((n) => (
                        <a
                            key={n.id}
                            href={`#/settings/${n.id}`}
                            className={active === n.id ? 'is-active' : ''}
                            onClick={(e) => {
                                e.preventDefault();
                                navigate({ view: 'settings', category: n.id });
                            }}
                        >
                            {n.icon}
                            {n.label}
                        </a>
                    ))}
                </nav>
                {active === 'general' && <GeneralSettings />}
                {active === 'appearance' && <AppearanceSettings />}
                {active === 'models' && <ModelsSettings />}
                {active === 'memory' && <MemorySettings />}
                {active === 'skills' && <SkillsSettings />}
                {active === 'mcp' && <McpSettings />}
                {active === 'permissions' && <PermissionsSettings />}
                {active === 'runtime' && <RuntimeSettings />}
            </div>
        </div>
    );
}

export function ResourcesPage(): React.ReactElement {
    return (
        <div className="main-scroll">
            <div className="page-head">
                <h1>资源</h1>
                <span className="sub">桌面 / 文件 / 工具一览（含 MCP 工具）</span>
            </div>
            <div className="card" style={{ maxWidth: 720 }}>
                <h2>
                    <Boxes size={16} style={{ verticalAlign: -3, marginRight: 6 }} />
                    M2+ 里程碑占位
                </h2>
                <p className="muted" style={{ lineHeight: 1.7 }}>
                    资源页将汇总 Desktop Environment 的 Provider 一览（应用 / 窗口 / 无障碍 / 截屏 /
                    输入 / 剪贴板 / 文件 / 进程 / 通知）与已接入的 MCP 工具清单，并提供逐项权限入口。
                    该视图依赖资源发现 IPC 面（设计规范 §4 前瞻依赖），随 M2 里程碑交付。
                </p>
            </div>
        </div>
    );
}
