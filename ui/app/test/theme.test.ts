/// M1.5-08 验收：语义 token 全量覆盖（§2.6 门槛 1）、对比度与状态色可区分
/// （门槛 2）、主题只影响颜色/圆角/阴影（门槛 5 静态等价物）。

import { describe, expect, it } from 'vitest';

import { PRIMITIVES } from '../src/theme/primitives.js';
import { buildThemeStylesheet, SEMANTIC_TOKENS, type Theme } from '../src/theme/schema.js';
import { BUILT_IN_THEMES, DEFAULT_THEME_ID, findTheme } from '../src/theme/themes.js';

const MODES = ['light', 'dark'] as const;

// ---- 颜色解析与 WCAG 相对亮度 ----

type Rgb = { r: number; g: number; b: number };

function parseHex(hex: string): Rgb {
    let s = hex.slice(1);
    if (s.length === 3 || s.length === 4) {
        s = [...s].map((c) => c + c).join('');
    }
    return {
        r: parseInt(s.slice(0, 2), 16),
        g: parseInt(s.slice(2, 4), 16),
        b: parseInt(s.slice(4, 6), 16),
    };
}

function parseRgbFunctional(value: string): Rgb {
    // rgb(18 22 28 / 45%) 或 rgb(18, 22, 28)
    const body = value.slice(value.indexOf('(') + 1, value.lastIndexOf(')'));
    const nums = body.split('/')[0]!.split(/[,\s]+/).filter((n) => n !== '');
    return { r: Number(nums[0]), g: Number(nums[1]!), b: Number(nums[2]!) };
}

function parseColor(value: string): Rgb {
    const v = value.trim();
    if (v.startsWith('#')) {
        return parseHex(v);
    }
    if (/^rgba?\(/i.test(v)) {
        return parseRgbFunctional(v);
    }
    throw new Error(`unsupported color literal: ${v}`);
}

/** 解析主题值：`var(--mir-*)` 经 PRIMITIVES 解析为具体色值（一层引用）。 */
function resolve(value: string): string {
    const m = /^var\((--mir-[a-z0-9-]+)\)$/i.exec(value.trim());
    if (m === null) {
        return value;
    }
    const primitive = PRIMITIVES[m[1]!];
    if (primitive === undefined) {
        throw new Error(`unknown primitive ${m[1]}`);
    }
    return primitive;
}

function channel(c: number): number {
    const s = c / 255;
    return s <= 0.03928 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
}

function luminance({ r, g, b }: Rgb): number {
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

function contrast(a: string, b: string): number {
    const la = luminance(parseColor(a));
    const lb = luminance(parseColor(b));
    const [hi, lo] = la >= lb ? [la, lb] : [lb, la];
    return (hi + 0.05) / (lo + 0.05);
}

// ---- 门槛 1：语义 token 全量覆盖 ----

describe('§2.6-1 semantic token coverage', () => {
    it('SEMANTIC_TOKENS has 25 unique tokens', () => {
        expect(SEMANTIC_TOKENS).toHaveLength(25);
        expect(new Set(SEMANTIC_TOKENS).size).toBe(25);
    });

    for (const theme of BUILT_IN_THEMES) {
        for (const mode of MODES) {
            it(`${theme.id}/${mode} covers exactly the semantic token set`, () => {
                const values = theme[mode];
                expect(Object.keys(values).sort()).toEqual([...SEMANTIC_TOKENS].sort());
            });
        }
    }

    it('5 built-in themes with unique ids and default resolvable', () => {
        expect(BUILT_IN_THEMES).toHaveLength(5);
        const ids = BUILT_IN_THEMES.map((t) => t.id);
        expect(new Set(ids).size).toBe(5);
        expect(findTheme(DEFAULT_THEME_ID)?.id).toBe(DEFAULT_THEME_ID);
    });

    it('buildThemeStylesheet declares every token for every theme/mode rule', () => {
        const css = buildThemeStylesheet(BUILT_IN_THEMES);
        for (const theme of BUILT_IN_THEMES) {
            for (const mode of MODES) {
                const selector = `:root[data-theme='${theme.id}'][data-mode='${mode}']`;
                const start = css.indexOf(selector);
                expect(start, `missing rule for ${selector}`).toBeGreaterThanOrEqual(0);
                const block = css.slice(start, css.indexOf('}', start));
                for (const token of SEMANTIC_TOKENS) {
                    expect(
                        block.includes(`--${token}:`),
                        `${selector} missing --${token}`,
                    ).toBe(true);
                }
            }
        }
    });
});

// ---- 门槛 2：对比度与状态色可区分 ----

const TEXT_ON_BACKGROUND: readonly string[] = [
    'foreground',
    'muted-foreground',
    'card-foreground',
    'secondary-foreground',
    'accent-foreground',
];
const UI_ON_BACKGROUND: readonly string[] = [
    'primary',
    'destructive',
    'success',
    'warning',
    'info',
    'evidence-highlight',
];

describe('§2.6-2 contrast thresholds', () => {
    for (const theme of BUILT_IN_THEMES) {
        for (const mode of MODES) {
            const v = theme[mode];
            const bg = resolve(v['background']);

            for (const token of TEXT_ON_BACKGROUND) {
                it(`${theme.id}/${mode} ${token} vs background >= 4.5`, () => {
                    expect(contrast(resolve(v[token]), bg)).toBeGreaterThanOrEqual(4.5);
                });
            }
            for (const token of UI_ON_BACKGROUND) {
                it(`${theme.id}/${mode} ${token} vs background >= 3`, () => {
                    expect(contrast(resolve(v[token]), bg)).toBeGreaterThanOrEqual(3);
                });
            }
            it(`${theme.id}/${mode} primary-foreground vs primary >= 4.5`, () => {
                expect(contrast(resolve(v['primary-foreground']), resolve(v['primary']))).toBeGreaterThanOrEqual(4.5);
            });
            it(`${theme.id}/${mode} status colors pairwise distinct`, () => {
                const colors = ['success', 'warning', 'destructive', 'info'].map((t) => resolve(v[t]));
                expect(new Set(colors).size).toBe(4);
            });
        }
    }
});

// ---- 门槛 5：主题只设置颜色/圆角/阴影自定义属性（布局零位移静态等价物） ----

describe('§2.6-5 theme only affects colors/radius/shadow', () => {
    it('every declaration sets a custom property only', () => {
        const css = buildThemeStylesheet(BUILT_IN_THEMES);
        const declarations = [...css.matchAll(/^\s*(--[\w-]+)\s*:\s*([^;]+);$/gm)].map((m) => m[1]!);
        expect(declarations.length).toBeGreaterThan(0);
        for (const prop of declarations) {
            expect(
                prop.startsWith('--') &&
                    (SEMANTIC_TOKENS.includes(prop.slice(2) as never) ||
                        /^--mir-radius-(sm|md|lg)$/.test(prop) ||
                        /^--shadow-(card|overlay)$/.test(prop)),
                `unexpected property ${prop}`,
            ).toBe(true);
        }
    });

    it('accepts themes with optional radius/shadow but no layout properties', () => {
        const theme: Theme = {
            id: 'test-radius',
            name: 't',
            light: BUILT_IN_THEMES[0]!.light,
            dark: BUILT_IN_THEMES[0]!.dark,
            radius: { sm: '4px', md: '6px', lg: '8px' },
            shadow: { card: '0 1px 2px #000', overlay: '0 2px 4px #000' },
        };
        const css = buildThemeStylesheet([theme]);
        expect(css).toContain('--mir-radius-sm: 4px;');
        expect(css).toContain('--shadow-overlay: 0 2px 4px #000;');
        const nonCustom = [...css.matchAll(/^\s*([a-z-]+)\s*:/gim)].filter((m) => !m[1]!.startsWith('--'));
        expect(nonCustom).toEqual([]);
    });
});
