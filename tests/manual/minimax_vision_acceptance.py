"""Opt-in paid vision acceptance through Mira's public Messages provider.

Reads the explicitly authorized local provider auth file into a child environment.
Creates two public synthetic images with random answers absent from the prompt.
No user screenshots, config/keys in output or credential command arguments.
This external test driver owns bounded child processes; it is not product runtime.
"""
import argparse
import json
import os
import secrets
import string
import subprocess
import tomllib
from pathlib import Path
from urllib.parse import urlsplit
from PIL import Image, ImageDraw, ImageFont


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--provider', type=Path, required=True)
    p.add_argument('--probe', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cfg = tomllib.loads((args.provider / 'config.toml').read_text())
    configured = cfg['model_providers'][cfg['model_provider']]
    origin = urlsplit(configured['base_url'])
    if origin.hostname not in ('api.minimaxi.com', 'api.minimax.io'):
        raise RuntimeError('This test is restricted to the authorized MiniMax provider')
    key = json.loads((args.provider / 'auth.json').read_text())['OPENAI_API_KEY']
    env = os.environ.copy()
    env.update(MIRA_MESSAGES_PROBE='1', MIRA_MESSAGES_KEY=key,
               MIRA_MESSAGES_ORIGIN='https://' + origin.netloc,
               MIRA_MESSAGES_PREFIX='/anthropic/v1', MIRA_MESSAGES_MODEL=cfg['model'])
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf', 48)
    colors = [('red', '#df2929'), ('blue', '#2563eb'), ('green', '#16a34a')]
    records = []
    for index in range(2):
        code = ''.join(secrets.choice(string.ascii_uppercase + string.digits) for _ in range(6))
        color, rgb = colors[index + 1]  # Distinct evidence; never included in the prompt.
        shape = ['circle', 'triangle'][index]
        image = Image.new('RGB', (512, 256), 'white')
        draw = ImageDraw.Draw(image)
        draw.text((140, 20), code, fill='black', font=font)
        draw.rectangle((40, 120, 180, 230), fill=rgb)
        if shape == 'circle':
            draw.ellipse((300, 120, 410, 230), fill='black')
        else:
            draw.polygon([(355, 110), (290, 230), (420, 230)], fill='black')
        path = args.output / f'vision-{index + 1}.png'
        image.save(path)
        env['MIRA_MESSAGES_IMAGE'] = str(path.resolve())
        env['MIRA_MESSAGES_PROMPT'] = ('Read the attached image. Return only a JSON object with '
                                       'code (the six-character string at the top), left_color '
                                       '(English color name of the left shape), and right_shape '
                                       '(English shape name of the black shape on the right). '
                                       'Do not infer from this prompt; inspect the pixels.')
        result = subprocess.run([str(args.probe.resolve())], env=env, capture_output=True,
                                text=True, timeout=100)
        data = json.loads(result.stdout)
        answer = data.get('answer', '')
        start, end = answer.find('{'), answer.rfind('}')
        actual = json.loads(answer[start:end + 1]) if start >= 0 else {}
        expected = dict(code=code, left_color=color, right_shape=shape)
        passed = (result.returncode == 0 and data.get('ok') and data.get('terminal_seen')
                  and data.get('image_bytes', 0) > 0 and actual == expected)
        record = dict(case=index + 1, expected=expected, actual=actual, passed=bool(passed),
                      **{k: v for k, v in data.items() if k != 'answer'})
        records.append(record)
        (args.output / 'results.json').write_text(json.dumps(dict(model=cfg['model'],
            protocol='anthropic.messages.v1', cases=records), ensure_ascii=False, indent=2))
        print(json.dumps(record, ensure_ascii=False), flush=True)
        if not passed:
            raise RuntimeError('MiniMax image acceptance failed; sanitized results retained')
    print('MiniMax vision: 2/2 images verified through Mira; codes and shapes matched')


if __name__ == '__main__':
    main()
