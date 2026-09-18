/// Mirage 控制台 — React 入口。
/// 装配顺序：字体 → token 样式表（L1/L2）→ 主题管理 → harness store → 壳。
/// 开发期默认 mock transport（与 M1.5-04 相同纪律）；真实传输经同一
/// MirageTransport 面接入，视图层不感知。

import '@fontsource-variable/saira/wdth.css';
import '@fontsource-variable/chivo-mono/wght.css';
import './styles.css';

import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { createMockTransport } from '@mirage/contracts';

import { App } from './App.js';
import { HarnessProvider } from './hooks.js';
import { HarnessStore } from './state/store.js';
import { themeManager } from './theme/service.js';
import { PRIMITIVE_CSS } from './theme/primitives.js';
import { buildThemeStylesheet } from './theme/schema.js';
import { BUILT_IN_THEMES } from './theme/themes.js';

function mountTokenStyles(): void {
    const style = document.createElement('style');
    style.id = 'mirage-theme-tokens';
    style.textContent = `${PRIMITIVE_CSS}\n${buildThemeStylesheet(BUILT_IN_THEMES)}`;
    document.head.append(style);
}

function bootstrap(): void {
    mountTokenStyles();
    themeManager.start();

    const rootEl = document.getElementById('app');
    if (rootEl === null) {
        throw new Error('missing #app mount point');
    }
    const { transport } = createMockTransport();
    const store = new HarnessStore(transport);
    store.start();

    createRoot(rootEl).render(
        <StrictMode>
            <HarnessProvider store={store}>
                <App />
            </HarnessProvider>
        </StrictMode>,
    );
}

bootstrap();
