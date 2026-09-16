/// M1.5-08 验收：parseAppearance / resolveMode 与 ThemeManager（注入 fake
/// root/storage/media，纯 Node 环境）。

import { describe, expect, it } from 'vitest';

import {
    APPEARANCE_STORAGE_KEY,
    parseAppearance,
    resolveMode,
    ThemeManager,
} from '../src/theme/theme-manager.js';

function fakeStorage(initial: Record<string, string> = {}): Storage {
    const data = new Map(Object.entries(initial));
    return {
        getItem: (k: string) => data.get(k) ?? null,
        setItem: (k: string, v: string) => void data.set(k, v),
        removeItem: (k: string) => void data.delete(k),
        clear: () => data.clear(),
        key: () => null,
        get length() {
            return data.size;
        },
    } as unknown as Storage;
}

function fakeRoot(): HTMLElement {
    return { dataset: {} } as unknown as HTMLElement;
}

function fakeMedia(initialDark: boolean): MediaQueryList & { setMatches(b: boolean): void } {
    const listeners = new Set<() => void>();
    const media = {
        matches: initialDark,
        addEventListener: (_t: string, l: () => void) => void listeners.add(l),
        removeEventListener: (_t: string, l: () => void) => void listeners.delete(l),
        setMatches(dark: boolean): void {
            (this as { matches: boolean }).matches = dark;
            for (const l of listeners) {
                l();
            }
        },
    };
    return media as unknown as MediaQueryList & { setMatches(b: boolean): void };
}

describe('resolveMode', () => {
    it('system follows prefers-color-scheme', () => {
        expect(resolveMode('system', true)).toBe('dark');
        expect(resolveMode('system', false)).toBe('light');
    });
    it('light/dark pass through', () => {
        expect(resolveMode('light', true)).toBe('light');
        expect(resolveMode('dark', false)).toBe('dark');
    });
});

describe('parseAppearance', () => {
    it('null falls back to default', () => {
        expect(parseAppearance(null)).toEqual({ themeId: 'mirage-dawn', mode: 'system' });
    });
    it('invalid JSON falls back', () => {
        expect(parseAppearance('{{{')).toEqual({ themeId: 'mirage-dawn', mode: 'system' });
    });
    it('invalid themeId falls back to default theme', () => {
        expect(parseAppearance(JSON.stringify({ themeId: 'nope', mode: 'dark' }))).toEqual({
            themeId: 'mirage-dawn',
            mode: 'dark',
        });
    });
    it('invalid mode falls back to system', () => {
        expect(parseAppearance(JSON.stringify({ themeId: 'mirage-nordic', mode: 'blue' }))).toEqual({
            themeId: 'mirage-nordic',
            mode: 'system',
        });
    });
    it('valid appearance round-trips', () => {
        expect(parseAppearance(JSON.stringify({ themeId: 'mirage-ink', mode: 'dark' }))).toEqual({
            themeId: 'mirage-ink',
            mode: 'dark',
        });
    });
});

describe('ThemeManager', () => {
    it('start() mounts data-theme/data-mode from persisted state', () => {
        const root = fakeRoot();
        const storage = fakeStorage({
            [APPEARANCE_STORAGE_KEY]: JSON.stringify({ themeId: 'mirage-nordic', mode: 'dark' }),
        });
        const media = fakeMedia(true);
        const mgr = new ThemeManager(root, storage, () => media);
        mgr.start();
        expect(root.dataset.theme).toBe('mirage-nordic');
        expect(root.dataset.mode).toBe('dark');
        mgr.stop();
    });

    it('setTheme updates attribute and persists; unknown id ignored', () => {
        const root = fakeRoot();
        const storage = fakeStorage();
        const media = fakeMedia(false);
        const mgr = new ThemeManager(root, storage, () => media);
        mgr.start();
        expect(root.dataset.theme).toBe('mirage-dawn');
        expect(root.dataset.mode).toBe('light');

        mgr.setTheme('mirage-matcha');
        expect(root.dataset.theme).toBe('mirage-matcha');
        expect(JSON.parse(storage.getItem(APPEARANCE_STORAGE_KEY)!)).toEqual({
            themeId: 'mirage-matcha',
            mode: 'system',
        });

        mgr.setTheme('does-not-exist');
        expect(root.dataset.theme).toBe('mirage-matcha');
        expect(mgr.get().themeId).toBe('mirage-matcha');
        mgr.stop();
    });

    it('setMode updates attribute and persists', () => {
        const root = fakeRoot();
        const storage = fakeStorage();
        const mgr = new ThemeManager(root, storage, () => fakeMedia(false));
        mgr.start();
        mgr.setMode('dark');
        expect(root.dataset.mode).toBe('dark');
        expect(JSON.parse(storage.getItem(APPEARANCE_STORAGE_KEY)!).mode).toBe('dark');
        mgr.setMode('system');
        expect(root.dataset.mode).toBe('light');
        mgr.stop();
    });

    it('system mode follows media matches change', () => {
        const root = fakeRoot();
        const media = fakeMedia(false);
        const mgr = new ThemeManager(root, fakeStorage(), () => media);
        mgr.start();
        expect(root.dataset.mode).toBe('light');
        media.setMatches(true);
        expect(root.dataset.mode).toBe('dark');
        media.setMatches(false);
        expect(root.dataset.mode).toBe('light');

        // stop() removes the listener
        mgr.stop();
        media.setMatches(true);
        expect(root.dataset.mode).toBe('light');
    });
});
