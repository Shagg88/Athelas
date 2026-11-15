#!/usr/bin/env python3
"""imgdiff.py - compare the screenshots of two renderer runs (Vulkan stages, notes/vulkan-parity-plan.md section 5.1).

    python3 -I tools/imgdiff.py <dirA> <dirB> <outdir> [--frozen]

Pairs PNGs by name order (both runs take their shots at the same presented frames, XBE_SHOT_FRAMES), and writes for
each pair: PSNR (RGB), SSIM on luma (8x8 windows), the 99.9th-percentile absolute difference, the share of pixels off
by more than 8 and 32 (of 255), the worst 64x64 tile by SSIM, and a heat map (|difference| x 8). A report.md sums up;
the exit code is 1 when a pair fails the thresholds (--frozen: a still frame, PSNR >= 40 dB, >8 share <= 0.2 %, every
tile SSIM >= 0.95; otherwise a moving scene: whole-frame SSIM >= 0.9 and every tile >= 0.8).
"""
import os
import sys

import numpy as np
from PIL import Image


def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float64)


def luma(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def ssim_map(x, y, win=8):
    """SSIM over non-overlapping win x win windows (fast and good enough to rank tiles)."""
    h, w = (x.shape[0] // win) * win, (x.shape[1] // win) * win
    x = x[:h, :w].reshape(h // win, win, w // win, win)
    y = y[:h, :w].reshape(h // win, win, w // win, win)
    mx, my = x.mean(axis=(1, 3)), y.mean(axis=(1, 3))
    vx, vy = x.var(axis=(1, 3)), y.var(axis=(1, 3))
    cov = ((x - mx[:, None, :, None]) * (y - my[:, None, :, None])).mean(axis=(1, 3))
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    return ((2 * mx * my + c1) * (2 * cov + c2)) / ((mx ** 2 + my ** 2 + c1) * (vx + vy + c2))


def compare(a_path, b_path, heat_path, tile=64):
    a, b = load(a_path), load(b_path)
    if a.shape != b.shape:
        return {"error": f"sizes differ: {a.shape[1]}x{a.shape[0]} and {b.shape[1]}x{b.shape[0]}"}
    d = np.abs(a - b)
    mse = (d ** 2).mean()
    psnr = float("inf") if mse == 0 else 10 * np.log10(255 ** 2 / mse)
    dm = d.max(axis=2)
    sm = ssim_map(luma(a), luma(b))
    per = tile // 8
    worst, worst_at = 1.0, (0, 0)
    for ty in range(0, sm.shape[0], per):
        for tx in range(0, sm.shape[1], per):
            v = float(sm[ty:ty + per, tx:tx + per].mean())
            if v < worst:
                worst, worst_at = v, (tx * 8, ty * 8)
    Image.fromarray(np.clip(dm * 8, 0, 255).astype(np.uint8)).save(heat_path)
    return {
        "psnr": psnr,
        "ssim": float(sm.mean()),
        "p999": float(np.percentile(dm, 99.9)),
        "over8": float((dm > 8).mean() * 100),
        "over32": float((dm > 32).mean() * 100),
        "worst_tile": worst,
        "worst_at": worst_at,
        "size": f"{a.shape[1]}x{a.shape[0]}",
    }


def main():
    args = [x for x in sys.argv[1:] if not x.startswith("--")]
    frozen = "--frozen" in sys.argv
    if len(args) != 3:
        print(__doc__)
        return 2
    da, db, out = args
    os.makedirs(out, exist_ok=True)
    pa = sorted(f for f in os.listdir(da) if f.lower().endswith(".png"))
    pb = sorted(f for f in os.listdir(db) if f.lower().endswith(".png"))
    lines = [f"# {da} vs {db} ({'frozen' if frozen else 'moving'} thresholds)", "",
             "| # | A | B | size | PSNR dB | SSIM | worst tile (at) | p99.9 | >8 % | >32 % | result |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    failed = 0
    if len(pa) != len(pb):
        lines.insert(1, f"**Different shot counts: {len(pa)} and {len(pb)}; only the first {min(len(pa), len(pb))} compared.**")
        failed += 1
    for i, (fa, fb) in enumerate(zip(pa, pb)):
        r = compare(os.path.join(da, fa), os.path.join(db, fb), os.path.join(out, f"heat_{i:02d}.png"))
        if "error" in r:
            lines.append(f"| {i} | {fa} | {fb} | | | | | | | | FAIL: {r['error']} |")
            failed += 1
            continue
        if frozen:
            ok = r["psnr"] >= 40 and r["over8"] <= 0.2 and r["worst_tile"] >= 0.95
        else:
            ok = r["ssim"] >= 0.9 and r["worst_tile"] >= 0.8
        failed += 0 if ok else 1
        psnr = "inf" if r["psnr"] == float("inf") else f"{r['psnr']:.1f}"
        lines.append(f"| {i} | {fa} | {fb} | {r['size']} | {psnr} | {r['ssim']:.4f} | {r['worst_tile']:.3f} "
                     f"{r['worst_at']} | {r['p999']:.0f} | {r['over8']:.3f} | {r['over32']:.3f} | {'ok' if ok else 'FAIL'} |")
    lines += ["", f"{failed} failed" if failed else "all pairs pass"]
    with open(os.path.join(out, "report.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
