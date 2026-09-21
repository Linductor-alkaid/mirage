// 点一个具体 agent 位置，触发 AgentDetailPanel，截图。
import { chromium } from '/usr/local/lib/node_modules/playwright/node_modules/playwright-core/index.mjs';

const OUT = '/workspace/mirage/docs/verification/m4-world';
const url = 'http://127.0.0.1:5173/#/world';
const log = (...a) => console.error('[shoot]', ...a);

const browser = await chromium.launch({
    executablePath: '/root/.cache/ms-playwright/chromium-1243/chrome-linux/chrome',
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-gl=swiftshader'],
});
const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 } });
const page = await ctx.newPage();
page.on('pageerror', (e) => log('pageerror:', e.message));

log('opening', url);
await page.goto(url, { waitUntil: 'networkidle', timeout: 60_000 });
await page.waitForSelector('[data-testid="world-canvas"]', { timeout: 20_000 });
await page.waitForSelector('[data-testid="world-agents"]', { timeout: 10_000 });
await page.waitForTimeout(3000);

// 先做俯览拿到全景
await page.click('text=俯览');
await page.waitForTimeout(800);

// 用 JS 在 canvas 里试多次点击：网格扫描 9x6 区域，找一个能选中 agent 的点
const canvas = await page.locator('[data-testid="world-canvas"]').boundingBox();
if (!canvas) {
    log('no canvas');
    process.exit(1);
}

const cols = 12, rows = 8;
let hit = null;
for (let r = 1; r < rows - 1; r += 1) {
    for (let c = 1; c < cols - 1; c += 1) {
        const x = canvas.x + canvas.width * (c / cols);
        const y = canvas.y + canvas.height * (r / rows);
        await page.mouse.click(x, y);
        await page.waitForTimeout(80);
        const detail = await page.locator('[data-testid="agent-detail"]').count();
        if (detail > 0) {
            hit = { x, y, c, r };
            break;
        }
    }
    if (hit) break;
}

if (hit) {
    log('hit at', hit);
    await page.waitForTimeout(500);
    const title = await page.textContent('[data-testid="agent-detail"] h2').catch(() => '');
    log('detail panel title:', title);
    const path = `${OUT}/shot-detail-open.png`;
    await page.screenshot({ path });
    log('saved', path);
} else {
    log('no agent hit found in grid scan');
    await page.screenshot({ path: `${OUT}/shot-detail-fail.png` });
}

await ctx.close();
await browser.close();
log('done');