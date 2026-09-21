/// WorldView — 入口文件（异步加载）；真正的 React 组件见 `./WorldPageLazy.tsx`。
///
/// 该文件仅供 React.lazy 装载；主 chunk 不会被 three.js 拖累。

import { lazy } from 'react';

export const WorldPageLazyComponent = lazy(async () => {
    const mod = await import('./WorldPageLazy.js');
    return { default: mod.WorldPageLazy };
});

export type { WorldPageProps } from './WorldPageLazy.js';