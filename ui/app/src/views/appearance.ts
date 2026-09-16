/// 设置 → 外观：主题库（5 套内置主题卡片预览）+ 明暗模式（浅色 / 深色 /
/// 跟随系统）+ 核心组件占位规格展示（规范 §2.5 组件在 mock 界面可见）。

import { h } from '../dom.js';
import { activityCard, approvalCardPlaceholder, snapshotFramePlaceholder } from '../components/cards.js';
import { statusBadge, taskBadge } from '../components/status-badge.js';
import { themeManager } from '../theme/service.js';
import type { ResolvedThemeMode, ThemeModePref } from '../theme/schema.js';
import { BUILT_IN_THEMES } from '../theme/themes.js';

export function renderAppearance(): HTMLElement {
    const wrap = h('div', { class: 'stack', 'data-testid': 'appearance-view' });

    wrap.append(h(
        'section',
        { class: 'card' },
        h('h2', {}, '外观'),
        h('p', { class: 'muted small' }, '主题是同一语义 token 契约的值集：切换只替换颜色与圆角/阴影幅度，界面布局不变。'),
        h(
            'div',
            { class: 'theme-gallery', 'data-testid': 'theme-gallery' },
            ...BUILT_IN_THEMES.map((theme) => themeCard(theme.id, theme.name, theme.light)),
        ),
        modePicker(),
    ));

    wrap.append(h(
        'section',
        { class: 'card' },
        h('h2', {}, '组件规格（占位预览）'),
        h('p', { class: 'muted small' }, 'ApprovalCard / SnapshotFrame 为占位规格，交互随权限线（DEC-010）与观察线（M2）接线。'),
        h(
            'div',
            { class: 'stack' },
            activityCard({
                kindLabel: 'process.execute',
                name: '命令执行',
                badge: taskBadge('Completed'),
                duration: '1.2s',
                summary: '运行 ls -l 汇总工作目录清单（示例）',
                detail: snapshotFramePlaceholder(),
            }),
            approvalCardPlaceholder(),
            snapshotFramePlaceholder(),
            h(
                'div',
                { class: 'row-gap', 'data-testid': 'badge-map' },
                statusBadge('成功', 'success'),
                statusBadge('失败', 'destructive'),
                statusBadge('运行中', 'primary'),
                statusBadge('取消中', 'warning'),
                statusBadge('已暂停', 'info'),
                statusBadge('空闲', 'muted'),
            ),
        ),
    ));
    return wrap;
}

function themeCard(id: string, name: string, values: { background: string; card: string; primary: string; accent: string; border: string }): HTMLElement {
    const selected = themeManager.get().themeId === id;
    const card = h(
        'button',
        {
            class: `theme-card${selected ? ' theme-card-selected' : ''}`,
            type: 'button',
            'data-testid': 'theme-card',
            'data-theme-id': id,
            'aria-pressed': String(selected),
        },
        h(
            'div',
            { class: 'theme-card-preview' },
          h('span', { class: 'theme-swatch', style: `background:${values.background}; border-color:${values.border}` }),
            h('span', { class: 'theme-swatch', style: `background:${values.card}; border-color:${values.border}` }),
            h('span', { class: 'theme-swatch', style: `background:${values.accent}; border-color:${values.border}` }),
            h('span', { class: 'theme-swatch', style: `background:${values.primary}; border-color:${values.border}` }),
        ),
        h('span', { class: 'theme-card-name' }, name),
        selected ? h('span', { class: 'badge is-primary' }, '当前') : h('span', {}),
    );
    card.addEventListener('click', () => themeManager.setTheme(id));
    return card;
}

function modePicker(): HTMLElement {
    const state = themeManager.get();
    const option = (value: ThemeModePref, label: string): HTMLElement => {
        const button = h(
            'button',
            {
                class: `segmented-item${state.mode === value ? ' segmented-active' : ''}`,
                type: 'button',
                'aria-pressed': String(state.mode === value),
                'data-testid': `mode-${value}`,
            },
            label,
        );
        button.addEventListener('click', () => themeManager.setMode(value));
        return button;
    };
    const resolved: ResolvedThemeMode = themeManager.resolvedMode();
    return h(
        'div',
        { class: 'mode-picker' },
        h('span', { class: 'field-label' }, '明暗模式'),
        h('div', { class: 'segmented', role: 'group', 'aria-label': '明暗模式' },
            option('light', '浅色'),
            option('dark', '深色'),
            option('system', '跟随系统'),
        ),
        h('span', { class: 'muted small', 'data-testid': 'resolved-mode' }, `当前生效：${resolved === 'dark' ? '深色' : '浅色'}`),
    );
}
