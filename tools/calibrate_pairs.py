#!/usr/bin/env python3
"""Calibrate the natural preset from raw/-facetune video pairs.

OpenCV is intentionally a tooling-only dependency:
    python3 -m pip install opencv-python numpy
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics
import sys

try:
    import cv2
    import numpy as np
except ImportError:
    print("Missing calibration dependencies: pip install opencv-python numpy",
          file=sys.stderr)
    raise SystemExit(2)


RAW_NAMES = ("{stem}.mov", "{stem}-montagem.mov", "{stem}.MOV")


def find_pairs(folder: Path):
    for treated in sorted(folder.glob("*-facetune.MOV")):
        stem = treated.name.removesuffix("-facetune.MOV")
        raw = next((folder / pattern.format(stem=stem) for pattern in RAW_NAMES
                    if (folder / pattern.format(stem=stem)).exists()), None)
        if raw:
            yield raw, treated


def read_middle_frames(path: Path, count: int = 7):
    cap = cv2.VideoCapture(str(path))
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    if total <= 0:
        return []
    positions = np.linspace(total * .15, total * .85, count).astype(int)
    frames = []
    for position in positions:
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(position))
        ok, frame = cap.read()
        if ok:
            frames.append(cv2.cvtColor(frame, cv2.COLOR_BGR2LAB))
    cap.release()
    return frames


def pair_metrics(raw_path: Path, treated_path: Path):
    raw_frames = read_middle_frames(raw_path)
    treated_frames = read_middle_frames(treated_path)
    samples = []
    for raw, treated in zip(raw_frames, treated_frames):
        if raw.shape != treated.shape:
            treated = cv2.resize(treated, (raw.shape[1], raw.shape[0]))
        h, w = raw.shape[:2]
        # Center crop intentionally approximates the single talking-head ROI.
        roi = np.s_[int(h*.18):int(h*.82), int(w*.25):int(w*.75)]
        a = raw[roi].astype(np.float32)
        b = treated[roi].astype(np.float32)
        raw_l = a[..., 0]
        treated_l = b[..., 0]
        samples.append({
            "luma_delta": float(np.mean(treated_l - raw_l) / 255.0),
            "chroma_a_delta": float(np.mean(b[..., 1] - a[..., 1]) / 255.0),
            "chroma_b_delta": float(np.mean(b[..., 2] - a[..., 2]) / 255.0),
            "texture_ratio": float(
                cv2.Laplacian(treated_l, cv2.CV_32F).var() /
                max(cv2.Laplacian(raw_l, cv2.CV_32F).var(), 1e-6)),
        })
    return samples


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", type=Path)
    parser.add_argument("--output", type=Path,
                        default=Path("calibration_report.json"))
    args = parser.parse_args()
    pairs = list(find_pairs(args.folder))
    if not pairs:
        raise SystemExit("No reliable raw/-facetune pairs found")
    details = []
    for raw, treated in pairs:
        samples = pair_metrics(raw, treated)
        if samples:
            details.append({"raw": str(raw), "treated": str(treated),
                            "samples": samples})
    metrics = [m for d in details for m in d["samples"]]
    aggregate = {key: statistics.median(m[key] for m in metrics)
                 for key in metrics[0]}
    texture_ratio = max(0.0, min(1.2, aggregate["texture_ratio"]))
    preset = {
        "master": 0.72,
        # Mapping accounts for the mask feather and texture re-injection in the
        # runtime processor: measured 25% attenuation maps to ~58% UI strength.
        "skin": round(max(0.25, min(0.85, (1.0 - texture_ratio) * 2.3)), 3),
        "preserveTexture": round(max(0.45, min(0.90, texture_ratio)), 3),
        "tone": round(max(0.10, min(0.60,
                                   0.24 + aggregate["luma_delta"] * 2.0)), 3),
        "underEye": 0.42, "shine": 0.30, "eyes": 0.20,
        "blush": 0.12, "lips": 0.16,
    }
    report = {"schemaVersion": 1, "pairCount": len(details),
              "aggregate": aggregate, "naturalPreset": preset,
              "pairs": details}
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Wrote {args.output} from {len(details)} pairs")


if __name__ == "__main__":
    main()
