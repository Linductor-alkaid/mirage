/// 颜色工具：把 hex 字符串转换为 RGB 0–1 元组，供 Three.js 材质使用。

export interface Rgb {
    readonly r: number;
    readonly g: number;
    readonly b: number;
}

export function hexToRgb(hex: string): Rgb {
    let s = hex.trim();
    if (s.startsWith('#')) {
        s = s.slice(1);
    }
    if (s.length === 3) {
        s = s.split('').map((c) => c + c).join('');
    }
    if (s.length !== 6 && s.length !== 8) {
        return { r: 0, g: 0, b: 0 };
    }
    const r = parseInt(s.slice(0, 2), 16) / 255;
    const g = parseInt(s.slice(2, 4), 16) / 255;
    const b = parseInt(s.slice(4, 6), 16) / 255;
    return {
        r: Number.isFinite(r) ? r : 0,
        g: Number.isFinite(g) ? g : 0,
        b: Number.isFinite(b) ? b : 0,
    };
}