/// L1 primitive 原始值（设计规范 §2.2 基准）：非主题化的事实源。主题值可以
/// 以 `var(--mir-*)` 引用这里的原始值；测试侧经 `PRIMITIVES` 解析为具体色值。

export const PRIMITIVES: Readonly<Record<string, string>> = {
    // 中性色阶（cool gray）
    '--mir-gray-0': '#ffffff',
    '--mir-gray-25': '#fbfcfe',
    '--mir-gray-50': '#f4f6f9',
    '--mir-gray-100': '#e9edf3',
    '--mir-gray-200': '#dde3ec',
    '--mir-gray-300': '#c6cedb',
    '--mir-gray-400': '#9aa4b2',
    '--mir-gray-500': '#66707d',
    '--mir-gray-600': '#4a5361',
    '--mir-gray-700': '#333c49',
    '--mir-gray-800': '#232b36',
    '--mir-gray-900': '#1c2530',
    '--mir-gray-950': '#12161c',
    // 品牌蓝
    '--mir-blue-50': '#eef4fe',
    '--mir-blue-100': '#dce8fd',
    '--mir-blue-300': '#8db4f7',
    '--mir-blue-400d': '#4c8dff',
    '--mir-blue-500': '#2563eb',
    '--mir-blue-600': '#1d4fd8',
    // 状态色
    '--mir-green-500': '#16a34a',
    '--mir-green-400d': '#34d399',
    '--mir-amber-500': '#d97706',
    '--mir-amber-400d': '#fbbf24',
    '--mir-red-500': '#dc2626',
    '--mir-red-400d': '#f87171',
    '--mir-sky-500': '#0284c7',
    '--mir-sky-400d': '#38bdf8',
};

/** L1 primitive CSS（间距/半径/字号/动效等非颜色阶 + 颜色阶）。 */
export const PRIMITIVE_CSS = `:root {
    /* 中性色阶（cool gray） */
    --mir-gray-0: #ffffff;   --mir-gray-25: #fbfcfe;  --mir-gray-50: #f4f6f9;
    --mir-gray-100: #e9edf3; --mir-gray-200: #dde3ec; --mir-gray-300: #c6cedb;
    --mir-gray-400: #9aa4b2; --mir-gray-500: #66707d; --mir-gray-600: #4a5361;
    --mir-gray-700: #333c49; --mir-gray-800: #232b36; --mir-gray-900: #1c2530;
    /* 品牌蓝 */
    --mir-blue-50: #eef4fe; --mir-blue-100: #dce8fd; --mir-blue-300: #8db4f7;
    --mir-blue-500: #2563eb; --mir-blue-600: #1d4fd8; --mir-blue-400d: #4c8dff;
    /* 状态色 */
    --mir-green-500: #16a34a;  --mir-green-400d: #34d399;
    --mir-amber-500: #d97706;  --mir-amber-400d: #fbbf24;
    --mir-red-500: #dc2626;    --mir-red-400d: #f87171;
    --mir-sky-500: #0284c7;    --mir-sky-400d: #38bdf8;
    /* 间距（4 基） */
    --mir-space-1: 4px;  --mir-space-2: 8px;  --mir-space-3: 12px;
    --mir-space-4: 16px; --mir-space-5: 20px; --mir-space-6: 24px; --mir-space-8: 32px;
    /* 半径 */
    --mir-radius-sm: 6px; --mir-radius-md: 8px; --mir-radius-lg: 10px; --mir-radius-pill: 999px;
    /* 字号阶 */
    --mir-text-xs: 12px; --mir-text-sm: 13px; --mir-text-base: 14px; --mir-text-lg: 16px;
    --mir-text-xl: 18px; --mir-text-2xl: 20px; --mir-text-3xl: 24px;
    /* 动效 */
    --mir-dur-fast: 150ms; --mir-dur-slow: 250ms;
    --mir-ease: cubic-bezier(0.2, 0, 0, 1);
    /* 阴影（仅两档） */
    --shadow-card: 0 1px 2px rgb(16 24 40 / 6%), 0 1px 3px rgb(16 24 40 / 10%);
    --shadow-overlay: 0 8px 24px rgb(16 24 40 / 16%);
}
@media (prefers-reduced-motion: reduce) {
    :root { --mir-dur-fast: 0ms; --mir-dur-slow: 0ms; }
}`;
