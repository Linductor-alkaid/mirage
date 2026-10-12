"""Opt-in synthetic screenshot -> Mirage tool -> Mira ImagePart -> real Messages model.

No user desktop pixels; no credentials in command arguments, logs, or artifacts.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import string
import subprocess
import tomllib
from urllib.parse import urlsplit
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--provider', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    cfg = tomllib.loads((args.provider / 'config.toml').read_text())
    provider = cfg['model_providers'][cfg['model_provider']]
    origin = urlsplit(provider['base_url'])
    if origin.hostname not in ('api.minimaxi.com', 'api.minimax.io'):
        raise RuntimeError('Restricted to the configured MiniMax provider')
    key = json.loads((args.provider / 'auth.json').read_text())['OPENAI_API_KEY']
    env = os.environ.copy()
    env.update(MIRAGE_IMAGE_LIVE='1', MIRAGE_IMAGE_KEY=key,
               MIRAGE_IMAGE_ORIGIN='https://' + origin.netloc, MIRAGE_IMAGE_MODEL=cfg['model'])
    args.output.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf', 48)
    cases = []
    for index, (color, rgb) in enumerate([('blue', '#2563eb'), ('green', '#16a34a')]):
        code = ''.join(secrets.choice(string.ascii_uppercase + string.digits) for _ in range(6))
        image = Image.new('RGB', (512, 256), 'white')
        draw = ImageDraw.Draw(image)
        draw.text((130, 20), code, fill='black', font=font)
        draw.rectangle((160, 120, 340, 230), fill=rgb)
        image.save(args.output / f'case-{index + 1}.png')
        raw = args.output / f'case-{index + 1}.bgra'
        raw.write_bytes(image.convert('RGBA').tobytes('raw', 'BGRA'))
        env['MIRAGE_IMAGE_RAW'] = str(raw.resolve())
        env['MIRAGE_IMAGE_PROMPT'] = (
            '你必须先实际调用 desktop.screen.capture 工具，参数为 '
            'source=display 和 source_id=d1。禁止在调用工具前输出回答。读取工具提供的图片， '
            '最后只输出 JSON，包含 code（顶部六位字符）和 color（矩形的英文颜色）。 '
            '答案只在图片中，禁止猜测或输出空值。工具可用，请直接调用，不要让用户提供图片。')
        result = subprocess.run([str(args.probe.resolve())], env=env, capture_output=True,
                                text=True, timeout=180)
        data = json.loads(result.stdout)
        answer = data.pop('answer', '')
        start, end = answer.find('{'), answer.rfind('}')
        actual = json.loads(answer[start:end + 1]) if start >= 0 else {}
        expected = dict(code=code, color=color)
        passed = (result.returncode == 0 and data['ok'] and actual == expected
                  and data['captures'] >= 1 and data['tool_calls'] >= 1
                  and data['supplied_input_items'] >= 1 and not data['supplied_input_dropped']
                  and not data['supply_degradations'])
        record = dict(case=index + 1, expected=expected, actual=actual, passed=bool(passed), **data)
        cases.append(record)
        (args.output / 'results.json').write_text(json.dumps(dict(
            model=cfg['model'], scope='synthetic screen via Mirage harness', cases=cases),
            ensure_ascii=False, indent=2))
        print(json.dumps(record, ensure_ascii=False), flush=True)
        if not passed:
            raise RuntimeError('Desktop image supply acceptance failed; sanitized evidence saved')


if __name__ == '__main__':
    main()
