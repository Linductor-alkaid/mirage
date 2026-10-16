// Independent browser-rendered sources and DOM ground truth. No detector output
// is fed back as expected boxes. Usage: node FILE OUTPUT [PLAYWRIGHT_MODULE].
const fs = require('fs');
const path = require('path');
const { chromium } = require(process.argv[3] || 'playwright');
const output = path.resolve(process.argv[2]);
fs.mkdirSync(output, { recursive: true });
const base = `<meta charset="utf-8"><style>
*{box-sizing:border-box}body{margin:0;padding:32px;font:20px 'Noto Sans CJK SC',sans-serif;color:#172334;background:#f4f5f7}
h1{font-size:30px;margin:0 0 16px}h2{font-size:22px;margin:0 0 14px}p{margin:12px 0}
.row{display:flex;gap:28px}.card{background:white;border:1px solid #9ba5b5;border-radius:8px;padding:22px;flex:1}
button{font:inherit;padding:10px 24px;border:1px solid #748296;background:#e8eef9;border-radius:6px;margin:8px 12px 8px 0}
input{font:inherit;width:100%;padding:10px;margin:10px 0}td,th{border-bottom:1px solid #ccd2dd;padding:10px 25px 10px 0;text-align:left}
</style>`;
const light = base + `<h1>Mirage 视觉验收 / Review</h1><p>任务列表 · Task queue</p><div class="row">
<div class="card"><h2>新建任务 New task</h2><p>名称 Name</p><input value="报告 report-2026.pdf"><p>状态 Status: Ready</p><button>提交 Submit</button><button disabled>取消 Cancel</button></div>
<div class="card"><h2>运行状态</h2><table><tr><th>任务</th><th>进度</th></tr><tr><td>屏幕采集</td><td>100%</td></tr><tr><td>OCR 识别</td><td>85%</td></tr></table><p style="font-size:14px">small text: 0123456789 / 保存文件</p><p style="color:#aaa">低对比度提示 Low contrast</p></div></div>
<p>注意：编号只代表视觉区域，不保证控件可交互。</p><div style="display:flex;gap:25px"><button style="background:#1c61b8;color:white">保存 Save</button><button>设置 Settings</button><button>帮助 Help</button></div>`;
const dark = base + `<style>body{background:#151b23;color:#e8edf3;font-size:16px}.card{background:#202832;color:#e8edf3;border-color:#697584}pre{font:16px monospace;line-height:1.8}.muted{color:#565e69}</style>
<h1>Runtime diagnostics 运行诊断</h1><div class="row"><div class="card"><h2>service.cpp</h2><pre>auto result = executor.submit_auto(task);
if (!result.get().ok) {
    report_error("capture_failed");
}
// Stop producers before shutdown
executor.shutdown(true);</pre></div><div class="card"><h2>Session status</h2><p>Task ID: M7-20</p><p>Capture: 1280 x 800</p><p>Latency: 42 ms</p><p>任务已完成 / Done</p><p class="muted">低对比文字 low contrast text</p></div></div>
<p style="font-size:12px">tiny 12px: Cancel / 保存 / retry_count=0</p><p style="font-size:24px">Warning: stale reference @v12</p><p>Path: /home/user/project/report.txt</p>`;
const geometry = base + `<h1>Geometry candidates / 几何候选</h1><p>Shapes have no semantic labels.</p><canvas id="scene" width="930" height="450"></canvas>`;
async function truth(page) {
  return page.evaluate(() => {
    const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    const found=[]; let n;
    while((n=walker.nextNode())) {
      if(!n.textContent.trim() || ['SCRIPT','STYLE'].includes(n.parentElement.tagName)) continue;
      const range=document.createRange(); range.selectNode(n);
      for(const r of range.getClientRects()) if(r.width && r.height && r.y>=0 && r.bottom<=innerHeight)
        found.push({text:n.textContent.trim(),bounds:[r.x,r.y,r.width,r.height]});
    }
    for(const n of document.querySelectorAll('input')) {
      const r=n.getBoundingClientRect(); found.push({text:n.value,bounds:[r.x,r.y,r.width,r.height],kind:'input'});
    }
    return found;
  });
}
(async()=>{
  const browser=await chromium.launch({headless:true,
    ...(process.env.MIRAGE_REVIEW_CHROMIUM ? {executablePath:process.env.MIRAGE_REVIEW_CHROMIUM} : {})});
  try {
    const page=await browser.newPage({viewport:{width:1000,height:680},deviceScaleFactor:1});
    const manifest=[];
    async function save(name,source,extra={}) {
      await page.evaluate(()=>document.fonts.ready);
      await page.screenshot({path:path.join(output,name+'.png')});
      manifest.push({name,source,truth:await truth(page),...extra});
    }
    for(const [name,url] of [['public-example','https://example.com'],['public-python','https://www.python.org/downloads/']]) {
      await page.goto(url,{waitUntil:'networkidle',timeout:45000});
      await save(name,url);
    }
    for(const [name,html] of [['form-light',light],['terminal-dark',dark],['geometry',geometry]]) {
      fs.writeFileSync(path.join(output,name+'.html'),html);
      await page.setContent(html);
      if(name==='geometry') await page.evaluate(()=>{
        const c=document.querySelector('canvas').getContext('2d');
        c.fillStyle='#111';c.fillRect(40,55,110,80);c.strokeStyle='#111';c.lineWidth=3;c.strokeRect(210,55,150,80);
        c.beginPath();c.arc(475,95,44,0,Math.PI*2);c.stroke();
        c.beginPath();c.moveTo(600,135);c.lineTo(650,55);c.lineTo(700,135);c.closePath();c.stroke();
        c.beginPath();c.moveTo(60,260);c.lineTo(110,210);c.lineTo(160,260);c.stroke();
        c.strokeRect(250,225,230,55);c.fillStyle='#bbb';c.fillRect(540,225,130,55);
        c.fillStyle='#111';c.fillRect(760,55,8,75);c.fillRect(738,85,52,8);
      });
      await save(name,'local browser-rendered fixture');
    }
    const lines=['Submit task', 'The quick brown fox jumps',
      'This domain is for use in documentation examples',
      'auto result = executor.submit_auto(task);',
      'report_error("capture_failed");', '/home/user/project/report.txt',
      '这是用于验证长文本识别质量的中文句子，请完整保留每一个字。'];
    await page.setContent(base+'<h1>Recognition line-length control</h1>'+lines.map(t=>'<p style="font:20px monospace;margin:30px 0">'+t+'</p>').join(''));
    await save('line-lengths','local independent recognition controls',{expected_lines:lines});
    // Frame sequence is encoded to a real video by the Python runner and decoded
    // before ScreenProvider capture. The text changes and the target moves.
    for(let f=0;f<3;f++) {
      await page.setContent(base+`<h1>Video review / 动态画面</h1><p>Frame 00${f+1}</p><p>${['Preparing task 准备任务','Running task 执行任务','Task completed 任务完成'][f]}</p><canvas width="930" height="390"></canvas>`);
      await page.evaluate(f=>{const c=document.querySelector('canvas').getContext('2d');c.fillStyle='#174e94';c.fillRect(70+f*210,90,160,85);c.font='24px sans-serif';c.fillStyle='white';c.fillText('Target '+(f+1),85+f*210,142);},f);
      await save('video-'+f,'local generated video frame',{frame:f,canvas_text:'Target '+(f+1)});
    }
    await page.setContent('<style>body{margin:0;background:white}</style>');
    await save('blank','negative control: no text or objects');
    fs.writeFileSync(path.join(output,'corpus.json'),JSON.stringify(manifest,null,2));
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
