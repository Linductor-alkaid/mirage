/// 会话页（harness 主界面）：签派栏 + 线程流 + Composer + 观察台。
/// 线程自动跟随：用户贴近底部时新消息自动滚入；上滚即交还控制权。
/// 数据面全部为契约路径（DEC-025）：会话面投影 + 任务快照镜像。

import { useEffect, useMemo, useRef, useState } from 'react';

import { useHarness } from '../../hooks.js';
import { SessionsSidebar } from './SessionsSidebar.js';
import { MessageView } from './messages.js';
import { Composer } from './Composer.js';
import { Observer } from './Observer.js';

function EmptyHero({ onNew, disabled }: { onNew(): void; disabled: boolean }): React.ReactElement {
    return (
        <div className="empty-hero">
            <span className="big">就绪</span>
            <p style={{ maxWidth: 460 }}>
                这里是任务控制大厅。选择左侧会话继续，或新建一场：执行模式把目标提交给
                Agent，步骤、观察与任务全程可见；对话模式在模型循环接入后开放。
            </p>
            <div style={{ display: 'flex', gap: 8 }}>
                <button type="button" className="btn btn-primary" onClick={onNew} disabled={disabled}>
                    新建会话
                </button>
            </div>
        </div>
    );
}

export function ChatPage({ sidebarCollapsed }: { sidebarCollapsed: boolean }): React.ReactElement {
    const { state, newSession } = useHarness();
    const sessionId = state.route.view === 'chat' ? state.route.sessionId : undefined;
    const [observerOpen, setObserverOpen] = useState(true);
    const threadRef = useRef<HTMLDivElement>(null);
    const followRef = useRef(true);

    const session = state.sessions.find((s) => s.id === sessionId);
    const messages = useMemo(
        () => (sessionId !== undefined ? state.messages.get(sessionId) ?? [] : []),
        [state.messages, sessionId],
    );

    useEffect(() => {
        followRef.current = true;
        const box = threadRef.current;
        if (box !== null) {
            box.scrollTop = box.scrollHeight;
        }
    }, [sessionId]);

    useEffect(() => {
        if (!followRef.current) {
            return;
        }
        const box = threadRef.current;
        if (box !== null) {
            box.scrollTop = box.scrollHeight;
        }
    }, [messages.length]);

    if (sessionId === undefined || session === undefined) {
        return (
            <div className="chat">
                {!sidebarCollapsed && <SessionsSidebar />}
                <div className="thread-col">
                    <EmptyHero onNew={() => newSession()} disabled={!state.sessionsSupported} />
                </div>
            </div>
        );
    }

    const truncated = state.historyTruncated.get(sessionId) === true;

    return (
        <div className="chat" data-testid="chat-page">
            {!sidebarCollapsed && <SessionsSidebar />}
            <div className="thread-col">
                <div
                    ref={threadRef}
                    className="thread"
                    onScroll={() => {
                        const box = threadRef.current;
                        if (box !== null) {
                            followRef.current = box.scrollHeight - box.scrollTop - box.clientHeight < 80;
                        }
                    }}
                >
                    <div className="thread-inner">
                        {truncated && (
                            <div className="system-line is-warn">
                                更早的会话记录未加载（session.history 窗口截断）。
                            </div>
                        )}
                        {messages.map((m) => (
                            <MessageView key={m.id} m={m} />
                        ))}
                    </div>
                </div>
                <Composer key={sessionId} sessionId={sessionId} />
            </div>
            <Observer open={observerOpen} onToggle={() => setObserverOpen(false)} />
        </div>
    );
}
