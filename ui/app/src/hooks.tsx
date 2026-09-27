/// React 绑定：store 上下文 + useSyncExternalStore 订阅 + 常用派生 hook。

import { createContext, useContext, useEffect, useState, useSyncExternalStore } from 'react';

import type { HarnessState, HarnessStore } from './state/store.js';

const HarnessContext = createContext<HarnessStore | null>(null);

export function HarnessProvider({
    store,
    children,
}: {
    store: HarnessStore;
    children: React.ReactNode;
}): React.ReactElement {
    return <HarnessContext.Provider value={store}>{children}</HarnessContext.Provider>;
}

export function useStore(): HarnessStore {
    const store = useContext(HarnessContext);
    if (store === null) {
        throw new Error('HarnessProvider missing');
    }
    return store;
}

export function useHarness(): { state: HarnessState } & ReturnType<HarnessStore['actions']> {
    const store = useStore();
    const state = useSyncExternalStore(store.subscribe, store.get);
    return { state, ...store.actions() };
}

/** 注入的墙钟时间源（M5-01 react-hooks/purity 挂账的清理形态）：渲染期
 * 不读 Date.now——时间由定时器回调推进，`nowMs` 只是状态值。粒度 30s，
 * 恰好覆盖 relativeTime 的分钟级提示；组件卸载即停表。 */
export function useNow(): number {
    const [nowMs, setNowMs] = useState((): number => Date.now());
    useEffect(() => {
        const timer = window.setInterval(() => setNowMs(Date.now()), 30_000);
        return () => window.clearInterval(timer);
    }, []);
    return nowMs;
}
