/// 内置主题（设计规范 §2.6，M1.5-08 交付 5 套）：每套提供 light/dark 完整
/// L2 值集。晨蓝（mirage-dawn）严格等于规范 §2.2 基准值；其余主题按 §2.6
/// 门槛（对比度、状态可区分、语义封闭）校准。状态四色在所有主题中保持
/// 语义 hue（成功绿/失败红/等待琥珀/信息蓝），主题只改变气质色与中性底。

import type { SemanticTokenValues, Theme } from './schema.js';

const g = (name: string): string => `var(--mir-${name})`;

/** 状态四色（light）：在规范 §2.2 基准上按 §2.6 对比度门槛校准
 * （绿/琥珀加深至 UI 边界 ≥ 3:1， hue 与语义不变）。 */
const STATUS_LIGHT: Pick<
    SemanticTokenValues, 'destructive' | 'success' | 'warning' | 'info'
> = {
    destructive: g('red-500'),
    success: '#157f3d',
    warning: '#b2540a',
    info: g('sky-500'),
};

const STATUS_DARK: Pick<
    SemanticTokenValues, 'destructive' | 'success' | 'warning' | 'info'
> = {
    destructive: g('red-400d'),
    success: g('green-400d'),
    warning: g('amber-400d'),
    info: g('sky-400d'),
};

/** 任务控制台 light 的状态四色：底色为较深的奶油灰（#d6d0c2），在共享
 * STATUS_LIGHT 基础上按 §2.6 门槛加深（hue 与语义封闭映射不变）。 */
const CONSOLE_STATUS_LIGHT: Pick<
    SemanticTokenValues, 'destructive' | 'success' | 'warning' | 'info'
> = {
    destructive: '#b91c1c',
    success: '#166534',
    warning: '#9a4a08',
    info: '#0369a1',
};

/** 玄墨的状态四色：低饱和深色变体，保持四色 hue 相互可区分。 */
const INK_STATUS_LIGHT: Pick<
    SemanticTokenValues, 'destructive' | 'success' | 'warning' | 'info'
> = {
    destructive: '#b91c1c',
    success: '#15803d',
    warning: '#b45309',
    info: '#0369a1',
};

const INK_STATUS_DARK: Pick<
    SemanticTokenValues, 'destructive' | 'success' | 'warning' | 'info'
> = {
    destructive: '#ef4444',
    success: '#4ade80',
    warning: '#f59e0b',
    info: '#38bdf8',
};

const dawn: Theme = {
    id: 'mirage-dawn',
    name: '晨蓝',
    light: {
        background: g('gray-50'),
        foreground: g('gray-900'),
        card: g('gray-0'),
        'card-foreground': g('gray-900'),
        popover: g('gray-0'),
        primary: g('blue-500'),
        'primary-foreground': '#ffffff',
        secondary: g('gray-100'),
        'secondary-foreground': g('gray-700'),
        muted: g('gray-100'),
        'muted-foreground': g('gray-500'),
        accent: g('blue-50'),
        'accent-foreground': g('blue-600'),
        border: g('gray-200'),
        input: g('gray-200'),
        ring: g('blue-500'),
        'sidebar-background': g('gray-25'),
        'surface-raised': g('gray-0'),
        'overlay-scrim': 'rgb(18 22 28 / 45%)',
        'evidence-highlight': g('blue-500'),
        'evidence-highlight-soft': 'rgb(37 99 235 / 20%)',
        ...STATUS_LIGHT,
    },
    dark: {
        background: g('gray-950'),
        foreground: '#e6eaf0',
        card: '#181d24',
        'card-foreground': '#e6eaf0',
        popover: '#1c222b',
        primary: g('blue-400d'),
        'primary-foreground': '#0b1220',
        secondary: '#232b36',
        'secondary-foreground': '#c9d1dc',
        muted: '#232b36',
        'muted-foreground': '#8b94a3',
        accent: '#1d2a44',
        'accent-foreground': g('blue-400d'),
        border: '#262d37',
        input: '#2a323e',
        ring: g('blue-400d'),
        'sidebar-background': '#151a21',
        'surface-raised': '#1c222b',
        'overlay-scrim': 'rgb(0 0 0 / 60%)',
        'evidence-highlight': g('blue-400d'),
        'evidence-highlight-soft': 'rgb(76 141 255 / 28%)',
        ...STATUS_DARK,
    },
};

const nordic: Theme = {
    id: 'mirage-nordic',
    name: '冷杉',
    light: {
        background: '#eef2f6',
        foreground: '#1b2733',
        card: '#f8fafc',
        'card-foreground': '#1b2733',
        popover: '#fbfdfe',
        primary: '#2e77ad',
        'primary-foreground': '#ffffff',
        secondary: '#dfe7ee',
        'secondary-foreground': '#33424f',
        muted: '#e2e9f0',
        'muted-foreground': '#5c6b7a',
        accent: '#d9e8f3',
        'accent-foreground': '#24597f',
        border: '#d3dde6',
        input: '#cfd9e3',
        ring: '#2e77ad',
        'sidebar-background': '#f3f7fa',
        'surface-raised': '#fbfdfe',
        'overlay-scrim': 'rgb(20 28 38 / 45%)',
        'evidence-highlight': '#2e77ad',
        'evidence-highlight-soft': 'rgb(46 119 173 / 20%)',
        ...STATUS_LIGHT,
    },
    dark: {
        background: '#0f151c',
        foreground: '#dfe7ee',
        card: '#16202a',
        'card-foreground': '#dfe7ee',
        popover: '#1a242f',
        primary: '#7db3e0',
        'primary-foreground': '#0d1822',
        secondary: '#223140',
        'secondary-foreground': '#c2cfdb',
        muted: '#223140',
        'muted-foreground': '#8798a8',
        accent: '#1d3245',
        'accent-foreground': '#7db3e0',
        border: '#26333f',
        input: '#2a3946',
        ring: '#7db3e0',
        'sidebar-background': '#131b23',
        'surface-raised': '#1a242f',
        'overlay-scrim': 'rgb(0 0 0 / 60%)',
        'evidence-highlight': '#7db3e0',
        'evidence-highlight-soft': 'rgb(125 179 224 / 28%)',
        ...STATUS_DARK,
    },
};

const ember: Theme = {
    id: 'mirage-ember',
    name: '暖沙',
    light: {
        background: '#f7f3ee',
        foreground: '#2b241d',
        card: '#fffdf9',
        'card-foreground': '#2b241d',
        popover: '#fffefb',
        primary: '#9c5a1e',
        'primary-foreground': '#ffffff',
        secondary: '#efe6da',
        'secondary-foreground': '#5a4a3a',
        muted: '#f0e8de',
        'muted-foreground': '#6d5f50',
        accent: '#f3e4d3',
        'accent-foreground': '#7c4a15',
        border: '#e3d8c9',
        input: '#ddd1bf',
        ring: '#9c5a1e',
        'sidebar-background': '#faf6f0',
        'surface-raised': '#fffefb',
        'overlay-scrim': 'rgb(38 29 20 / 45%)',
        'evidence-highlight': '#9c5a1e',
        'evidence-highlight-soft': 'rgb(156 90 30 / 20%)',
        ...STATUS_LIGHT,
    },
    dark: {
        background: '#191412',
        foreground: '#ece3d9',
        card: '#201a16',
        'card-foreground': '#ece3d9',
        popover: '#241d19',
        primary: '#d99a55',
        'primary-foreground': '#1c130a',
        secondary: '#332a22',
        'secondary-foreground': '#d3c5b6',
        muted: '#332a22',
        'muted-foreground': '#a08e7c',
        accent: '#3a2c1e',
        'accent-foreground': '#d99a55',
        border: '#2e261f',
        input: '#342b23',
        ring: '#d99a55',
        'sidebar-background': '#171210',
        'surface-raised': '#241d19',
        'overlay-scrim': 'rgb(0 0 0 / 60%)',
        'evidence-highlight': '#d99a55',
        'evidence-highlight-soft': 'rgb(217 154 85 / 28%)',
        ...STATUS_DARK,
    },
};

const matcha: Theme = {
    id: 'mirage-matcha',
    name: '抹茶',
    light: {
        background: '#f2f5ee',
        foreground: '#232b1e',
        card: '#fbfdf8',
        'card-foreground': '#232b1e',
        popover: '#fdfefa',
        primary: '#4f7312',
        'primary-foreground': '#ffffff',
        secondary: '#e4eadd',
        'secondary-foreground': '#3f4a34',
        muted: '#e7ece0',
        'muted-foreground': '#5d6a50',
        accent: '#e2ecd4',
        'accent-foreground': '#3d5a10',
        border: '#d6ddca',
        input: '#d0d9c2',
        ring: '#4f7312',
        'sidebar-background': '#f5f8f1',
        'surface-raised': '#fdfefa',
        'overlay-scrim': 'rgb(24 31 18 / 45%)',
        'evidence-highlight': '#4f7312',
        'evidence-highlight-soft': 'rgb(79 115 18 / 20%)',
        ...STATUS_LIGHT,
    },
    dark: {
        background: '#121710',
        foreground: '#e0e6d8',
        card: '#1a2117',
        'card-foreground': '#e0e6d8',
        popover: '#1e261b',
        primary: '#94bd5c',
        'primary-foreground': '#101807',
        secondary: '#26301f',
        'secondary-foreground': '#cbd5bd',
        muted: '#26301f',
        'muted-foreground': '#8d9b7c',
        accent: '#223019',
        'accent-foreground': '#94bd5c',
        border: '#273224',
        input: '#2b3827',
        ring: '#94bd5c',
        'sidebar-background': '#161c14',
        'surface-raised': '#1e261b',
        'overlay-scrim': 'rgb(0 0 0 / 60%)',
        'evidence-highlight': '#94bd5c',
        'evidence-highlight-soft': 'rgb(148 189 92 / 28%)',
        ...STATUS_DARK,
    },
};

const ink: Theme = {
    id: 'mirage-ink',
    name: '玄墨',
    light: {
        background: '#f4f4f4',
        foreground: '#151515',
        card: '#ffffff',
        'card-foreground': '#151515',
        popover: '#ffffff',
        primary: '#151515',
        'primary-foreground': '#ffffff',
        secondary: '#e6e6e6',
        'secondary-foreground': '#333333',
        muted: '#e8e8e8',
        'muted-foreground': '#555555',
        accent: '#ececec',
        'accent-foreground': '#111111',
        border: '#d6d6d6',
        input: '#d2d2d2',
        ring: '#151515',
        'sidebar-background': '#f8f8f8',
        'surface-raised': '#ffffff',
        'overlay-scrim': 'rgb(10 10 10 / 45%)',
        'evidence-highlight': '#151515',
        'evidence-highlight-soft': 'rgb(21 21 21 / 20%)',
        ...INK_STATUS_LIGHT,
    },
    dark: {
        background: '#0c0c0c',
        foreground: '#ededed',
        card: '#141414',
        'card-foreground': '#ededed',
        popover: '#171717',
        primary: '#ededed',
        'primary-foreground': '#0a0a0a',
        secondary: '#222222',
        'secondary-foreground': '#cfcfcf',
        muted: '#222222',
        'muted-foreground': '#909090',
        accent: '#242424',
        'accent-foreground': '#ededed',
        border: '#232323',
        input: '#282828',
        ring: '#ededed',
        'sidebar-background': '#101010',
        'surface-raised': '#171717',
        'overlay-scrim': 'rgb(0 0 0 / 60%)',
        'evidence-highlight': '#fafafa',
        'evidence-highlight-soft': 'rgb(250 250 250 / 24%)',
        ...INK_STATUS_DARK,
    },
};

/** 任务控制台（MISSION CONSOLE，UI 重写默认主题）：控制台炭蓝底 + 奶油仪
 * 表面板，琥珀为行动/主动读数色，蓝线剖面蓝为证据强调。dark = 夜班大厅，
 * light = 白班控制室。状态四色沿用共享校准常量，语义封闭不变。 */
const inkConsole = '#eae5d8';
const consoleTheme: Theme = {
    id: 'mirage-console',
    name: '任务控制台',
    light: {
        background: '#d6d0c2',
        foreground: '#23282f',
        card: g('cream-100'),
        'card-foreground': g('cream-900'),
        popover: g('cream-50'),
        primary: g('amber-700'),
        'primary-foreground': '#fff6e8',
        secondary: g('cream-200'),
        'secondary-foreground': '#4a4636',
        muted: '#e9e2d0',
        'muted-foreground': g('cream-700'),
        accent: 'rgb(138 77 8 / 12%)',
        'accent-foreground': '#7c460a',
        destructive: CONSOLE_STATUS_LIGHT.destructive,
        success: CONSOLE_STATUS_LIGHT.success,
        warning: CONSOLE_STATUS_LIGHT.warning,
        info: CONSOLE_STATUS_LIGHT.info,
        border: '#c4bca4',
        input: '#b9b099',
        ring: g('amber-700'),
        'sidebar-background': '#cbc5b6',
        'surface-raised': g('cream-50'),
        'overlay-scrim': 'rgb(40 36 24 / 45%)',
        'evidence-highlight': g('blueline-700'),
        'evidence-highlight-soft': 'rgb(46 111 158 / 15%)',
    },
    dark: {
        background: g('console-950'),
        foreground: inkConsole,
        card: g('console-800'),
        'card-foreground': inkConsole,
        popover: g('console-900'),
        primary: g('amber-450'),
        'primary-foreground': '#231a06',
        secondary: g('console-700'),
        'secondary-foreground': '#cfc9b8',
        muted: g('console-800'),
        'muted-foreground': '#9aa2ac',
        accent: 'rgb(232 163 61 / 14%)',
        'accent-foreground': g('amber-300'),
        destructive: STATUS_DARK.destructive,
        success: STATUS_DARK.success,
        warning: STATUS_DARK.warning,
        info: STATUS_DARK.info,
        border: '#33404e',
        input: '#3a4756',
        ring: g('amber-450'),
        'sidebar-background': g('console-900'),
        'surface-raised': g('console-700'),
        'overlay-scrim': 'rgb(10 13 17 / 60%)',
        'evidence-highlight': g('blueline-400'),
        'evidence-highlight-soft': 'rgb(127 179 213 / 16%)',
    },
    radius: { sm: '4px', md: '6px', lg: '8px' },
    shadow: {
        card: '0 1px 2px rgb(8 10 14 / 35%)',
        overlay: '0 12px 32px rgb(8 10 14 / 50%)',
    },
};

export const BUILT_IN_THEMES: readonly Theme[] = [consoleTheme, dawn, nordic, ember, matcha, ink];

export const DEFAULT_THEME_ID = 'mirage-console';

export function findTheme(id: string): Theme | undefined {
    return BUILT_IN_THEMES.find((theme) => theme.id === id);
}
