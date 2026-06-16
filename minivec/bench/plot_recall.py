#!/usr/bin/env python3
"""画 recall–QPS 曲线(每条 M 一条线)。

run_sweep 产出 CSV 后用它出图:
    python3 bench/plot_recall.py [results.csv] [out.png]
默认读 bench/results.csv,存 bench/recall_curve.png。
依赖: matplotlib (pip install matplotlib)。CSV 解析用标准库。

读法:同一条 M 曲线上,ef_search 越大 → recall 越高、QPS 越低(越靠右下)。
理想索引在"右上角"(高 recall + 高 QPS)。这张图就是 recall–latency 三角的可视化。
"""
import csv
import sys
from collections import defaultdict


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "bench/results.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "bench/recall_curve.png"

    # 按 M 分组: M -> [(recall, qps, ef), ...]
    by_m = defaultdict(list)
    with open(src, newline="") as f:
        for row in csv.DictReader(f):
            by_m[int(row["M"])].append(
                (float(row["recall"]), float(row["qps"]), int(row["ef_search"]))
            )

    if not by_m:
        print(f"{src} 没有数据行 —— 先填 sweep.c 的扫描循环再跑 run_sweep")
        return
    if all(r < 0 for pts in by_m.values() for r, _, _ in pts):
        print("recall 全是 -1 —— 先填 bench/metrics.c 的 recall_at_k / percentile")
        return

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.figure(figsize=(7, 5))
    for m in sorted(by_m):
        pts = sorted(by_m[m], key=lambda t: t[0])   # 按 recall 排序连线
        recalls = [p[0] for p in pts]
        qpss = [p[1] for p in pts]
        plt.plot(recalls, qpss, marker="o", label=f"M={m}")
        for r, q, ef in pts:
            plt.annotate(f"ef={ef}", (r, q), fontsize=7,
                         textcoords="offset points", xytext=(4, 4))

    plt.xlabel("recall@10")
    plt.ylabel("QPS (single-thread)")
    plt.title("MiniVec HNSW: recall vs QPS")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out, dpi=120)
    print(f"saved -> {out}")


if __name__ == "__main__":
    main()
