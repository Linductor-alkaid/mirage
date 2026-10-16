"""Package reviewed evidence into a standalone local HTML (no network/CDN)."""
import argparse
import base64
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--review', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--tuning', type=Path)
args = parser.parse_args()
review = json.loads(args.review.read_text())
root = Path(review['artifact_root'])
if args.tuning:
    review['tuning'] = json.loads(args.tuning.read_text())


def encoded(path):
    return 'data:image/png;base64,' + base64.b64encode(path.read_bytes()).decode()


for sample in review['samples']:
    sample['variants'] = {}
    if args.tuning:
        sample.update(review['tuning']['reviews'][sample['name']])
    for variant in ('samples', 'rec640', 'tuned-geometry'):
        folder = root / variant / sample['name']
        if (folder / 'report.json').exists():
            sample['variants'][variant] = {
                'raw': encoded(folder / 'raw.png'),
                'marked': encoded(folder / 'marked.png'),
                'data': json.loads((folder / 'report.json').read_text()),
            }

template = r'''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>Mirador 视觉复核 · 2026-10-16</title>
<style>
:root{font:16px system-ui,sans-serif;color:#202b36;background:#f6f7f9}*{box-sizing:border-box}
body{margin:0}header{padding:28px 32px;background:white;border-bottom:1px solid #ccd2dc}
h1{font-size:27px;margin:0 0 10px}p{line-height:1.7;margin:8px 0}main{display:grid;grid-template-columns:250px minmax(0,1fr);min-height:80vh}
nav{padding:18px 12px;border-right:1px solid #ccd2dc}nav button{display:block;width:100%;text-align:left;background:transparent;color:inherit;padding:12px;border:0;border-radius:5px;cursor:pointer;font:inherit}
nav button.active{background:#dce8f6;box-shadow:inset 3px 0 #245c9f}nav button span{float:right;font-size:12px;padding:3px 5px;border-radius:3px}
.Pass{color:#155a35;background:#e5f4e9}.Fail{color:#8a2820;background:#fbece8}article{padding:24px;min-width:0}h2{margin:0 0 12px}
label{margin-right:16px}select,button{font:inherit}select{padding:7px}#note{padding:14px 18px;background:white;border-left:3px solid #245c9f;margin:16px 0}
.images{display:grid;grid-template-columns:1fr 1fr;gap:14px}figure{margin:0;min-width:0;background:white;border:1px solid #cbd3dd}figcaption{padding:8px 12px;background:#edf1f5}img{display:block;width:100%;height:auto}img.zoom{width:auto;max-width:none}figure{overflow:auto}
table{width:100%;border-collapse:collapse;background:white;margin:18px 0}th,td{text-align:left;border-bottom:1px solid #dce0e7;padding:9px;vertical-align:top}th{background:#e9eef5}tbody tr{cursor:pointer}tbody tr:hover,tbody tr.selected{background:#e6f0fe}td.text{white-space:pre-wrap;word-break:break-word}
small{color:#526171}#regionPreview{max-width:100%;width:auto;border:2px solid #245c9f;margin-top:10px}.scope{font-size:13px;color:#4b5868}
@media(max-width:900px){main{display:block}nav{display:flex;overflow:auto}nav button{min-width:180px}.images{grid-template-columns:1fr}article{padding:14px}}
</style>
<header><h1>Mirador 标注视觉复核</h1><p>11 个场景 · 屏幕采集像素一致 11/11 · 编号映射 11/11 · 本轮质量通过 5/11</p>
<p class="scope">2026-10-16 · Linux / PP-OCRv5 mobile · Codex 逐图视觉复核；非人工签字。原图、标注和文本结果分开看，框正确不代表字正确。</p></header>
<main><nav id="nav" aria-label="测试场景"></nav><article><h2 id="title"></h2>
<label>配置 <select id="variant"><option value="samples">原始默认 rec_width=320</option><option value="rec640">诊断对照 rec_width=640</option><option value="tuned-geometry">产品：640 + 几何 16/4</option></select></label>
<label><input type="checkbox" id="zoom">按原始像素查看</label>
<p id="note"></p><p id="meta" class="scope"></p><div class="images"><figure><figcaption>ScreenProvider 原图</figcaption><img id="raw" alt="实际屏幕采集原图"></figure><figure><figcaption>mirador SoM 实际输出</figcaption><img id="marked" alt="正式管线的编号图"></figure></div>
<p><small>点击下表一行，查看相同原图上的区域裁剪。坐标是 Xvfb 回放的全局坐标；图像原点 (37, 53)。</small></p><canvas id="regionPreview" hidden></canvas>
<table><thead><tr><th>编号</th><th>来源</th><th>实际识别文字</th><th>置信度</th><th>区域 x,y,w,h</th></tr></thead><tbody id="rows"></tbody></table>
<p class="scope">边界：视频是编码后抽取的 3 帧，未验证实时 tracker、持续播放帧率或跨帧 ID。宿主图经过裁剪和像素回放，不代表原生 Wayland 输入坐标。未修改依赖、模型或产品默认配置。</p></article></main>
<script id="payload" type="application/json">__DATA__</script><script>
const data=JSON.parse(document.querySelector('#payload').textContent), nav=document.querySelector('#nav'), variant=document.querySelector('#variant');let selected=0;if(data.tuning)variant.value='tuned-geometry';
const types=['OCR','Detector','Template','Geometry'];
function show(){
 const sample=data.samples[selected];if(!sample.variants[variant.value])variant.value='samples';
 variant.options[1].disabled=!sample.variants.rec640;
 variant.options[2].disabled=!sample.variants['tuned-geometry'];
 const current=sample.variants[variant.value], report=current.data;
 document.querySelector('#title').textContent=sample.label+' · '+(variant.value==='tuned-geometry'?sample.product_verdict:sample.verdict);
 document.querySelector('#note').textContent=variant.value==='tuned-geometry'?sample.product_review:sample.review;
 document.querySelector('#meta').textContent=`${sample.pixels.join(' × ')} · ${report.regions.length} 个区域 · backend_calls=${report.backend_calls} · ${variant.value==='samples'?'原始默认':variant.value==='tuned-geometry'?'当前产品参数；仅本批样本结论':'宽度诊断配置'}`;
 document.querySelector('#raw').src=current.raw;document.querySelector('#marked').src=current.marked;
 document.querySelector('#regionPreview').hidden=true;
 const rows=document.querySelector('#rows');rows.replaceChildren();
 report.regions.forEach((r,i)=>{const tr=document.createElement('tr');
 const m=report.marks.find(m=>m.ref===r.ref);
 [m?m.number:i+1,types[r.source],r.text||'（无语义文字）',r.confidence.toFixed(3),r.bounds.join(', ')].forEach((t,j)=>{const td=document.createElement('td');td.textContent=t;if(j===2)td.className='text';tr.append(td)});
 tr.onclick=()=>{rows.querySelectorAll('tr').forEach(n=>n.classList.remove('selected'));tr.classList.add('selected');
 const canvas=document.querySelector('#regionPreview'),image=document.querySelector('#raw'),g=report.image_geometry;
 const x=Math.max(0,r.bounds[0]-g[0]-8),y=Math.max(0,r.bounds[1]-g[1]-8),w=Math.min(image.naturalWidth-x,r.bounds[2]+16),h=Math.min(image.naturalHeight-y,r.bounds[3]+16);
 canvas.width=w;canvas.height=h;canvas.getContext('2d').drawImage(image,x,y,w,h,0,0,w,h);canvas.hidden=false;};rows.append(tr)});
 [...nav.children].forEach((el,i)=>{el.classList.toggle('active',i===selected);const verdict=variant.value==='tuned-geometry'?data.samples[i].product_verdict:data.samples[i].verdict;el.lastChild.textContent=verdict;el.lastChild.className=verdict;});
}
data.samples.forEach((s,i)=>{const b=document.createElement('button');b.textContent=s.label;const badge=document.createElement('span');badge.className=s.verdict;badge.textContent=s.verdict;b.append(badge);b.onclick=()=>{selected=i;show()};nav.append(b)});
variant.onchange=show;document.querySelector('#zoom').onchange=e=>document.querySelectorAll('img').forEach(im=>im.classList.toggle('zoom',e.target.checked));show();
</script></html>'''
args.output.parent.mkdir(parents=True, exist_ok=True)
if args.tuning:
    template = template.replace('本轮质量通过 5/11', '基线质量 5/11 → 产品调优 7/11')
    template = template.replace('未修改依赖、模型或产品默认配置。', '已调优产品默认参数；未修改依赖或模型。')
    extra = '<section style="padding:32px"><h2>其他能力实测</h2><p>OCR 缓存：模型调用次数 1→1→2→3→4，命中/Refresh/版本/ROI 行为正确。模板索引：同位与移动命中，空白拒绝。融合：OCR+模板合并，重复帧保留 ID，新场景拒绝旧 generation。</p><p>变化检测：默认漏掉 16×16 修改，已知 ROI 比较可检出。全局位移有缩略图量化误差。实验跟踪默认 PSR=5 拒绝本例真匹配；PSR=4 找到 (+12,-8)，仍拒绝空白和不同文字。尚未启用为产品能力。</p><p><a href="https://github.com/Linductor-alkaid/mirador/issues/36">上游调优反馈 #36</a> · 小图标、其他语言、语义 detector 与深度 tracker 仍需独立验收。</p><div class="images">'
    for name, label in [('track-translated-psr4', '实验跟踪：移动后正确定位'), ('track-imposter-psr4', '实验跟踪：不同文字替换被拒绝')]:
        extra += '<figure><figcaption>' + label + '</figcaption><img alt="跟踪诊断" src="' + encoded(root / 'capabilities' / (name + '.png')) + '"></figure>'
    template = template.replace('</main>', '</main>' + extra + '</div></section>')
args.output.write_text(template.replace('__DATA__', json.dumps(review, ensure_ascii=False).replace('<', '\\u003c')))
print(args.output.resolve())
