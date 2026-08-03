#!/usr/bin/env python3
"""Compare fine skin texture only inside stable cheek regions."""

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
from calibrate_semantic import masks_for, read_frame
from evaluate_ours_semantic import resize_for_evaluation
from facetune_reference import (
    normalize_reference_export, untreated_control_mask)
from make_visual_comparison import write_lbf_mesh


def fit_points(frame, facemark):
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    cascade = cv2.CascadeClassifier(
        cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
    faces = cascade.detectMultiScale(gray, 1.1, 5, minSize=(100, 100))
    if not len(faces):
        return None
    face = max(faces, key=lambda item: item[2] * item[3])
    ok, fitted = facemark.fit(
        frame, np.asarray([face], dtype=np.int32))
    return fitted[0][0].astype(np.float32) if ok else None


def cheek_mask(shape, points, semantic_skin):
    mask = np.zeros(shape[:2], np.uint8)
    left = np.vstack([
        points[1:6], points[31], points[48], points[36], points[39]])
    right = np.vstack([
        points[11:16], points[35], points[54], points[42], points[45]])
    cv2.fillConvexPoly(
        mask, cv2.convexHull(left.astype(np.int32)), 255, cv2.LINE_AA)
    cv2.fillConvexPoly(
        mask, cv2.convexHull(right.astype(np.int32)), 255, cv2.LINE_AA)
    face_width = float(np.linalg.norm(points[16] - points[0]))
    inset = max(3, round(face_width * .025))
    if inset % 2 == 0:
        inset += 1
    mask = cv2.erode(
        mask, cv2.getStructuringElement(
            cv2.MORPH_ELLIPSE, (inset, inset)))
    return (mask > 127) & (semantic_skin > .5)


def band_energy(frame, mask, sigma):
    luma = cv2.cvtColor(frame, cv2.COLOR_BGR2LAB)[..., 0].astype(np.float32)
    high = luma - cv2.GaussianBlur(luma, (0, 0), sigma)
    values = high[mask]
    if not len(values):
        return 0.0
    # Trim rare compression/feature outliers so the number describes skin
    # texture instead of a stray eyelash or hair.
    limit = np.percentile(np.abs(values), 95)
    values = values[np.abs(values) <= limit]
    return float(np.sqrt(np.mean(values * values)))


def texture_metrics(raw, output, mask):
    source = raw.astype(np.float32) / 255
    fine_base = cv2.boxFilter(
        source, -1, (7, 7), normalize=True,
        borderType=cv2.BORDER_REPLICATE)
    coarse_base = cv2.boxFilter(
        source, -1, (15, 15), normalize=True,
        borderType=cv2.BORDER_REPLICATE)
    fine_magnitude = np.max(np.abs(source - fine_base), axis=2)[mask]
    mid_magnitude = np.max(np.abs(fine_base - coarse_base), axis=2)[mask]
    result = {
        "fineMagnitudeP50": float(np.percentile(fine_magnitude, 50)),
        "fineMagnitudeP75": float(np.percentile(fine_magnitude, 75)),
        "fineMagnitudeP90": float(np.percentile(fine_magnitude, 90)),
        "midMagnitudeP50": float(np.percentile(mid_magnitude, 50)),
        "midMagnitudeP75": float(np.percentile(mid_magnitude, 75)),
        "midMagnitudeP90": float(np.percentile(mid_magnitude, 90))
    }
    for name, sigma in (("micro", .85), ("fine", 1.60), ("mid", 3.20)):
        raw_energy = band_energy(raw, mask, sigma)
        out_energy = band_energy(output, mask, sigma)
        result[f"{name}RawEnergy"] = raw_energy
        result[f"{name}OutputEnergy"] = out_energy
        result[f"{name}TextureRatio"] = (
            out_energy / max(raw_energy, 1e-6))
    return result


def aggregate(samples):
    keys = [key for key in samples[0]
            if key not in ("clip", "fraction")]
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
    parser.add_argument("--preserve-texture", type=float)
    args = parser.parse_args()

    facemark = cv2.face.createFacemarkLBF()
    facemark.loadModel(str(args.model))
    ours_samples, facetune_samples = [], []
    with tempfile.TemporaryDirectory(
            prefix="rosto-cheek-", dir=args.output.parent) as temp_name:
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
                semantic = masks_for(raw, facemark)
                points = fit_points(raw, facemark)
                if semantic is None or points is None:
                    continue
                control = untreated_control_mask(raw, points)
                treated, _ = normalize_reference_export(
                    raw, treated, control)
                mask = cheek_mask(raw.shape, points, semantic["skin"])
                if np.count_nonzero(mask) < 100:
                    continue
                cv2.imwrite(str(raw_ppm), raw)
                if write_lbf_mesh(
                        raw, args.model, mesh_bin,
                        facemark=facemark) is None:
                    continue
                command = [
                    str(args.cli.resolve()), str(raw_ppm), str(ours_ppm),
                    "--mesh", str(mesh_bin)
                ]
                if args.preserve_texture is not None:
                    command.extend([
                        "--preserve-texture",
                        str(args.preserve_texture)])
                subprocess.run(
                    command, check=True, stdout=subprocess.DEVNULL)
                ours = cv2.imread(str(ours_ppm))
                identity = {
                    "clip": raw_path.stem, "fraction": fraction}
                ours_samples.append({
                    **identity, **texture_metrics(raw, ours, mask)})
                facetune_samples.append({
                    **identity, **texture_metrics(raw, treated, mask)})

    ours = aggregate(ours_samples)
    facetune = aggregate(facetune_samples)
    report = {
        "schemaVersion": 1,
        "sampleCount": len(ours_samples),
        "evaluationWidth": 960,
        "region": "eroded semantic cheek interiors",
        "rostoNaturalMedian": ours,
        "facetuneMedian": facetune,
        "signedError": {
            key: ours[key] - facetune[key] for key in ours},
        "samples": ours_samples,
        "facetuneSamples": facetune_samples
    }
    args.output.write_text(
        json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({
        "sampleCount": len(ours_samples),
        "rostoNaturalMedian": ours,
        "facetuneMedian": facetune,
        "signedError": report["signedError"]
    }, indent=2))


if __name__ == "__main__":
    main()
