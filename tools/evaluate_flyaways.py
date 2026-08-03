#!/usr/bin/env python3
"""Validate sparse flyaway reconstruction against preservation invariants."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import cv2
import numpy as np

from remove_flyaways import guide_roi_from_annotation


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("result", type=Path)
    parser.add_argument("mask", type=Path)
    parser.add_argument("--guide-image", type=Path)
    parser.add_argument("--metrics", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--gate", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    original = cv2.imread(str(args.original), cv2.IMREAD_COLOR)
    result = cv2.imread(str(args.result), cv2.IMREAD_COLOR)
    mask = cv2.imread(str(args.mask), cv2.IMREAD_GRAYSCALE)
    if original is None or result is None or mask is None:
        raise FileNotFoundError("Could not read original, result, or mask")
    if result.shape != original.shape or mask.shape != original.shape[:2]:
        raise ValueError("Validation images must have identical dimensions")

    delta = np.max(
        np.abs(result.astype(np.int16) - original.astype(np.int16)), axis=2
    )
    changed = delta > 2
    selected = mask > 0
    allowed = cv2.dilate(
        selected.astype(np.uint8),
        cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7)),
    ) > 0

    height, width = mask.shape
    yy, xx = np.mgrid[:height, :width]
    # Conservative central-face protection zone. Hair finishing must remain
    # outside this area independently of the facial-retouch masks.
    face_protection = (
        ((xx - width * 0.50) / (width * 0.31)) ** 2
        + ((yy - height * 0.61) / (height * 0.27)) ** 2
        <= 1.0
    )

    gray_original = cv2.cvtColor(original, cv2.COLOR_BGR2GRAY)
    gray_result = cv2.cvtColor(result, cv2.COLOR_BGR2GRAY)
    line_original = cv2.Laplacian(gray_original, cv2.CV_32F)
    line_result = cv2.Laplacian(gray_result, cv2.CV_32F)
    if np.any(selected):
        original_energy = float(np.mean(np.abs(line_original[selected])))
        result_energy = float(np.mean(np.abs(line_result[selected])))
    else:
        original_energy = 0.0
        result_energy = 0.0

    report: dict[str, object] = {
        "dimensions": [width, height],
        "selected_pixels": int(np.count_nonzero(selected)),
        "selected_fraction": float(np.mean(selected)),
        "changed_pixels": int(np.count_nonzero(changed)),
        "changed_outside_sparse_mask": int(np.count_nonzero(changed & ~allowed)),
        "changed_inside_face_protection": int(
            np.count_nonzero(changed & face_protection)
        ),
        "line_energy_before": original_energy,
        "line_energy_after": result_energy,
        "line_energy_reduction_percent": (
            (original_energy - result_energy) / max(original_energy, 1e-6) * 100.0
        ),
    }

    if args.guide_image:
        annotated = cv2.imread(str(args.guide_image), cv2.IMREAD_COLOR)
        if annotated is None:
            raise FileNotFoundError(f"Could not read guide: {args.guide_image}")
        guide = guide_roi_from_annotation(original, annotated)
        report["changed_outside_guide"] = int(
            np.count_nonzero(changed & (guide == 0))
        )
        report["selected_outside_guide"] = int(
            np.count_nonzero(selected & (guide == 0))
        )

    guided_strength = 0.0
    if args.metrics:
        source_metrics = json.loads(args.metrics.read_text(encoding="utf-8"))
        guided_strength = float(
            source_metrics.get("settings", {}).get("strength", 0.0)
        )
        protected_overlap = int(
            source_metrics.get("metrics", {}).get(
                "protected_overlap_pixels", -1
            )
        )
        report["protected_overlap_pixels"] = protected_overlap

    # The maximum setting deliberately expands coverage to faint crown wisps.
    # Keep the natural 70% preset below 4%, then allow at most 6.1% at 100%.
    mask_limit = (
        0.04 + max(0.0, min(1.0, guided_strength) - 0.70) * 0.07
        if args.guide_image else 0.008
    )
    report["mask_fraction_limit"] = mask_limit
    checks = {
        "mask_is_local": 0 < report["selected_fraction"] < mask_limit,
        "outside_mask_is_unchanged": report["changed_outside_sparse_mask"] == 0,
        "face_is_unchanged": report["changed_inside_face_protection"] == 0,
        "line_energy_is_reduced": report["line_energy_reduction_percent"] > 5.0,
    }
    if args.guide_image:
        checks["guide_is_respected"] = (
            report["changed_outside_guide"] == 0
            and report["selected_outside_guide"] == 0
        )
    if args.metrics:
        checks["protected_hair_is_unchanged"] = (
            report["protected_overlap_pixels"] == 0
        )
    report["checks"] = checks
    report["passed"] = all(checks.values())

    encoded = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 1 if args.gate and not report["passed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
