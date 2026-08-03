#!/usr/bin/env python3
"""Estimate whether Facetune consistently changes facial geometry.

The treated landmarks are similarity-aligned to the raw landmarks first, so
translation, rotation and uniform scale from export do not count as reshaping.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics

import cv2
import numpy as np

from calibrate_pairs import find_pairs
from calibrate_semantic import read_frame


def resize(frame, width=960):
    if frame.shape[1] == width:
        return frame
    scale = width / frame.shape[1]
    return cv2.resize(
        frame, (width, round(frame.shape[0] * scale)),
        interpolation=cv2.INTER_AREA)


def fit(frame, facemark, cascade):
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    faces = cascade.detectMultiScale(gray, 1.1, 5, minSize=(80, 80))
    if not len(faces):
        return None
    face = max(faces, key=lambda item: item[2] * item[3])
    ok, shapes = facemark.fit(
        frame, np.asarray([face], dtype=np.int32))
    return shapes[0][0].astype(np.float32) if ok else None


def align_similarity(source, target):
    matrix, inliers = cv2.estimateAffinePartial2D(
        source, target, method=cv2.LMEDS)
    if matrix is None:
        return None
    homogeneous = np.hstack(
        [source, np.ones((len(source), 1), np.float32)])
    return homogeneous @ matrix.T


def distance(points, a, b):
    return float(np.linalg.norm(points[a] - points[b]))


def geometry_metrics(raw, treated):
    aligned = align_similarity(treated, raw)
    if aligned is None:
        return None
    face_width = max(distance(raw, 0, 16), 1e-6)

    def change(a, b):
        return 100 * (
            distance(aligned, a, b) - distance(raw, a, b)) / face_width

    groups = {
        "jaw": range(0, 17),
        "brows": range(17, 27),
        "nose": range(27, 36),
        "eyes": range(36, 48),
        "mouth": range(48, 68)
    }
    result = {
        "jawWidthPercentFace": change(2, 14),
        "lowerJawWidthPercentFace": change(4, 12),
        "leftEyeWidthPercentFace": change(36, 39),
        "rightEyeWidthPercentFace": change(42, 45),
        "noseWidthPercentFace": change(31, 35),
        "mouthWidthPercentFace": change(48, 54),
        "mouthHeightPercentFace": change(51, 57)
    }
    for name, indices in groups.items():
        displacement = np.linalg.norm(
            aligned[list(indices)] - raw[list(indices)], axis=1)
        result[f"{name}ResidualPercentFace"] = (
            100 * float(np.mean(displacement)) / face_width)
    result["allResidualPercentFace"] = (
        100 * float(np.mean(np.linalg.norm(aligned - raw, axis=1))) /
        face_width)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", type=Path)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    facemark = cv2.face.createFacemarkLBF()
    facemark.loadModel(str(args.model))
    cascade = cv2.CascadeClassifier(
        cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
    samples = []
    for raw_path, treated_path in find_pairs(args.folder):
        for fraction in (.20, .35, .50, .65, .80):
            raw = read_frame(raw_path, fraction)
            treated = read_frame(treated_path, fraction)
            if raw is None or treated is None:
                continue
            raw = resize(raw)
            treated = cv2.resize(
                treated, (raw.shape[1], raw.shape[0]),
                interpolation=cv2.INTER_AREA)
            raw_points = fit(raw, facemark, cascade)
            treated_points = fit(treated, facemark, cascade)
            if raw_points is None or treated_points is None:
                continue
            measured = geometry_metrics(raw_points, treated_points)
            if measured:
                measured.update({
                    "clip": raw_path.stem, "fraction": fraction
                })
                samples.append(measured)
    keys = [key for key in samples[0]
            if key not in ("clip", "fraction")]
    aggregate = {}
    for key in keys:
        values = [sample[key] for sample in samples]
        median_value = statistics.median(values)
        sign = 1 if median_value >= 0 else -1
        consistency = sum(
            1 for value in values if value * sign >= 0) / len(values)
        aggregate[key] = {
            "median": median_value,
            "p90Absolute": float(np.percentile(np.abs(values), 90)),
            "signConsistency": consistency
        }
    likely = {
        key: value for key, value in aggregate.items()
        if "Residual" not in key and
        abs(value["median"]) >= .25 and
        value["signConsistency"] >= .70
    }
    report = {
        "schemaVersion": 1,
        "sampleCount": len(samples),
        "unit": "percent of raw face width",
        "method": "LBF 68 landmarks; treated similarity-aligned to raw",
        "aggregate": aggregate,
        "likelyConsistentReshaping": likely,
        "caveat": (
            "Landmark motion can also be caused by retouch-induced detector "
            "bias. Confirm any small signal with MediaPipe and visual overlays."
        ),
        "samples": samples
    }
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({
        "sampleCount": len(samples),
        "likelyConsistentReshaping": likely,
        "featureResiduals": {
            key: value for key, value in aggregate.items()
            if "Residual" in key
        }
    }, indent=2))


if __name__ == "__main__":
    main()
