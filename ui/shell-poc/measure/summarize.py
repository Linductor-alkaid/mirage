#!/usr/bin/env python3
"""汇总 shell PoC 结果 JSON（DEC-006 M3-06）。

用法：python3 measure/summarize.py <results/*.json> ...

从各壳的延迟结果 JSON（series stats + 原始 samples）出发：
1. 从原始样本重算百分位，与壳内统计交叉核对（偏差 > 1µs 报 mismatch）；
2. 输出 markdown 表格片段（stdout），供证据文档引用。

从原始样本重算 = 独立于壳内 JS 统计的第二口径，防止壳内统计缺陷冒充结果。
"""
import json
import sys


def percentiles(values):
    s = sorted(values)
    n = len(s)
    q = lambda p: s[min(n - 1, int(p * n))]
    mean = sum(s) / n
    var = sum((x - mean) ** 2 for x in s) / n
    return {
        "n": n,
        "mean_us": mean,
        "stdev_us": var ** 0.5,
        "min_us": s[0],
        "p50_us": q(0.50),
        "p90_us": q(0.90),
        "p95_us": q(0.95),
        "p99_us": q(0.99),
        "max_us": s[-1],
    }


def main(paths):
    rows = []
    mismatch = []
    for p in paths:
        with open(p, "rb") as f:
            d = json.load(f)
        shell = d.get("shell", "?")
        for label, series in sorted(d.get("series", {}).items()):
            raw = d.get("samples", {}).get(label) or []
            recomputed = percentiles(raw) if raw else None
            stats = series["stats"]
            if recomputed and abs(recomputed["p50_us"] - stats["p50_us"]) > 1.0:
                mismatch.append((p, label, stats["p50_us"], recomputed["p50_us"]))
            rows.append((shell, series.get("size", 0), stats, recomputed))
    if not rows:
        print("no series found", file=sys.stderr)
        return 1
    print("| 壳 | 载荷 | n | mean µs | stdev | min | p50 | p90 | p95 | p99 | max | 复算 p50 |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for shell, size, st, re_ in rows:
        rec = f"{re_['p50_us']:.1f}" if re_ else "-"
        st_round = {k: (round(v, 1) if isinstance(v, float) else v) for k, v in st.items()}
        print(f"| {shell} | {size}B | {st_round['n']} | {st_round['mean_us']} | "
              f"{st_round['stdev_us']} | {st_round['min_us']} | {st_round['p50_us']} | "
              f"{st_round['p90_us']} | {st_round['p95_us']} | {st_round['p99_us']} | "
              f"{st_round['max_us']} | {rec} |")
    if mismatch:
        print("\nMISMATCH（壳内统计 vs 原始样本重算，偏差 > 1µs）:", file=sys.stderr)
        for p, label, a, b in mismatch:
            print(f"  {p} {label}: in-page={a} recomputed={b}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
