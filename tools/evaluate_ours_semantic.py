#!/usr/bin/env python3
"""Compare Rosto Natural and Facetune metrics over all calibration samples."""

from __future__ import annotations

import argparse
from contextlib import nullcontext
import json
from pathlib import Path
import statistics
import subprocess
import tempfile

import cv2

from calibrate_pairs import find_pairs
from calibrate_semantic import masks_for, metrics, read_frame
from facetune_reference import (
    normalize_reference_export, untreated_control_mask)
from make_visual_comparison import (
    MediaPipeMeshDetector, write_lbf_mesh)


def resize_for_evaluation(frame, max_width=960):
    if frame.shape[1] <= max_width:
        return frame
    scale = max_width / frame.shape[1]
    return cv2.resize(
        frame, (max_width, round(frame.shape[0] * scale)),
        interpolation=cv2.INTER_AREA)


def aggregate(samples):
    keys = [key for key in samples[0]
            if key not in ("clip", "fraction")]
    return {key: statistics.median(sample[key] for sample in samples)
            for key in keys}


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
    ours_samples, reference_samples = [], []
    detector_context = (
        MediaPipeMeshDetector(args.face_model)
        if args.landmarks == "mediapipe" else nullcontext(None))
    with tempfile.TemporaryDirectory(
            prefix="rosto-evaluate-", dir=args.output.parent) as temp_name, \
            detector_context as mesh_detector:
        temp = Path(temp_name)
        raw_ppm = temp / "raw.ppm"
        ours_ppm = temp / "ours.ppm"
        mesh_bin = temp / "mesh.bin"
        for raw_path, treated_path in find_pairs(args.folder):
            for fraction in (.25, .50, .75):
                raw = read_frame(raw_path, fraction)
                treated = read_frame(treated_path, fraction)
                if raw is None or treated is None:
                    continue
                raw = resize_for_evaluation(raw)
                treated = cv2.resize(
                    treated, (raw.shape[1], raw.shape[0]),
                    interpolation=cv2.INTER_AREA)
                masks = masks_for(raw, facemark)
                if masks is None:
                    continue
                control = untreated_control_mask(raw, masks["_points"])
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
                command = [
                    str(args.cli.resolve()), str(raw_ppm), str(ours_ppm),
                    "--mesh", str(mesh_bin)]
                for name in (
                        "master", "skin", "preserve_texture", "under_eye",
                        "tone", "shine", "eyes", "blush", "lips"):
                    value = getattr(args, name)
                    if value is not None:
                        command.extend([
                            "--" + name.replace("_", "-"), str(value)])
                subprocess.run(
                    command,
                    check=True, stdout=subprocess.DEVNULL)
                ours = cv2.imread(str(ours_ppm))
                ours_metric = metrics(raw, ours, masks)
                reference_metric = metrics(raw, treated, masks)
                identity = {"clip": raw_path.stem, "fraction": fraction}
                ours_samples.append({**identity, **ours_metric})
                reference_samples.append({**identity, **reference_metric})

    ours = aggregate(ours_samples)
    reference = aggregate(reference_samples)
    errors = {key: ours[key] - reference[key] for key in ours}
    report = {
        "schemaVersion": 1,
        "sampleCount": len(ours_samples),
        "evaluationWidth": 960,
        "rostoNaturalMedian": ours,
        "facetuneMedian": reference,
        "signedError": errors,
        "samples": ours_samples,
        "facetuneSamples": reference_samples
    }
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({
        "sampleCount": len(ours_samples),
        "rostoNaturalMedian": ours,
        "facetuneMedian": reference,
        "signedError": errors
    }, indent=2))


if __name__ == "__main__":
    main()
