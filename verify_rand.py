#!/usr/bin/env python3
"""Analisi visiva della qualità dei campioni TrueAudioRand."""

import argparse
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def load_samples(path: Path) -> np.ndarray:
    try:
        data = np.loadtxt(path, dtype=np.uint64)
    except OSError as exc:
        print(f"Errore lettura file: {exc}", file=sys.stderr)
        sys.exit(1)

    if data.size == 0:
        print("Il file non contiene campioni.", file=sys.stderr)
        sys.exit(1)

    return data.astype(np.float64)


def plot_histogram(samples: np.ndarray, output: Path) -> None:
    fig, ax = plt.subplots(figsize=(10, 6))

    counts, edges, _ = ax.hist(
        samples,
        bins=256,
        density=True,
        alpha=0.75,
        color="#2563eb",
        edgecolor="white",
        linewidth=0.3,
        label="Distribuzione osservata",
    )

    mean = samples.mean()
    std = samples.std(ddof=1)
    x = np.linspace(samples.min(), samples.max(), 500)
    gaussian = (
        1.0 / (std * np.sqrt(2 * np.pi))
        * np.exp(-0.5 * ((x - mean) / std) ** 2)
    )
    ax.plot(
        x,
        gaussian,
        color="#dc2626",
        linewidth=2,
        label="Gaussiana teorica (non attesa per TRNG uniforme)",
    )

    uniform_height = 1.0 / (samples.max() - samples.min() + 1)
    ax.axhline(
        uniform_height,
        color="#16a34a",
        linestyle="--",
        linewidth=2,
        label="Uniforme teorica (attesa)",
    )

    ax.set_title("Istogramma campioni vs distribuzione gaussiana")
    ax.set_xlabel("Valore campione (uint32)")
    ax.set_ylabel("Densità")
    ax.legend()
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(output, dpi=150)
    plt.close(fig)


def plot_scatter(samples: np.ndarray, output: Path) -> None:
    fig, ax = plt.subplots(figsize=(8, 8))

    x = samples[:-1]
    y = samples[1:]
    step = max(1, len(x) // 5000)
    ax.scatter(
        x[::step],
        y[::step],
        s=4,
        alpha=0.35,
        color="#7c3aed",
        edgecolors="none",
    )

    ax.set_title("Correlazione campione N vs campione N+1")
    ax.set_xlabel("Campione N")
    ax.set_ylabel("Campione N+1")
    ax.set_aspect("equal", adjustable="box")
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(output, dpi=150)
    plt.close(fig)


def print_stats(samples: np.ndarray) -> None:
    print(f"Campioni analizzati : {len(samples):,}")
    print(f"Minimo              : {samples.min():,.0f}")
    print(f"Massimo             : {samples.max():,.0f}")
    print(f"Media               : {samples.mean():,.2f}")
    print(f"Dev. std.           : {samples.std(ddof=1):,.2f}")
    expected_mean = (2**32 - 1) / 2
    print(f"Media attesa (U32)  : {expected_mean:,.2f}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Verifica qualità random TrueAudioRand")
    parser.add_argument(
        "input",
        nargs="?",
        default="random_samples.txt",
        help="File con un campione uint32 per riga (default: random_samples.txt)",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    samples = load_samples(input_path)

    print_stats(samples)

    hist_path = Path("histogram.png")
    scatter_path = Path("scatter.png")

    plot_histogram(samples, hist_path)
    plot_scatter(samples, scatter_path)

    print(f"\nGrafici salvati:")
    print(f"  - {hist_path}")
    print(f"  - {scatter_path}")


if __name__ == "__main__":
    main()
