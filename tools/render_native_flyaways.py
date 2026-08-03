#!/usr/bin/env python3
"""Render the exact C++ flyaway path using Python only for model inference."""

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
    parser.add_argument("output", type=Path)
    parser.add_argument("--strength", type=float, default=1.0)
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
        / "rosto_flyaway_render",
    )
    parser.add_argument("--mask-output", type=Path)
    parser.add_argument("--protected-output", type=Path)
    parser.add_argument("--debug-output", type=Path)
    parser.add_argument("--metrics-output", type=Path)
    parser.add_argument("--gate", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    image = cv2.imread(str(args.image), cv2.IMREAD_COLOR)
    if image is None:
        raise FileNotFoundError(f"Could not read image: {args.image}")
    height, width = image.shape[:2]
    scale = min(1.0, 640.0 / max(height, width))
    preview = cv2.resize(
        image,
        (max(1, int(width * scale)), max(1, int(height * scale))),
        interpolation=cv2.INTER_AREA,
    )
    probability = _segment_hair(preview, args.model)
    rgba = cv2.cvtColor(image, cv2.COLOR_BGR2RGBA).astype(np.float32) / 255.0

    with tempfile.TemporaryDirectory(prefix="rosto-native-render-") as folder:
        root = Path(folder)
        input_path = root / "image.rgba.f32"
        probability_path = root / "probability.f32"
        result_path = root / "result.rgba.f32"
        mask_path = root / "allowed.u8"
        protected_path = root / "protected.u8"
        rgba.tofile(input_path)
        np.asarray(probability, np.float32).tofile(probability_path)
        completed = subprocess.run(
            [
                str(args.cli),
                str(width),
                str(height),
                str(preview.shape[1]),
                str(preview.shape[0]),
                str(float(np.clip(args.strength, 0.0, 1.0))),
                str(input_path),
                str(probability_path),
                str(result_path),
                str(mask_path),
                str(protected_path),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        native_metrics = json.loads(completed.stdout)
        result_rgba = np.fromfile(result_path, np.float32).reshape(
            height, width, 4
        )
        allowed = np.fromfile(mask_path, np.uint8).reshape(height, width)
        protected = np.fromfile(protected_path, np.uint8).reshape(height, width)

    result = cv2.cvtColor(
        np.clip(result_rgba * 255.0, 0, 255).astype(np.uint8),
        cv2.COLOR_RGBA2BGR,
    )
    delta = np.max(
        np.abs(result.astype(np.int16) - image.astype(np.int16)), axis=2
    )
    changed = delta > 2
    yy, xx = np.mgrid[:height, :width]
    face = (
        ((xx - width * .50) / (width * .31)) ** 2
        + ((yy - height * .61) / (height * .27)) ** 2
        <= 1.0
    )
    gray_before = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    gray_after = cv2.cvtColor(result, cv2.COLOR_BGR2GRAY)
    line_before = cv2.Laplacian(gray_before, cv2.CV_32F)
    line_after = cv2.Laplacian(gray_after, cv2.CV_32F)
    changed_energy_before = float(
        np.mean(np.abs(line_before[changed]))
    ) if np.any(changed) else 0.0
    changed_energy_after = float(
        np.mean(np.abs(line_after[changed]))
    ) if np.any(changed) else 0.0
    report: dict[str, object] = {
        **native_metrics,
        "dimensions": [width, height],
        "changed_pixels": int(np.count_nonzero(changed)),
        "changed_fraction": float(np.mean(changed)),
        "changed_inside_face": int(np.count_nonzero(changed & face)),
        "changed_inside_protected_hair": int(
            np.count_nonzero(changed & (protected >= 128))
        ),
        "changed_outside_allowed_region": int(
            np.count_nonzero(changed & (allowed == 0))
        ),
        "line_energy_before": changed_energy_before,
        "line_energy_after": changed_energy_after,
    }
    checks = {
        "reconstruction_is_local": 0 < report["changed_fraction"] < .04,
        "face_is_unchanged": report["changed_inside_face"] == 0,
        "protected_hair_is_unchanged":
            report["changed_inside_protected_hair"] == 0,
        "allowed_region_is_respected":
            report["changed_outside_allowed_region"] == 0,
        "line_energy_does_not_increase":
            changed_energy_after <= changed_energy_before * 1.02,
    }
    report["checks"] = checks
    report["passed"] = all(checks.values())

    args.output.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(args.output), result)
    if args.mask_output:
        args.mask_output.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(args.mask_output), allowed)
    if args.protected_output:
        args.protected_output.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(args.protected_output), protected)
    if args.debug_output:
        args.debug_output.parent.mkdir(parents=True, exist_ok=True)
        mask_overlay = image.copy()
        mask_overlay[protected >= 128] = (
            mask_overlay[protected >= 128].astype(np.float32) * .45
            + np.array([45, 190, 45], np.float32) * .55
        ).astype(np.uint8)
        mask_overlay[allowed > 0] = (
            mask_overlay[allowed > 0].astype(np.float32) * .25
            + np.array([35, 35, 245], np.float32) * .75
        ).astype(np.uint8)
        cv2.imwrite(
            str(args.debug_output),
            np.hstack([image, result, mask_overlay]),
        )
    encoded = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.metrics_output:
        args.metrics_output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 1 if args.gate and not report["passed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
