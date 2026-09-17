/// Markdown 渲染（assistant 消息体）：markdown-it + DOMPurify 消毒。
/// 工具调用卡 / 审批卡 / 快照卡不走 markdown 通道（自有组件结构）。

import DOMPurify from 'dompurify';
import MarkdownIt from 'markdown-it';

const md: MarkdownIt = new MarkdownIt({
    html: false,
    linkify: true,
    breaks: true,
});

/** 消毒并渲染为 HTML 字符串。渲染由 React dangerouslySetInnerHTML 承担。 */
export function renderMarkdown(source: string): string {
    const raw = md.render(source);
    return DOMPurify.sanitize(raw, {
        ALLOWED_TAGS: [
            'p', 'br', 'strong', 'em', 'del', 'code', 'pre', 'ul', 'ol', 'li',
            'blockquote', 'h1', 'h2', 'h3', 'h4', 'a', 'hr', 'table', 'thead',
            'tbody', 'tr', 'th', 'td', 'span',
        ],
        ALLOWED_ATTR: ['href', 'class', 'target', 'rel'],
    });
}
