#!/usr/bin/env python3
"""Render face crops, checker overlay and texture bands against Facetune."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

import cv2
import numpy as np

from make_visual_comparison import (
    face_crop, largest_face, middle_frame, panel, write_lbf_mesh)


def checker(a, b, tile=72):
    yy, xx = np.indices(a.shape[:2])
    choose_b = ((xx // tile + yy // tile) % 2).astype(bool)
    result = a.copy()
    result[choose_b] = b[choose_b]
    return result


def texture_view(frame, sigma=1.6, gain=4.0):
    base = cv2.GaussianBlur(
        frame.astype(np.float32), (0, 0), sigma)
    detail = (frame.astype(np.float32) - base) * gain + 128
    return np.clip(detail, 0, 255).astype(np.uint8)


def fit_panel(frame, title, height):
    return panel(frame, title, height)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("raw", type=Path)
    parser.add_argument("facetune", type=Path)
    parser.add_argument("cli", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--fraction", type=float, default=.5)
    parser.add_argument("--panel-height", type=int, default=520)
    args = parser.parse_args()

    raw = middle_frame(args.raw, args.fraction)
    reference = middle_frame(args.facetune, args.fraction)
    reference = cv2.resize(
        reference, (raw.shape[1], raw.shape[0]),
        interpolation=cv2.INTER_AREA)
    face = largest_face(raw)
    if face is None:
        raise RuntimeError("Face not detected")

    work = args.output.parent
    prefix = args.output.stem
    raw_ppm = work / f"{prefix}-raw.ppm"
    ours_ppm = work / f"{prefix}-ours.ppm"
    mesh_bin = work / f"{prefix}-mesh.bin"
    cv2.imwrite(str(raw_ppm), raw)
    if write_lbf_mesh(raw, args.model, mesh_bin) is None:
        raise RuntimeError("Landmarks not detected")
    subprocess.run([
        str(args.cli.resolve()), str(raw_ppm), str(ours_ppm),
        "--mesh", str(mesh_bin)
    ], check=True)
    ours = cv2.imread(str(ours_ppm))

    raw = face_crop(raw, face, .24, .30, .16)
    ours = face_crop(ours, face, .24, .30, .16)
    reference = face_crop(reference, face, .24, .30, .16)
    comparison = checker(ours, reference)
    difference = np.clip(
        cv2.absdiff(ours, reference).astype(np.float32) * 4,
        0, 255).astype(np.uint8)

    top = cv2.hconcat([
        fit_panel(raw, "1  CRU", args.panel_height),
        fit_panel(ours, "2  PLUGIN", args.panel_height),
        fit_panel(reference, "3  FACETUNE", args.panel_height)
    ])
    bottom = cv2.hconcat([
        fit_panel(comparison, "4  CHECKER: PLUGIN / FACETUNE",
                  args.panel_height),
        fit_panel(texture_view(ours), "5  TEXTURA PLUGIN x4",
                  args.panel_height),
        fit_panel(texture_view(reference), "6  TEXTURA FACETUNE x4",
                  args.panel_height)
    ])
    combined = cv2.vconcat([top, bottom])
    cv2.imwrite(
        str(args.output), combined,
        [cv2.IMWRITE_JPEG_QUALITY, 95])
    raw_ppm.unlink(missing_ok=True)
    ours_ppm.unlink(missing_ok=True)
    mesh_bin.unlink(missing_ok=True)
    print(args.output)


if __name__ == "__main__":
    main()
