// 拍时间序列快照，看 agent 是否真的在动。
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

// 点「俯览」按钮，把镜头拉远
await page.waitForTimeout(2000);
await page.click('text=俯览');
await page.waitForTimeout(500);

// 三个时间点：t≈2s, t≈10s, t≈25s
const shots = [
    { t: '2s', wait: 0 },
    { t: '10s', wait: 8000 },
    { t: '25s', wait: 15000 },
];
for (const s of shots) {
    if (s.wait > 0) await page.waitForTimeout(s.wait);
    const hud = {
        agents: await page.textContent('[data-testid="world-agents"]'),
        time: await page.textContent('[data-testid="world-time"]'),
    };
    const path = `${OUT}/time-${s.t}.png`;
    await page.screenshot({ path });
    log(`time=${s.t}`, hud, '→', path);
}

await ctx.close();
await browser.close();
log('done');