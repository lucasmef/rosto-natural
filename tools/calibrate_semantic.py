#!/usr/bin/env python3
"""Measure Facetune changes separately for skin, eyes, under-eye and lips."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics

import cv2
import numpy as np

from calibrate_pairs import find_pairs
from facetune_reference import (
    normalize_reference_export, untreated_control_mask)


def read_frame(path: Path, fraction: float):
    cap = cv2.VideoCapture(str(path))
    count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    cap.set(cv2.CAP_PROP_POS_FRAMES, max(0, round((count - 1) * fraction)))
    ok, frame = cap.read()
    cap.release()
    return frame if ok else None


def feature_mask(shape, points, blur=0):
    mask = np.zeros(shape[:2], np.uint8)
    cv2.fillConvexPoly(mask, cv2.convexHull(points.astype(np.int32)), 255)
    if blur:
        mask = cv2.GaussianBlur(mask, (0, 0), blur)
    return mask.astype(np.float32) / 255


def masks_for(frame, facemark):
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    cascade = cv2.CascadeClassifier(
        cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
    faces = cascade.detectMultiScale(gray, 1.1, 5, minSize=(100, 100))
    if not len(faces):
        return None
    face = max(faces, key=lambda item: item[2] * item[3])
    ok, fitted = facemark.fit(frame, np.asarray([face], dtype=np.int32))
    if not ok:
        return None
    p = fitted[0][0]
    jaw, brows = p[:17], p[17:27]
    face_width = float(np.linalg.norm(jaw[16] - jaw[0]))
    face_mask = np.zeros(frame.shape[:2], np.uint8)
    # Measure skin well inside the fitted face. The former synthetic ellipse
    # included hair/background beside the jaw and made contour leakage look
    # like successful smoothing.
    face_hull = cv2.convexHull(np.vstack([jaw, brows]).astype(np.int32))
    cv2.fillConvexPoly(face_mask, face_hull, 255, cv2.LINE_AA)
    inset = max(3, round(face_width * .025))
    if inset % 2 == 0:
        inset += 1
    face_mask = cv2.erode(
        face_mask, cv2.getStructuringElement(
            cv2.MORPH_ELLIPSE, (inset, inset)))

    eyes = feature_mask(frame.shape, np.vstack([p[36:42], p[42:48]]), 2)
    brows_mask = feature_mask(frame.shape, np.vstack([p[17:22], p[22:27]]), 3)
    lips = feature_mask(frame.shape, p[48:60], 2)
    nose = feature_mask(frame.shape, p[27:36], 3)
    protect = np.clip(eyes + brows_mask + lips + nose * .75, 0, 1)
    protect = cv2.dilate(protect, np.ones((9, 9), np.uint8), iterations=1)
    skin = face_mask.astype(np.float32) / 255 * (1 - protect)

    under = np.zeros(frame.shape[:2], np.uint8)
    for eye in (p[36:42], p[42:48]):
        x, y, w, h = cv2.boundingRect(eye.astype(np.int32))
        cv2.ellipse(under, (x + w // 2, y + h + round(h * .65)),
                    (round(w * .60), round(h * .75)), 0, 0, 360, 255, -1)
    under = under.astype(np.float32) / 255 * (1 - eyes)
    return {
        "skin": skin, "eyes": eyes, "underEye": under, "lips": lips,
        "_points": p
    }


def weighted_mean(values, mask):
    total = mask.sum()
    return float((values * mask).sum() / max(total, 1e-6))


def metrics(raw, treated, masks):
    treated = cv2.resize(treated, (raw.shape[1], raw.shape[0]))
    raw_lab = cv2.cvtColor(raw, cv2.COLOR_BGR2LAB).astype(np.float32)
    out_lab = cv2.cvtColor(treated, cv2.COLOR_BGR2LAB).astype(np.float32)
    raw_hsv = cv2.cvtColor(raw, cv2.COLOR_BGR2HSV).astype(np.float32)
    out_hsv = cv2.cvtColor(treated, cv2.COLOR_BGR2HSV).astype(np.float32)
    result = {}
    for region in ("skin", "underEye"):
        mask = masks[region]
        if region == "skin":
            result["skinRawLuma"] = weighted_mean(
                raw_lab[..., 0], mask) / 255
            result["skinRawChromaA"] = weighted_mean(
                raw_lab[..., 1], mask) / 255
            result["skinRawChromaB"] = weighted_mean(
                raw_lab[..., 2], mask) / 255
        result[f"{region}LumaDelta"] = (
            weighted_mean(out_lab[..., 0], mask) -
            weighted_mean(raw_lab[..., 0], mask)) / 255
        if region == "skin":
            result["skinChromaADelta"] = (
                weighted_mean(out_lab[..., 1], mask) -
                weighted_mean(raw_lab[..., 1], mask)) / 255
            result["skinChromaBDelta"] = (
                weighted_mean(out_lab[..., 2], mask) -
                weighted_mean(raw_lab[..., 2], mask)) / 255
    skin = masks["skin"] > .5
    lap_raw = cv2.Laplacian(raw_lab[..., 0], cv2.CV_32F)[skin]
    lap_out = cv2.Laplacian(out_lab[..., 0], cv2.CV_32F)[skin]
    result["skinTextureRatio"] = float(
        lap_out.var() / max(lap_raw.var(), 1e-6))
    for region in ("eyes", "lips"):
        mask = masks[region]
        result[f"{region}LumaDelta"] = (
            weighted_mean(out_lab[..., 0], mask) -
            weighted_mean(raw_lab[..., 0], mask)) / 255
        result[f"{region}SaturationDelta"] = (
            weighted_mean(out_hsv[..., 1], mask) -
            weighted_mean(raw_hsv[..., 1], mask)) / 255
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder", type=Path)
    ap.add_argument("--model", type=Path, required=True)
    ap.add_argument("--output", type=Path,
                    default=Path("semantic_calibration_report.json"))
    args = ap.parse_args()
    facemark = cv2.face.createFacemarkLBF()
    facemark.loadModel(str(args.model))
    samples = []
    for raw_path, treated_path in find_pairs(args.folder):
        for fraction in (.25, .50, .75):
            raw = read_frame(raw_path, fraction)
            treated = read_frame(treated_path, fraction)
            if raw is None or treated is None:
                continue
            masks = masks_for(raw, facemark)
            if masks:
                treated = cv2.resize(
                    treated, (raw.shape[1], raw.shape[0]),
                    interpolation=cv2.INTER_AREA)
                control = untreated_control_mask(raw, masks["_points"])
                treated, _ = normalize_reference_export(
                    raw, treated, control)
                sample = metrics(raw, treated, masks)
                sample.update({"clip": raw_path.stem, "fraction": fraction})
                samples.append(sample)
    keys = [key for key in samples[0] if key not in ("clip", "fraction")]
    aggregate = {key: statistics.median(s[key] for s in samples) for key in keys}
    report = {"schemaVersion": 1, "sampleCount": len(samples),
              "aggregateMedian": aggregate, "samples": samples}
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report["aggregateMedian"], indent=2))


if __name__ == "__main__":
    main()
