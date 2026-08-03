#!/usr/bin/env python3
"""Measure jaw edge sharpness and changes outside the face mask."""

from __future__ import annotations

import argparse
from contextlib import nullcontext
import json
from pathlib import Path
import statistics
import subprocess
import tempfile

import cv2
import numpy as np

from calibrate_pairs import find_pairs
from calibrate_semantic import read_frame
from facetune_reference import (
    normalize_reference_export, untreated_control_mask)
from make_visual_comparison import (
    MediaPipeMeshDetector, write_lbf_mesh)


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


def jaw_regions(shape, points):
    jaw = points[:17].astype(np.int32)
    brows = points[17:27].astype(np.int32)
    face_width = float(np.linalg.norm(points[16] - points[0]))
    thickness = max(7, round(face_width * .045))
    jaw_band = np.zeros(shape[:2], np.uint8)
    cv2.polylines(jaw_band, [jaw], False, 255, thickness, cv2.LINE_AA)
    face = np.zeros(shape[:2], np.uint8)
    hull = cv2.convexHull(np.vstack([jaw, brows]))
    cv2.fillConvexPoly(face, hull, 255, cv2.LINE_AA)
    inner = (jaw_band > 0) & (face > 127)
    outer = (jaw_band > 0) & (face <= 127)
    return jaw_band > 0, inner, outer


def edge_energy(frame, band):
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
    gx = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
    magnitude = np.hypot(gx, gy)[band]
    if not len(magnitude):
        return 0.0
    threshold = np.percentile(magnitude, 75)
    strongest = magnitude[magnitude >= threshold]
    return float(np.mean(strongest))


def outside_change(raw, output, outer):
    difference = cv2.absdiff(raw, output).astype(np.float32)
    magnitude = np.mean(difference, axis=2)[outer]
    return {
        "meanCodeValue": float(np.mean(magnitude)),
        "p95CodeValue": float(np.percentile(magnitude, 95))
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", type=Path)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--face-model", type=Path,
                        default=Path("models/face_landmarker.task"))
    parser.add_argument("--landmarks", choices=("mediapipe", "lbf"),
                        default="lbf")
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    facemark = cv2.face.createFacemarkLBF()
    facemark.loadModel(str(args.model))
    cascade = cv2.CascadeClassifier(
        cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
    samples = []
    detector_context = (
        MediaPipeMeshDetector(args.face_model)
        if args.landmarks == "mediapipe" else nullcontext(None))
    with tempfile.TemporaryDirectory(
            prefix="rosto-contour-", dir=args.output.parent) as temp_name, \
            detector_context as mesh_detector:
        temp = Path(temp_name)
        raw_ppm, ours_ppm = temp / "raw.ppm", temp / "ours.ppm"
        mesh_bin = temp / "mesh.bin"
        for raw_path, treated_path in find_pairs(args.folder):
            for fraction in (.25, .50, .75):
                raw = read_frame(raw_path, fraction)
                treated = read_frame(treated_path, fraction)
                if raw is None or treated is None:
                    continue
                raw = resize(raw)
                treated = cv2.resize(
                    treated, (raw.shape[1], raw.shape[0]),
                    interpolation=cv2.INTER_AREA)
                points = fit(raw, facemark, cascade)
                if points is None:
                    continue
                control = untreated_control_mask(raw, points)
                treated, _ = normalize_reference_export(
                    raw, treated, control)
                cv2.imwrite(str(raw_ppm), raw)
                if mesh_detector is not None:
                    mesh_ok = mesh_detector.write(raw, mesh_bin)
                else:
                    mesh_ok = write_lbf_mesh(
                        raw, args.model, mesh_bin,
                        facemark=facemark) is not None
                if not mesh_ok:
                    continue
                subprocess.run([
                    str(args.cli.resolve()), str(raw_ppm), str(ours_ppm),
                    "--mesh", str(mesh_bin)
                ], check=True, stdout=subprocess.DEVNULL)
                ours = cv2.imread(str(ours_ppm))
                band, inner, outer = jaw_regions(raw.shape, points)
                raw_edge = edge_energy(raw, band)
                ours_edge = edge_energy(ours, band)
                reference_edge = edge_energy(treated, band)
                ours_outside = outside_change(raw, ours, outer)
                reference_outside = outside_change(raw, treated, outer)
                samples.append({
                    "clip": raw_path.stem,
                    "fraction": fraction,
                    "oursJawSharpnessRatio": ours_edge / max(raw_edge, 1e-6),
                    "facetuneJawSharpnessRatio":
                        reference_edge / max(raw_edge, 1e-6),
                    "oursOutsideMeanCodeValue":
                        ours_outside["meanCodeValue"],
                    "oursOutsideP95CodeValue":
                        ours_outside["p95CodeValue"],
                    "facetuneOutsideMeanCodeValue":
                        reference_outside["meanCodeValue"],
                    "facetuneOutsideP95CodeValue":
                        reference_outside["p95CodeValue"]
                })
    keys = [key for key in samples[0]
            if key not in ("clip", "fraction")]
    aggregate = {
        key: statistics.median(sample[key] for sample in samples)
        for key in keys
    }
    report = {
        "schemaVersion": 1,
        "sampleCount": len(samples),
        "evaluationWidth": 960,
        "aggregateMedian": aggregate,
        "samples": samples
    }
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report["aggregateMedian"], indent=2))


if __name__ == "__main__":
    main()
