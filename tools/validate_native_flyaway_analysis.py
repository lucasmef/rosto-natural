#!/usr/bin/env python3
"""Validate the C++ flyaway mask analysis with real HairSegmenter output."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

import cv2
import numpy as np

from remove_flyaways import _segment_hair


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument(
        "--model",
        type=Path,
        default=Path(__file__).resolve().parents[1]
        / "models"
        / "hair_segmenter.tflite",
    )
    parser.add_argument(
        "--cli",
        type=Path,
        default=Path(__file__).resolve().parents[1]
        / "build-manual"
        / "rosto_flyaway_analysis",
    )
    parser.add_argument("--output", type=Path)
    parser.add_argument("--debug-output", type=Path)
    parser.add_argument("--gate", action="store_true")
    return parser.parse_args()


def run_native(
    cli: Path, probability: np.ndarray, strength: float
) -> tuple[np.ndarray, np.ndarray, dict[str, object]]:
    height, width = probability.shape
    with tempfile.TemporaryDirectory(prefix="rosto-native-hair-") as folder:
        root = Path(folder)
        probability_path = root / "probability.f32"
        allowed_path = root / "allowed.u8"
        protected_path = root / "protected.u8"
        np.asarray(probability, np.float32).tofile(probability_path)
        completed = subprocess.run(
            [
                str(cli),
                str(width),
                str(height),
                str(strength),
                str(probability_path),
                str(allowed_path),
                str(protected_path),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        metrics = json.loads(completed.stdout)
        allowed = np.fromfile(allowed_path, np.uint8).reshape(height, width)
        protected = np.fromfile(protected_path, np.uint8).reshape(height, width)
    return allowed, protected, metrics


def main() -> int:
    args = parse_args()
    image = cv2.imread(str(args.image), cv2.IMREAD_COLOR)
    if image is None:
        raise FileNotFoundError(f"Could not read image: {args.image}")
    if not args.cli.is_file():
        raise FileNotFoundError(f"Could not find native CLI: {args.cli}")
    scale = min(1.0, 640.0 / max(image.shape[:2]))
    preview = cv2.resize(
        image,
        (
            max(1, int(image.shape[1] * scale)),
            max(1, int(image.shape[0] * scale)),
        ),
        interpolation=cv2.INTER_AREA,
    )
    probability = _segment_hair(preview, args.model)
    natural, protected, natural_metrics = run_native(
        args.cli, probability, 0.70
    )
    maximum, protected_max, maximum_metrics = run_native(
        args.cli, probability, 1.0
    )

    strong = probability >= 0.90
    strong_recall = float(
        np.count_nonzero(strong & (protected_max > 0))
        / max(1, np.count_nonzero(strong))
    )
    overlap = int(np.count_nonzero((maximum > 0) & (protected_max > 0)))
    report: dict[str, object] = {
        "dimensions": [preview.shape[1], preview.shape[0]],
        "natural": natural_metrics,
        "maximum": maximum_metrics,
        "natural_fraction": float(np.mean(natural > 0)),
        "maximum_fraction": float(np.mean(maximum > 0)),
        "protected_strong_hair_recall": strong_recall,
        "allowed_protected_overlap": overlap,
    }
    checks = {
        "natural_mask_is_local": 0 < report["natural_fraction"] < 0.08,
        "maximum_mask_is_local": 0 < report["maximum_fraction"] < 0.12,
        "maximum_expands_coverage": int(maximum_metrics["allowed_pixels"])
        >= int(natural_metrics["allowed_pixels"]),
        "strong_hair_is_protected": strong_recall > 0.88,
        "protected_hair_is_excluded": overlap == 0,
        "maximum_reduces_safety_padding": int(
            np.count_nonzero(protected_max)
        ) <= int(np.count_nonzero(protected)),
    }
    report["checks"] = checks
    report["passed"] = all(checks.values())

    if args.debug_output:
        overlay = preview.copy()
        overlay[protected_max > 0] = (
            overlay[protected_max > 0].astype(np.float32) * 0.45
            + np.array([45, 190, 45], np.float32) * 0.55
        ).astype(np.uint8)
        overlay[maximum > 0] = (
            overlay[maximum > 0].astype(np.float32) * 0.25
            + np.array([35, 35, 245], np.float32) * 0.75
        ).astype(np.uint8)
        args.debug_output.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(args.debug_output), overlay)
    encoded = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 1 if args.gate and not report["passed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
