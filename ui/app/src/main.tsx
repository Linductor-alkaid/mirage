/// Mirage 控制台 — React 入口。
/// 装配顺序：字体 → token 样式表（L1/L2）→ 主题管理 → harness store → 壳。
/// transport 选择（M1.5-05）：默认 mock；`?transport=bridge` 或 `?ws=<url>`
/// 走 dev bridge WebSocket 真实传输（断线自动重连 + resync）；mock 的
/// `?events=off` 构造无事件能力的服务，用于浏览器内验收降级轮询路径。

import '@fontsource-variable/saira/wdth.css';
import '@fontsource-variable/chivo-mono/wght.css';
import './styles.css';

import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { createMockTransport, WsBridgeTransport } from '@mirage/contracts';
import type { MirageTransport } from '@mirage/contracts';

import { App } from './App.js';
import { HarnessProvider } from './hooks.js';
import { HarnessStore } from './state/store.js';
import { themeManager } from './theme/service.js';
import { PRIMITIVE_CSS } from './theme/primitives.js';
import { buildThemeStylesheet } from './theme/schema.js';
import { BUILT_IN_THEMES } from './theme/themes.js';

const DEFAULT_BRIDGE_URL = 'ws://127.0.0.1:8787/';

function createTransportSelection(): {
    transport: MirageTransport;
    reconnect?: () => MirageTransport;
} {
    const params = new URLSearchParams(window.location.search);
    const wsUrl = params.get('ws');
    if (params.get('transport') === 'bridge' || wsUrl !== null) {
        const url = wsUrl ?? DEFAULT_BRIDGE_URL;
        return {
            transport: new WsBridgeTransport(url),
            reconnect: () => new WsBridgeTransport(url),
        };
    }
    const eventsCapability = params.get('events') !== 'off';
    return { transport: createMockTransport({ eventsCapability }).transport };
}

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
    const { transport, reconnect } = createTransportSelection();
    const store = new HarnessStore(transport, { reconnect });
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
