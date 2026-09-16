/// Mirage 控制台 — 入口。M1.5-04 以 mock transport 启动（无后端依赖）；
/// 真实传输（dev bridge）在 M1.5-05 接入，二者共享同一 MirageTransport 面。
/// M1.5-08：注入 L1/L2 token 样式表并启动主题管理（data-theme/data-mode
/// 挂载、持久化、跟随系统）。

import './styles.css';

import { createMockTransport } from '@mirage/contracts';

import { App } from './app.js';
import { Store } from './store.js';
import { PRIMITIVE_CSS } from './theme/primitives.js';
import { buildThemeStylesheet } from './theme/schema.js';
import { BUILT_IN_THEMES } from './theme/themes.js';
import { themeManager } from './theme/service.js';

function mountTokenStyles(): void {
    const style = document.createElement('style');
    style.id = 'mirage-theme-tokens';
    style.textContent = `${PRIMITIVE_CSS}\n${buildThemeStylesheet(BUILT_IN_THEMES)}`;
    document.head.append(style);
}

function bootstrap(): void {
    mountTokenStyles();
    themeManager.start();

    const root = document.getElementById('app');
    if (root === null) {
        throw new Error('missing #app mount point');
    }
    // 开发期默认：mock transport。后续经 URL 参数选择真实传输
    // （?transport=devbridge，M1.5-05），不改变视图层。
    const { transport } = createMockTransport();
    const app = new App(transport, new Store());
    // 主题切换即时生效：外观变更触发重渲染（属性挂载已在 manager 内完成）。
    themeManager.subscribe(() => app.renderNow(root));
    void app.start(root);
}

bootstrap();
