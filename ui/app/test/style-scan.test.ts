/// M1.5-08 验收：组件零私定颜色 —— styles.css 与 components/*.ts 不得出现
/// 十六进制色值或 rgb(/rgba( 字面量；views/appearance.ts 的主题预览 swatch
/// （style 属性内联展示主题值）属允许例外。

import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

const SRC = join(import.meta.dirname, '..', 'src');

/** 去掉允许形态后残留即违规：var(...)、color-mix(...)、transparent、
 * inherit、currentColor。repeating-linear-gradient 内也只允许 var/color-mix。 */
function findPrivateColors(source: string): string[] {
    // 先剥掉所有允许的颜色函数形态（成对括号内容不精确剥离，改为逐行检查注释之外的违规字面量）
    const withoutComments = source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
    const offenders: string[] = [];
    const hexRe = /#[0-9a-fA-F]{3,8}\b/g;
    const rgbRe = /rgba?\(/g;
    let m: RegExpExecArray | null;
    while ((m = hexRe.exec(withoutComments)) !== null) {
        offenders.push(`hex ${m[0]} @${withoutComments.slice(0, m.index).split('\n').length}`);
    }
    while ((m = rgbRe.exec(withoutComments)) !== null) {
        offenders.push(`rgb( @${withoutComments.slice(0, m.index).split('\n').length}`);
    }
    return offenders;
}

describe('component styles consume semantic tokens only', () => {
    it('styles.css has no private color literals', () => {
        const css = readFileSync(join(SRC, 'styles.css'), 'utf8');
        expect(findPrivateColors(css)).toEqual([]);
    });

    for (const file of readdirSync(join(SRC, 'components'))) {
        if (!file.endsWith('.ts')) {
            continue;
        }
        it(`components/${file} has no private color literals`, () => {
            const source = readFileSync(join(SRC, 'components', file), 'utf8');
            expect(findPrivateColors(source), file).toEqual([]);
        });
    }
});
