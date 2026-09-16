/// Tiny DOM helpers for the vanilla-TS views (component framework choice is
/// deliberately deferred — M1.5-04 keeps the view layer dependency-free).

export type Child = Node | string | null | undefined | false;

export function h<K extends keyof HTMLElementTagNameMap>(
    tag: K,
    attrs: Record<string, string | boolean | ((event: Event) => void)> = {},
    ...children: Child[]
): HTMLElementTagNameMap[K] {
    const element = document.createElement(tag);
    for (const [key, value] of Object.entries(attrs)) {
        if (typeof value === 'function') {
            element.addEventListener(key.replace(/^on/, '').toLowerCase(), value as EventListener);
        } else if (typeof value === 'boolean') {
            if (value) {
                element.setAttribute(key, '');
            }
        } else if (key === 'class') {
            element.className = value;
        } else {
            element.setAttribute(key, value);
        }
    }
    append(element, children);
    return element;
}

function append(parent: HTMLElement, children: Child[]): void {
    for (const child of children) {
        if (child === null || child === undefined || child === false) {
            continue;
        }
        parent.append(typeof child === 'string' ? document.createTextNode(child) : child);
    }
}

/** Replaces `container`'s children with the given nodes. */
export function render(container: HTMLElement, ...children: Child[]): void {
    container.replaceChildren();
    append(container, children);
}
