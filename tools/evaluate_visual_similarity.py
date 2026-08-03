#!/usr/bin/env python3
"""Direct pixel/edge similarity of Rosto Natural to Facetune."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics
import subprocess
import tempfile

import cv2
import numpy as np

from calibrate_pairs import find_pairs
from calibrate_semantic import read_frame
from evaluate_cheek_texture import fit_points
from evaluate_ours_semantic import resize_for_evaluation
from facetune_reference import (
    normalize_reference_export, untreated_control_mask)
from make_visual_comparison import write_lbf_mesh


def face_interior_mask(shape, points):
    mask = np.zeros(shape[:2], np.uint8)
    hull = cv2.convexHull(
        np.vstack([points[:17], points[17:27]]).astype(np.int32))
    cv2.fillConvexPoly(mask, hull, 255, cv2.LINE_AA)
    face_width = float(np.linalg.norm(points[16] - points[0]))
    inset = max(3, round(face_width * .012))
    if inset % 2 == 0:
        inset += 1
    return cv2.erode(
        mask, cv2.getStructuringElement(
            cv2.MORPH_ELLIPSE, (inset, inset))) > 127


def delta_e(a, b, mask):
    # Compare low-frequency color independently from texture and single-pixel
    # codec differences. Detail similarity is gated separately below.
    a = cv2.GaussianBlur(a, (0, 0), 4.0)
    b = cv2.GaussianBlur(b, (0, 0), 4.0)
    lab_a = cv2.cvtColor(a, cv2.COLOR_BGR2LAB).astype(np.float32)
    lab_b = cv2.cvtColor(b, cv2.COLOR_BGR2LAB).astype(np.float32)
    # OpenCV Lab is encoded in 8-bit display units. Keeping those units makes
    # the threshold interpretable as average code-value distance.
    difference = np.linalg.norm(lab_a - lab_b, axis=2)
    return float(np.mean(difference[mask]))


def gradient_error(a, b, mask):
    gray_a = cv2.cvtColor(a, cv2.COLOR_BGR2GRAY).astype(np.float32)
    gray_b = cv2.cvtColor(b, cv2.COLOR_BGR2GRAY).astype(np.float32)

    def magnitude(gray):
        gx = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
        gy = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
        return np.hypot(gx, gy)

    return float(np.mean(np.abs(magnitude(gray_a) - magnitude(gray_b))[mask]))


def aggregate(samples):
    keys = [key for key in samples[0]
            if key not in ("clip", "fraction", "exportNormalization")]
    return {
        key: statistics.median(sample[key] for sample in samples)
        for key in keys
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", type=Path)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--master", type=float)
    parser.add_argument("--skin", type=float)
    parser.add_argument("--preserve-texture", type=float)
    parser.add_argument("--under-eye", type=float)
    parser.add_argument("--tone", type=float)
    parser.add_argument("--shine", type=float)
    parser.add_argument("--eyes", type=float)
    parser.add_argument("--blush", type=float)
    parser.add_argument("--lips", type=float)
    args = parser.parse_args()

    facemark = cv2.face.createFacemarkLBF()
    facemark.loadModel(str(args.model))
    samples = []
    with tempfile.TemporaryDirectory(
            prefix="rosto-similarity-", dir=args.output.parent) as temp_name:
        temp = Path(temp_name)
        raw_ppm = temp / "raw.ppm"
        ours_ppm = temp / "ours.ppm"
        mesh_bin = temp / "mesh.bin"
        for raw_path, treated_path in find_pairs(args.folder):
            for fraction in (.25, .50, .75):
                raw = read_frame(raw_path, fraction)
                reference = read_frame(treated_path, fraction)
                if raw is None or reference is None:
                    continue
                raw = resize_for_evaluation(raw)
                reference = cv2.resize(
                    reference, (raw.shape[1], raw.shape[0]),
                    interpolation=cv2.INTER_AREA)
                points = fit_points(raw, facemark)
                if points is None:
                    continue
                mask = face_interior_mask(raw.shape, points)
                control = untreated_control_mask(raw, points)
                reference, normalization = normalize_reference_export(
                    raw, reference, control)
                cv2.imwrite(str(raw_ppm), raw)
                if write_lbf_mesh(
                        raw, args.model, mesh_bin,
                        facemark=facemark) is None:
                    continue
                command = [
                    str(args.cli.resolve()), str(raw_ppm), str(ours_ppm),
                    "--mesh", str(mesh_bin)
                ]
                for name in (
                        "master", "skin", "preserve_texture", "under_eye",
                        "tone", "shine", "eyes", "blush", "lips"):
                    value = getattr(args, name)
                    if value is not None:
                        command.extend([
                            "--" + name.replace("_", "-"), str(value)])
                subprocess.run(
                    command, check=True, stdout=subprocess.DEVNULL)
                ours = cv2.imread(str(ours_ppm))
                raw_delta = delta_e(raw, reference, mask)
                ours_delta = delta_e(ours, reference, mask)
                raw_gradient = gradient_error(raw, reference, mask)
                ours_gradient = gradient_error(ours, reference, mask)
                samples.append({
                    "clip": raw_path.stem,
                    "fraction": fraction,
                    "rawToFacetuneDeltaE": raw_delta,
                    "pluginToFacetuneDeltaE": ours_delta,
                    "deltaEImprovementPercent": 100 * (
                        1 - ours_delta / max(raw_delta, 1e-6)),
                    "rawToFacetuneGradientError": raw_gradient,
                    "pluginToFacetuneGradientError": ours_gradient,
                    "gradientImprovementPercent": 100 * (
                        1 - ours_gradient / max(raw_gradient, 1e-6)),
                    "exportNormalization": normalization
                })

    report = {
        "schemaVersion": 1,
        "sampleCount": len(samples),
        "evaluationWidth": 960,
        "region": "eroded lower-face interior",
        "referenceNormalization":
            "per-channel affine export drift fitted outside the face",
        "aggregateMedian": aggregate(samples),
        "samples": samples
    }
    args.output.write_text(
        json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report["aggregateMedian"], indent=2))


if __name__ == "__main__":
    main()
