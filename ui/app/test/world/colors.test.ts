/// 颜色工具测试。

import { describe, expect, it } from 'vitest';

import { hexToRgb } from '../../src/world/projector/colors.js';

describe('hexToRgb', () => {
    it('parses 6-digit hex', () => {
        expect(hexToRgb('#FF8800')).toEqual({ r: 1, g: 136 / 255, b: 0 });
        expect(hexToRgb('00ff00')).toEqual({ r: 0, g: 1, b: 0 });
    });
    it('parses 3-digit hex by expanding', () => {
        expect(hexToRgb('#f80')).toEqual({ r: 1, g: 136 / 255, b: 0 });
    });
    it('returns black on malformed input', () => {
        expect(hexToRgb('zz')).toEqual({ r: 0, g: 0, b: 0 });
        expect(hexToRgb('')).toEqual({ r: 0, g: 0, b: 0 });
    });
});