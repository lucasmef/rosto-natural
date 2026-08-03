#!/usr/bin/env python3
"""Compose labeled comparison JPGs into a compact contact sheet."""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2
import numpy as np


def fit(image, width):
    height = round(image.shape[0] * width / image.shape[1])
    return cv2.resize(image, (width, height), interpolation=cv2.INTER_AREA)


def tile(path, width):
    image = cv2.imread(str(path))
    if image is None:
        raise RuntimeError(f"Could not read {path}")
    image = fit(image, width)
    label = np.full((42, width, 3), (24, 24, 24), np.uint8)
    name = path.stem
    for version in ("v09", "v08", "v07"):
        name = name.replace(f"rosto-natural-{version}-", "CLIPE ")
    cv2.putText(label, name,
                (14, 28), cv2.FONT_HERSHEY_SIMPLEX, .62,
                (210, 210, 210), 2, cv2.LINE_AA)
    return cv2.vconcat([label, image])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--columns", type=int, default=2)
    parser.add_argument("--tile-width", type=int, default=900)
    args = parser.parse_args()
    tiles = [tile(path, args.tile_width) for path in args.inputs]
    target_height = max(item.shape[0] for item in tiles)
    blank = np.full(
        (target_height, args.tile_width, 3), (24, 24, 24), np.uint8)
    normalized = [
        cv2.copyMakeBorder(
            item, 0, target_height - item.shape[0], 0, 0,
            cv2.BORDER_CONSTANT, value=(24, 24, 24))
        for item in tiles
    ]
    while len(normalized) % args.columns:
        normalized.append(blank.copy())
    rows = [
        cv2.hconcat(normalized[index:index + args.columns])
        for index in range(0, len(normalized), args.columns)
    ]
    sheet = cv2.vconcat(rows)
    cv2.imwrite(str(args.output), sheet, [cv2.IMWRITE_JPEG_QUALITY, 91])
    print(args.output)


if __name__ == "__main__":
    main()
