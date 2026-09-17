/// React 绑定：store 上下文 + useSyncExternalStore 订阅 + 常用派生 hook。

import { createContext, useContext, useSyncExternalStore } from 'react';

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
