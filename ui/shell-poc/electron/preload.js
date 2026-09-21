// Mirage shell PoC：Electron preload 桥（DEC-006 M3-06，非产品代码）。
// 与 harness/bridge-latency.html 的桥检测契约对应：window.mirageBridge.ping/report/done。
'use strict';

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('mirageBridge', {
  ping: (req) => ipcRenderer.invoke('poc-ping', req),
  report: (msg) => ipcRenderer.invoke('poc-report', msg),
  done: () => ipcRenderer.invoke('poc-done'),
});
