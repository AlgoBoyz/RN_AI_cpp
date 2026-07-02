#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统计 AI 输出 dx/dy 随时间的分布，诊断"抖"的来源。
数据源: %USERPROFILE%/rn_ai/fusion.log 中的 [computeMove] 行。

输出:
  1. raw / out 的整体统计 (count, min, max, mean, abs-mean, std)
  2. 帧间 dx/dy 的"方向反转次数"和"一阶差分"统计 —— 抖的核心指标
  3. 按时间分桶 (每秒) 的 mean|dx|, mean|dy|, 反转率 —— 看时间分布
  4. target 坐标的帧间跳变 —— 排查是检测目标在抖还是 calc 放大抖动

用法: python analyze_ai_jitter.py [log_path]
"""
import re
import sys
import os
import statistics
from collections import defaultdict

LOG = os.path.join(os.environ.get("USERPROFILE", ""), "rn_ai", "fusion.log")
if len(sys.argv) > 1:
    LOG = sys.argv[1]

# 18:35:07 [computeMove] target=(293,284) pred=(293.0,284.3) branch=raw raw=(-248.830,-230.007) out=(-249,-230) fps=45 infer=3.7
PAT = re.compile(
    r"^(\d{2}):(\d{2}):(\d{2})\s+\[computeMove\]\s+"
    r"target=\(([-0-9.]+),([-0-9.]+)\)\s+"
    r"pred=\(([-0-9.]+),([-0-9.]+)\)\s+"
    r"branch=(\w+)\s+"
    r"raw=\(([-0-9.]+),([-0-9.]+)\)\s+"
    r"out=\((-?[0-9]+),(-?[0-9]+)\)\s+"
    r"fps=([0-9.]+)\s+infer=([0-9.]+)"
)

rows = []
with open(LOG, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        m = PAT.search(line)
        if not m:
            continue
        h, mi, s = int(m.group(1)), int(m.group(2)), int(m.group(3))
        t = h * 3600 + mi * 60 + s  # 秒级时间
        rows.append({
            "t": t,
            "tx": float(m.group(4)), "ty": float(m.group(5)),
            "px": float(m.group(6)), "py": float(m.group(7)),
            "branch": m.group(8),
            "rawx": float(m.group(9)), "rawy": float(m.group(10)),
            "outx": int(m.group(11)), "outy": int(m.group(12)),
            "fps": float(m.group(13)), "infer": float(m.group(14)),
        })

if not rows:
    print(f"no [computeMove] rows found in {LOG}")
    sys.exit(1)

print(f"=== 日志: {LOG} ===")
print(f"样本数: {len(rows)}")
print(f"时间: {rows[0]['t']//3600:02d}:{rows[0]['t']%3600//60:02d}:{rows[0]['t']%60:02d}"
      f" -> {rows[-1]['t']//3600:02d}:{rows[-1]['t']%3600//60:02d}:{rows[-1]['t']%60:02d}")
print(f"branch 分布: {dict((b, sum(1 for r in rows if r['branch']==b)) for b in set(r['branch'] for r in rows))}")
print()

def stats(name, vals):
    vals = list(vals)
    absv = [abs(v) for v in vals]
    print(f"  {name}: count={len(vals)} min={min(vals):.1f} max={max(vals):.1f} "
          f"mean={statistics.mean(vals):.1f} mean|v|={statistics.mean(absv):.1f} "
          f"std={statistics.pstdev(vals):.1f}")

print("=== 整体统计 ===")
stats("raw_x", (r["rawx"] for r in rows))
stats("raw_y", (r["rawy"] for r in rows))
stats("out_x", (r["outx"] for r in rows))
stats("out_y", (r["outy"] for r in rows))
stats("target_x", (r["tx"] for r in rows))
stats("target_y", (r["ty"] for r in rows))
print()

# 帧间差分 & 方向反转
def diffs_and_flips(vals):
    d = [vals[i+1] - vals[i] for i in range(len(vals)-1)]
    flips = 0
    for i in range(len(d)-1):
        if d[i] != 0 and d[i+1] != 0 and (d[i] > 0) != (d[i+1] > 0):
            flips += 1
    return d, flips

print("=== 帧间变化 (抖动核心指标) ===")
for name, key in [("raw_x","rawx"),("raw_y","rawy"),("out_x","outx"),("out_y","outy"),
                  ("target_x","tx"),("target_y","ty")]:
    vals = [r[key] for r in rows]
    d, flips = diffs_and_flips(vals)
    ad = [abs(x) for x in d]
    print(f"  {name}: Δ mean|Δ|={statistics.mean(ad):.1f} max|Δ|={max(ad):.1f} "
          f"反转={flips}/{len(d)} ({100*flips/max(1,len(d)):.1f}%)")
print()
print("注: 反转率高 = 相邻帧方向反复跳 = 抖。target 反转率高=检测在抖; raw 反转率高于 target=calc 放大抖动。")
print()

# 按秒分桶
print("=== 按秒分桶 (每秒一行) ===")
print(f"{'time':>8} {'n':>4} {'|rawx|':>7} {'|rawy|':>7} {'rawXflip%':>9} {'|Δtx|':>7} {'txFlip%':>8}")
buckets = defaultdict(list)
for r in rows:
    buckets[r["t"]].append(r)
for t in sorted(buckets):
    b = buckets[t]
    rawx = [r["rawx"] for r in b]
    rawy = [r["rawy"] for r in b]
    tx = [r["tx"] for r in b]
    _, rxf = diffs_and_flips(rawx)
    _, txf = diffs_and_flips(tx)
    n = len(b)
    print(f"{t//3600:02d}:{t%3600//60:02d}:{t%60:02d}  {n:>4} "
          f"{statistics.mean(abs(v) for v in rawx):>7.1f} "
          f"{statistics.mean(abs(v) for v in rawy):>7.1f} "
          f"{100*rxf/max(1,n-2):>8.1f} "
          f"{statistics.mean(abs(tx[i+1]-tx[i]) for i in range(len(tx)-1)) if len(tx)>1 else 0:>7.1f} "
          f"{100*txf/max(1,n-2):>7.1f}")
