// @vitest-environment jsdom
/// ChatSimulator（chat 模式本地模拟）：思考块 → 分片流 → onAssistantDone
/// 的完整收敛，以及取消语义（fake timers 确定性推进，无真实等待）。

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { ChatSimulator } from '../src/state/harness-mock.js';
import type { ChatTurnEvents } from '../src/state/harness-mock.js';

// 与 src/state/harness-mock.ts 的语料一致（mock 域合同；语料变更时此测试同步更新）。
const FALLBACK_REPLY =
    '收到。当前是**对话模式**——我可以直接讨论、查笔记、出草稿；不会产生任何桌面动作。需要我实际操作桌面的话，切到执行模式再提交。';
const PERMISSION_REPLY =
    'Mirage 的权限判定在 **Provider × 范围** 矩阵上逐格取 `allow / ask / deny`，未命中的格子回退到默认模式。批准流（ask）会把请求送进批准中心，不打断你正在看的视图。';

type EventLogEntry =
    | { type: 'thinking'; text: string }
    | { type: 'chunk'; text: string }
    | { type: 'done' };

function makeSim(): { sim: ChatSimulator; log: EventLogEntry[]; doneIds: string[] } {
    const log: EventLogEntry[] = [];
    const doneIds: string[] = [];
    const events: ChatTurnEvents = {
        onThinking: (_sessionId, _messageId, block) => {
            log.push({ type: 'thinking', text: block.text });
        },
        onAssistantChunk: (_sessionId, _messageId, chunk) => {
            log.push({ type: 'chunk', text: chunk });
        },
        onAssistantDone: (_sessionId, messageId) => {
            log.push({ type: 'done' });
            doneIds.push(messageId);
        },
    };
    return { sim: new ChatSimulator(events), log, doneIds };
}

/** 与 ChatSimulator.begin 的思考延时公式一致（900 + min(len,120)*8 ms）。 */
function thinkingDelayOf(userText: string): number {
    return 900 + Math.min(userText.length, 120) * 8;
}

function chunksOf(log: readonly EventLogEntry[]): string {
    return log
        .filter((entry): entry is Extract<EventLogEntry, { type: 'chunk' }> => entry.type === 'chunk')
        .map((entry) => entry.text)
        .join('');
}

beforeEach(() => {
    vi.useFakeTimers();
});

afterEach(() => {
    vi.useRealTimers();
});

describe('ChatSimulator full turn', () => {
    it('begin -> thinking -> chunked stream -> done, converging to the full reply', async () => {
        const { sim, log, doneIds } = makeSim();
        const userText = '帮我看看今天的安排';
        const messageId = sim.begin('s-1', userText);

        expect(typeof messageId).toBe('string');
        expect(sim.busy()).toBe(true);
        expect(log).toEqual([]); // 思考期不产生任何回调

        await vi.advanceTimersByTimeAsync(thinkingDelayOf(userText));
        expect(log[0]?.type).toBe('thinking');
        expect(chunksOf(log).length).toBeGreaterThan(0); // 流式首片同步到达

        await vi.runAllTimersAsync();

        // done 恰好一次、携带 begin 返回的消息 id，且是最后一个事件
        expect(doneIds).toEqual([messageId]);
        expect(log[log.length - 1]?.type).toBe('done');
        // 分片拼接收敛为完整回复
        expect(chunksOf(log)).toBe(FALLBACK_REPLY);
        const chunkCount = log.filter((entry) => entry.type === 'chunk').length;
        expect(chunkCount).toBeGreaterThan(3); // 确实是分片流，不是一次性整段
        expect(sim.busy()).toBe(false);

        // done 之后不再有输出
        const total = log.length;
        await vi.advanceTimersByTimeAsync(10_000);
        expect(log).toHaveLength(total);
    });

    it('keyword reply corpus is routed deterministically (权限 → permission reply)', async () => {
        const { sim, log } = makeSim();
        const userText = '桌面权限是怎么判定的？';
        sim.begin('s-1', userText);
        await vi.advanceTimersByTimeAsync(thinkingDelayOf(userText));
        const thinking = log.find((entry) => entry.type === 'thinking');
        expect(thinking && thinking.type === 'thinking' ? thinking.text : '').toContain('DEC-010');
        await vi.runAllTimersAsync();
        expect(chunksOf(log)).toBe(PERMISSION_REPLY);
    });
});

describe('ChatSimulator cancel', () => {
    it('cancel mid-stream still fires onAssistantDone once and stops all further chunks', async () => {
        const { sim, log, doneIds } = makeSim();
        const userText = '随便聊聊';
        const messageId = sim.begin('s-2', userText);
        await vi.advanceTimersByTimeAsync(thinkingDelayOf(userText));
        await vi.advanceTimersByTimeAsync(50); // 再流出若干片
        const chunkCountAtCancel = log.filter((entry) => entry.type === 'chunk').length;
        expect(chunkCountAtCancel).toBeGreaterThanOrEqual(1);

        sim.cancel();
        expect(sim.busy()).toBe(false);
        // 以已发内容收尾：done 仍被调用，且已发内容是完整文本前缀
        expect(doneIds).toEqual([messageId]);
        expect(FALLBACK_REPLY.startsWith(chunksOf(log))).toBe(true);

        await vi.runAllTimersAsync();
        expect(log.filter((entry) => entry.type === 'chunk')).toHaveLength(chunkCountAtCancel);
        expect(doneIds).toHaveLength(1);
    });

    it('cancel during thinking clears the timer: no callbacks ever fire', async () => {
        const { sim, log } = makeSim();
        sim.begin('s-3', '在吗');
        await vi.advanceTimersByTimeAsync(100); // 未到思考延时
        sim.cancel();
        expect(sim.busy()).toBe(false);
        expect(log).toEqual([]);
        await vi.runAllTimersAsync();
        expect(log).toEqual([]); // 定时器已被清理
    });

    it('a cancelled simulator accepts a new turn', async () => {
        const { sim, log, doneIds } = makeSim();
        const firstText = '第一条';
        const firstId = sim.begin('s-4', firstText);
        await vi.advanceTimersByTimeAsync(thinkingDelayOf(firstText) + 30);
        sim.cancel(); // 流中取消：第一条以已发内容收尾（done 已发）
        expect(doneIds).toEqual([firstId]);
        expect(sim.busy()).toBe(false);
        log.length = 0; // 只考察第二轮的收敛
        doneIds.length = 0;

        const userText = '第二条';
        const secondId = sim.begin('s-4', userText);
        expect(sim.busy()).toBe(true);
        await vi.runAllTimersAsync();
        expect(doneIds).toEqual([secondId]);
        expect(chunksOf(log)).toBe(FALLBACK_REPLY);
        expect(sim.busy()).toBe(false);
    });
});
