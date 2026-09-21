#!/usr/bin/env python3
"""内存基线采样器（DEC-006 M3-06）。

用法：python3 measure/memory_sampler.py --pid <根进程PID> --interval-ms 500 \
        --out <结果JSON> [--label electron]

对根进程的整个进程树（/proc/<pid>/stat PPID 遍历）每 interval 求和 VmRSS，
直到根进程退出。输出：max_rss_kib、final_rss_kib、进程数峰值、采样序列。
口径：三壳一致（壳进程树 VmRSS 总和，含 GPU / renderer / utility 子进程）。
"""
import argparse
import json
import time


def read_tree(root):
    """返回 {pid: (ppid, rss_kib, name)} 全表。"""
    table = {}
    try:
        pids = [p for p in __import__("os").listdir("/proc") if p.isdigit()]
    except OSError:
        return table
    for pid in pids:
        try:
            with open(f"/proc/{pid}/stat", "rb") as f:
                stat = f.read()
            # comm 字段可含空格与括号：从最后一个 ')' 之后解析
            after = stat.rsplit(b")", 1)[1].split()
            ppid = int(after[1])
            rss_pages = int(after[21])  # field 24（1-based），0-indexed 21 after comm
            with open(f"/proc/{pid}/status", "rb") as f:
                for line in f:
                    if line.startswith(b"VmRSS:"):
                        rss_kib = int(line.split()[1])
                        break
                else:
                    rss_kib = 0
            name = stat.split(b"(", 1)[1].rsplit(b")", 1)[0].decode(errors="replace")
            table[int(pid)] = (ppid, rss_kib, name)
        except (OSError, IndexError, ValueError):
            continue
    return table


def tree_rss(root, table):
    if root not in table:
        return None, 0
    total = 0
    count = 0
    children = {}
    for pid, (ppid, _rss, _name) in table.items():
        children.setdefault(ppid, []).append(pid)
    stack = [root]
    while stack:
        pid = stack.pop()
        entry = table.get(pid)
        if not entry:
            continue
        total += entry[1]
        count += 1
        stack.extend(children.get(pid, []))
    return total, count


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--interval-ms", type=int, default=500)
    ap.add_argument("--out", required=True)
    ap.add_argument("--label", default="shell")
    args = ap.parse_args()

    series = []
    max_rss = 0
    max_procs = 0
    final_rss = None
    t0 = time.time()
    while True:
        table = read_tree(args.pid)
        rss, n = tree_rss(args.pid, table)
        if rss is None:
            break
        max_rss = max(max_rss, rss)
        max_procs = max(max_procs, n)
        final_rss = rss
        series.append({"t_ms": int((time.time() - t0) * 1000), "rss_kib": rss, "procs": n})
        time.sleep(args.interval_ms / 1000.0)

    out = {
        "label": args.label,
        "root_pid": args.pid,
        "interval_ms": args.interval_ms,
        "max_rss_kib": max_rss,
        "final_rss_kib": final_rss or 0,
        "max_processes": max_procs,
        "samples": series,
    }
    with open(args.out, "w") as f:
        json.dump(out, f, indent=1)
    print(f"{args.label}: max_rss={max_rss}KiB final={final_rss}KiB procs_max={max_procs}")


if __name__ == "__main__":
    main()
