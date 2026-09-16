/// 主题管理（设计规范 §2.6 机制）：`data-theme` + `data-mode` 挂载在根节点、
/// 切换即时生效（只换属性，不重载界面）、持久化到 localStorage、`system`
/// 模式监听系统明暗变更。组件与视图不感知主题，只消费语义 token。

import type { ResolvedThemeMode, ThemeModePref } from './schema.js';
import { DEFAULT_THEME_ID, findTheme } from './themes.js';

export const APPEARANCE_STORAGE_KEY = 'mirage.appearance';

export interface AppearanceState {
    themeId: string;
    mode: ThemeModePref;
}

export function resolveMode(mode: ThemeModePref, systemDark: boolean): ResolvedThemeMode {
    if (mode === 'system') {
        return systemDark ? 'dark' : 'light';
    }
    return mode;
}

/** Parses persisted appearance; invalid ids/modes fall back to defaults. */
export function parseAppearance(raw: string | null): AppearanceState {
    const fallback: AppearanceState = { themeId: DEFAULT_THEME_ID, mode: 'system' };
    if (raw === null) {
        return fallback;
    }
    try {
        const value = JSON.parse(raw) as { themeId?: unknown; mode?: unknown };
        const themeId = typeof value.themeId === 'string' && findTheme(value.themeId) !== undefined
            ? value.themeId
            : fallback.themeId;
        const mode = value.mode === 'light' || value.mode === 'dark' || value.mode === 'system'
            ? value.mode
            : fallback.mode;
        return { themeId, mode };
    } catch {
        return fallback;
    }
}

type Listener = (state: AppearanceState) => void;

export class ThemeManager {
    private state: AppearanceState;
    private readonly listeners = new Set<Listener>();
    private systemMedia: MediaQueryList | null = null;
    private readonly systemListener = () => this.apply();

    constructor(
        private readonly root: HTMLElement = document.documentElement,
        private readonly storage: Storage | null = typeof localStorage === 'undefined' ? null : localStorage,
        private readonly media: () => MediaQueryList | null = () =>
            typeof matchMedia === 'function' ? matchMedia('(prefers-color-scheme: dark)') : null,
    ) {
        this.state = parseAppearance(this.storage?.getItem(APPEARANCE_STORAGE_KEY) ?? null);
    }

    get(): AppearanceState {
        return this.state;
    }

    subscribe(listener: Listener): () => void {
        this.listeners.add(listener);
        return () => this.listeners.delete(listener);
    }

    /** Applies persisted appearance and starts following system changes. */
    start(): void {
        this.apply();
        this.systemMedia = this.media();
        this.systemMedia?.addEventListener('change', this.systemListener);
    }

    stop(): void {
        this.systemMedia?.removeEventListener('change', this.systemListener);
        this.systemMedia = null;
    }

    setTheme(themeId: string): void {
        if (findTheme(themeId) === undefined || themeId === this.state.themeId) {
            return;
        }
        this.state = { ...this.state, themeId };
        this.persistAndApply();
    }

    setMode(mode: ThemeModePref): void {
        if (mode === this.state.mode) {
            return;
        }
        this.state = { ...this.state, mode };
        this.persistAndApply();
    }

    /** Current effective dark/light mode (also exposed for tests/previews). */
    resolvedMode(): ResolvedThemeMode {
        return resolveMode(this.state.mode, this.systemMedia?.matches ?? false);
    }

    private persistAndApply(): void {
        this.storage?.setItem(APPEARANCE_STORAGE_KEY, JSON.stringify(this.state));
        this.apply();
        for (const listener of this.listeners) {
            listener(this.state);
        }
    }

    private apply(): void {
        this.root.dataset.theme = this.state.themeId;
        this.root.dataset.mode = this.resolvedMode();
    }
}
