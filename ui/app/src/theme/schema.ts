/// 主题 schema（设计规范 §2.6）：主题 = 同一语义 token 契约（L2 全量清单）
/// 的多套值集。本文件是 L2 清单与 CSS 产物形态的唯一事实源 —— 组件与视图
/// 禁止取用语义层之外的颜色（验收硬门槛，以测试锁定）。

/** L2 语义 token 全量清单（shadcn 兼容命名 + Mirage 扩展）。 */
export const SEMANTIC_TOKENS = [
    'background',
    'foreground',
    'card',
    'card-foreground',
    'popover',
    'primary',
    'primary-foreground',
    'secondary',
    'secondary-foreground',
    'muted',
    'muted-foreground',
    'accent',
    'accent-foreground',
    'destructive',
    'success',
    'warning',
    'info',
    'border',
    'input',
    'ring',
    'sidebar-background',
    'surface-raised',
    'overlay-scrim',
    'evidence-highlight',
    'evidence-highlight-soft',
] as const;

export type SemanticToken = (typeof SEMANTIC_TOKENS)[number];
export type SemanticTokenValues = Record<SemanticToken, string>;

/** 主题内每套值只允许颜色值；半径/阴影微调走独立可选字段（布局零位移门槛：
 * 主题不得引入影响布局的属性）。 */
export interface ThemeRadius {
    readonly sm: string;
    readonly md: string;
    readonly lg: string;
}

export interface ThemeShadow {
    readonly card: string;
    readonly overlay: string;
}

export interface Theme {
    readonly id: string;
    readonly name: string;
    readonly light: SemanticTokenValues;
    readonly dark: SemanticTokenValues;
    readonly radius?: ThemeRadius;
    readonly shadow?: ThemeShadow;
}

export type ThemeModePref = 'light' | 'dark' | 'system';
export type ResolvedThemeMode = 'light' | 'dark';

function declarationBlock(values: SemanticTokenValues, theme: Theme): string {
    const lines = SEMANTIC_TOKENS.map((token) => `    --${token}: ${values[token]};`);
    if (theme.radius !== undefined) {
        lines.push(
            `    --mir-radius-sm: ${theme.radius.sm};`,
            `    --mir-radius-md: ${theme.radius.md};`,
            `    --mir-radius-lg: ${theme.radius.lg};`,
        );
    }
    if (theme.shadow !== undefined) {
        lines.push(`    --shadow-card: ${theme.shadow.card};`, `    --shadow-overlay: ${theme.shadow.overlay};`);
    }
    return lines.join('\n');
}

/** Generates the theme stylesheet: L1 primitives (fixed, not themed) plus a
 * `[data-theme][data-mode]` rule pair per theme. Mounted once by main.ts. */
export function buildThemeStylesheet(themes: readonly Theme[]): string {
    const rules = themes.map((theme) => [
        `:root[data-theme='${theme.id}'][data-mode='light'] {\n${declarationBlock(theme.light, theme)}\n}`,
        `:root[data-theme='${theme.id}'][data-mode='dark'] {\n${declarationBlock(theme.dark, theme)}\n}`,
    ].join('\n'));
    return rules.join('\n');
}
