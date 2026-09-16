/// Mirage 控制台 — 入口。M1.5-04 以 mock transport 启动（无后端依赖）；
/// 真实传输（dev bridge）在 M1.5-05 接入，二者共享同一 MirageTransport 面。

import './styles.css';

import { createMockTransport } from '@mirage/contracts';

import { App } from './app.js';
import { Store } from './store.js';

function bootstrap(): void {
    const root = document.getElementById('app');
    if (root === null) {
        throw new Error('missing #app mount point');
    }
    // 开发期默认：mock transport。后续经 URL 参数选择真实传输
    // （?transport=devbridge，M1.5-05），不改变视图层。
    const { transport } = createMockTransport();
    const app = new App(transport, new Store());
    void app.start(root);
}

bootstrap();
