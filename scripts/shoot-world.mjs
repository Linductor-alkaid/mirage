// 截 Mirage World View 的渲染快照（直接调 global playwright-core，绕开 python 版本差）
import { chromium } from '/usr/local/lib/node_modules/playwright/node_modules/playwright-core/index.mjs';
import { writeFileSync } from 'node:fs';
import { mkdirSync } from 'node:fs';

const OUT = '/workspace/mirage/docs/verification/m4-world';
mkdirSync(OUT, { recursive: true });

const url = process.argv[2] || 'http://127.0.0.1:5173/#/world';
const log = (...a) => console.error('[shoot]', ...a);

const browser = await chromium.launch({
    executablePath: '/root/.cache/ms-playwright/chromium-1243/chrome-linux/chrome',
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist'],
});
const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 } });
const page = await ctx.newPage();
page.on('console', (m) => log('console:' + m.type(), m.text()));
page.on('pageerror', (e) => log('pageerror:', e.message));

log('opening', url);
await page.goto(url, { waitUntil: 'networkidle', timeout: 60_000 });
await page.waitForSelector('[data-testid="world-canvas"]', { timeout: 20_000 });
await page.waitForSelector('[data-testid="world-agents"]', { timeout: 10_000 });
await page.waitForTimeout(3500);

const hud1 = {
    agents: await page.textContent('[data-testid="world-agents"]'),
    fps: await page.textContent('[data-testid="world-fps"]'),
    time: await page.textContent('[data-testid="world-time"]'),
};
log('hud@3.5s', hud1);
const p1 = `${OUT}/shot-overview.png`;
await page.screenshot({ path: p1 });
log('saved', p1);

// 等 8 秒，期望出现 collaboration + status badge
await page.waitForTimeout(8_000);
const hud2 = {
    agents: await page.textContent('[data-testid="world-agents"]'),
    fps: await page.textContent('[data-testid="world-fps"]'),
    time: await page.textContent('[data-testid="world-time"]'),
};
log('hud@11.5s', hud2);
const p2 = `${OUT}/shot-mid.png`;
await page.screenshot({ path: p2 });
log('saved', p2);

// 点击 canvas 中心测试 picking
const box = await page.locator('[data-testid="world-canvas"]').boundingBox();
if (box) {
    await page.mouse.click(box.x + box.width * 0.5, box.y + box.height * 0.5);
    await page.waitForTimeout(800);
    const detailCount = await page.locator('[data-testid="agent-detail"]').count();
    log('click-center: agent-detail count =', detailCount);
    const p3 = `${OUT}/shot-selected.png`;
    await page.screenshot({ path: p3 });
    log('saved', p3);
    if (detailCount > 0) {
        const title = await page.textContent('[data-testid="agent-detail"] h2');
        log('panel title:', title);
    }
}

// 再点一个偏左下角的位置
if (box) {
    await page.mouse.click(box.x + box.width * 0.35, box.y + box.height * 0.65);
    await page.waitForTimeout(800);
    const p4 = `${OUT}/shot-selected-2.png`;
    await page.screenshot({ path: p4 });
    log('saved', p4);
}

await ctx.close();
await browser.close();
log('done');