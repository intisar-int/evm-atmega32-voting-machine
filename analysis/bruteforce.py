"""Brute-force analysis of the 4-digit admin PIN, driven by the reference model.

Attacker model (stated assumptions, all adjustable below):
  * knows the PIN is 4 decimal digits (10^4 candidates) and guesses without repetition;
  * has physical access to the keypad only (no side channels, no flash readout);
  * needs T_KEY seconds per key press+release.

Two attack surfaces exist in firmware/evm_final.c:
  (a) pre-session admin login  - unlimited wall-clock time, 1.5 s penalty per miss;
  (b) in-session pause prompt  - only while the (~119 s) voting window is open.

For each lock-out policy the script reports E[time to recover the PIN] on (a)
and P(recover PIN) inside the voting window on (b). Policy "firmware" is the
shipped behaviour (no lock-out). Numbers come from running evm_model.py, not
from a closed-form guess, so they include the 1.5 s delay windows, key loss
during busy time, and the Timer1 phase.

Usage:  python3 analysis/bruteforce.py   (writes docs/figures/bruteforce_analysis.png)
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "model"))
from evm_model import ENDED, EVM, Config, boot_to_voting  # noqa: E402

SPACE = 10_000
POLICIES = {
    "firmware (none)": dict(lockout_after=0),
    "3 tries, 30 s x2^n, cap 15 min": dict(lockout_after=3, lockout_base_s=30, lockout_cap_s=900),
    "3 tries, 60 s x2^n, cap 1 h": dict(lockout_after=3, lockout_base_s=60, lockout_cap_s=3600),
}


def login_attack_times(cfg: Config, t_key_ms: int):
    """T(k): wall-clock ms at which the verdict on guess k is produced."""
    m = EVM(cfg)
    m.settle()
    times = []
    for _ in range(SPACE):
        m.settle()
        for i in range(4):
            m.press("0")                       # always-wrong guess; only timing matters
            if i < 3:
                m.advance(t_key_ms)
        times.append(m.now_ms)
        m.advance(t_key_ms)
    return times


def window_attack_attempts(cfg: Config, t_key_ms: int):
    """Attempts the attacker can complete through the pause prompt while voting is open."""
    m = boot_to_voting(cfg)
    attempts = 0
    while m.phase != ENDED and attempts < 5000:
        m.settle()
        if m.phase == ENDED:
            break
        m.press("#")
        m.advance(t_key_ms)
        for i in range(4):
            m.settle() if m.busy else None
            if not m.press("0"):
                break
            if m.phase != ENDED:
                m.advance(t_key_ms)
        attempts += 1
    return attempts


def run(t_key_s: float):
    t_key_ms = int(t_key_s * 1000)
    out = {}
    for name, kw in POLICIES.items():
        cfg = Config(**kw)
        T = login_attack_times(cfg, t_key_ms)
        mean_s = sum(T) / len(T) / 1000
        worst_s = T[-1] / 1000
        n = window_attack_attempts(Config(**kw), t_key_ms)
        out[name] = dict(mean_hours=mean_s / 3600, worst_hours=worst_s / 3600,
                         window_attempts=n, window_p=n / SPACE)
    return out


if __name__ == "__main__":
    results = {f"t_key={t}s": run(t) for t in (0.15, 0.3, 0.6)}
    (ROOT / "analysis" / "bruteforce_results.json").write_text(json.dumps(results, indent=2))
    for tk, res in results.items():
        print(f"\n{tk}")
        print(f"{'policy':36s} {'E[time] (h)':>12s} {'worst (h)':>11s} {'attempts/119s':>14s} {'P(win)':>8s}")
        for name, r in res.items():
            print(f"{name:36s} {r['mean_hours']:12.2f} {r['worst_hours']:11.2f} "
                  f"{r['window_attempts']:14d} {100*r['window_p']:7.3f}%")

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    base = results["t_key=0.3s"]
    names = list(base)
    fig, ax = plt.subplots(1, 2, figsize=(11, 4.2))
    mean_h = [base[n]["mean_hours"] for n in names]
    ax[0].bar(range(len(names)), mean_h, color=["#c0504d", "#4f81bd", "#2e7d32"])
    ax[0].set_yscale("log")
    ax[0].set_xticks(range(len(names)))
    ax[0].set_xticklabels([n.replace(", ", "\n", 1) for n in names], fontsize=8)
    ax[0].set_ylabel("expected time to recover admin PIN [hours, log]")
    ax[0].set_ylim(top=max(mean_h) * 4)
    ax[0].set_title("(a) Pre-session login attack")
    for i, v in enumerate(mean_h):
        ax[0].text(i, v * 1.15, f"{v:.1f} h" if v < 100 else f"{v/24:.0f} d", ha="center", fontsize=9)
    p = [100 * base[n]["window_p"] for n in names]
    ax[1].bar(range(len(names)), p, color=["#c0504d", "#4f81bd", "#2e7d32"])
    ax[1].set_xticks(range(len(names)))
    ax[1].set_xticklabels([n.replace(", ", "\n", 1) for n in names], fontsize=8)
    ax[1].set_ylabel("P(PIN recovered inside voting window) [%]")
    ax[1].set_ylim(top=max(p) * 1.15)
    ax[1].set_title("(b) In-session pause-prompt attack")
    for i, v in enumerate(p):
        ax[1].text(i, v + max(p) * 0.02, f"{v:.3f}%", ha="center", fontsize=9)
    fig.suptitle("Admin-PIN brute force vs. lock-out policy  (reference model, 0.3 s per key)", fontsize=11)
    fig.tight_layout()
    fig.savefig(ROOT / "docs" / "figures" / "bruteforce_analysis.png", dpi=160)
