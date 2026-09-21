// Mirage shell PoC：Electron 壳承载（DEC-006 M3-06，非产品代码）。
//
// 用法：
//   npx electron . --page=harness --out=<结果JSON> [--url=<本地资产 index.html>]
//
// --page=harness  加载 harness/bridge-latency.html，按固定方法学测桥延迟，
//                 harness 通过 poc-report / poc-done 回传结果后退出。
// --page=ui       加载 ui/app/dist 生产资产（--url 指定 index.html），等待 8s 后
//                 抓取 DOM 就绪状态与 console/preload 错误，验证本地资产加载后退出。
'use strict';

const { app, BrowserWindow, ipcMain } = require('electron');
const fs = require('fs');
const path = require('path');

// 与 CEF PoC 相同的显示后端（X11/XWayland），保证两壳口径一致。
app.commandLine.appendSwitch('ozone-platform', 'x11');

function argValue(name, fallback) {
  const hit = process.argv.find((a) => a.startsWith(`--${name}=`));
  return hit ? hit.slice(name.length + 3) : fallback;
}

const page = argValue('page', 'harness');
const outPath = path.resolve(argValue('out', path.join(__dirname, '..', 'results', 'electron.json')));
const uiUrl = argValue('url', '');

const result = {
  shell: 'electron',
  page: page,
  versions: {
    electron: process.versions.electron,
    chrome: process.versions.chrome,
    node: process.versions.node,
  },
  meta: null,
  series: {},
  samples: {},
  ui_check: null,
  diagnostics: { console: [], preload_error: [], render_gone: null },
};

function writeOut() {
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, JSON.stringify(result, null, 1));
}

function nativeNowUs() {
  return Number(process.hrtime.bigint() / 1000n);
}

let win = null;
let finished = false;

function finish() {
  if (finished) return;
  finished = true;
  writeOut();
  if (win && !win.isDestroyed()) win.destroy();
  app.quit();
}

ipcMain.handle('poc-ping', (_event, req) => {
  const enterUs = nativeNowUs();
  let parsed = {};
  try { parsed = JSON.parse(req); } catch { /* 非法载荷按空对象回显 */ }
  const reply = {
    op: 'pong',
    i: parsed.i,
    size: parsed.size,
    enter_us: enterUs,
    leave_us: 0,
  };
  reply.leave_us = nativeNowUs();
  return JSON.stringify(reply);
});

ipcMain.handle('poc-report', (_event, raw) => {
  let msg;
  try { msg = JSON.parse(raw); } catch { return false; }
  if (msg.kind === 'meta') {
    result.meta = msg;
  } else if (msg.kind === 'series') {
    result.series[msg.label] = { size: msg.size, stats: msg.stats };
  } else if (msg.kind === 'samples') {
    const arr = result.samples[msg.label] || (result.samples[msg.label] = []);
    for (let i = 0; i < msg.values.length; i++) arr[msg.offset + i] = msg.values[i];
  }
  return true;
});

ipcMain.handle('poc-done', () => {
  finish();
  return true;
});

app.whenReady().then(() => {
  win = new BrowserWindow({
    width: 1280,
    height: 800,
    show: true,
    title: 'shell-poc-electron',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  win.webContents.on('console-message', (_e, level, message, line, source) => {
    result.diagnostics.console.push({ level, message: String(message).slice(0, 500), line, source });
  });
  win.webContents.on('preload-error', (_e, p, error) => {
    result.diagnostics.preload_error.push({ path: String(p), error: String(error).slice(0, 500) });
  });
  win.webContents.on('render-process-gone', (_e, details) => {
    result.diagnostics.render_gone = details;
  });

  const target = page === 'ui'
    ? (uiUrl ? 'file://' + path.resolve(uiUrl) : undefined)
    : 'file://' + path.join(__dirname, '..', 'harness', 'bridge-latency.html');
  if (!target) {
    result.ui_check = { error: 'missing --url for --page=ui' };
    finish();
    return;
  }
  win.loadURL(target);

  // 看门狗：harness 页面异常时不产生 done，避免进程无限滞留（结果以部分数据落盘）。
  if (page !== 'ui') {
    setTimeout(() => {
      if (!finished) {
        result.diagnostics.console.push({ level: 0, message: 'watchdog: harness timeout' });
        finish();
      }
    }, 90000);
  }

  if (page === 'ui') {
    win.webContents.once('did-finish-load', () => {
      setTimeout(async () => {
        try {
          result.ui_check = await win.webContents.executeJavaScript(
            'JSON.stringify({ready: document.readyState, title: document.title, ' +
            'rootChildren: ((document.getElementById("app")||document.getElementById("root")||{children:[]}).children.length), ' +
            'bodyChildren: document.body.children.length, ' +
            'protocol: location.protocol, ' +
            'assetScripts: document.querySelectorAll("script[src]").length, ' +
            'stylesheets: document.querySelectorAll("link[rel=stylesheet]").length})',
          ).then((s) => JSON.parse(s));
        } catch (e) {
          result.ui_check = { error: String(e).slice(0, 500) };
        }
        result.diagnostics.console = result.diagnostics.console.slice(-50);
        finish();
      }, 8000);
    });
  }
});

app.on('window-all-closed', () => {
  writeOut();
  app.quit();
});
