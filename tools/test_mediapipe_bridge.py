#!/usr/bin/env python3
"""Run Face Landmarker and Hair Segmenter through the native C bridge."""

from __future__ import annotations

import argparse
import ctypes
import json
from pathlib import Path

import numpy as np
from PIL import Image as PillowImage


ROOT = Path(__file__).resolve().parents[1]
LANDMARK_COUNT = 478


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument(
        "--library",
        type=Path,
        default=ROOT / "dist/native/libRostoMediaPipe.dylib",
    )
    parser.add_argument(
        "--face-model",
        type=Path,
        default=ROOT / "models/face_landmarker.task",
    )
    parser.add_argument(
        "--hair-model",
        type=Path,
        default=ROOT / "models/hair_segmenter.tflite",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "mediapipe_bridge_gate.json",
    )
    return parser.parse_args()


def configure(library: ctypes.CDLL) -> None:
    byte_pointer = ctypes.POINTER(ctypes.c_uint8)
    float_pointer = ctypes.POINTER(ctypes.c_float)
    library.rosto_mp_face_create.restype = ctypes.c_void_p
    library.rosto_mp_face_create.argtypes = [
        ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t
    ]
    library.rosto_mp_face_destroy.argtypes = [ctypes.c_void_p]
    library.rosto_mp_face_detect.restype = ctypes.c_int
    library.rosto_mp_face_detect.argtypes = [
        ctypes.c_void_p, byte_pointer, ctypes.c_int, ctypes.c_int,
        ctypes.c_int, float_pointer, ctypes.c_size_t, float_pointer,
        ctypes.c_char_p, ctypes.c_size_t,
    ]
    library.rosto_mp_hair_create.restype = ctypes.c_void_p
    library.rosto_mp_hair_create.argtypes = [
        ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t
    ]
    library.rosto_mp_hair_destroy.argtypes = [ctypes.c_void_p]
    library.rosto_mp_hair_segment.restype = ctypes.c_int
    library.rosto_mp_hair_segment.argtypes = [
        ctypes.c_void_p, byte_pointer, ctypes.c_int, ctypes.c_int,
        ctypes.c_int, float_pointer, ctypes.c_size_t,
        ctypes.c_char_p, ctypes.c_size_t,
    ]
    library.rosto_mp_runtime_version.restype = ctypes.c_char_p


def main() -> int:
    args = parse_args()
    if not args.library.is_file():
        raise FileNotFoundError(args.library)
    try:
        image = PillowImage.open(args.image).convert("RGBA")
    except OSError as exc:
        raise FileNotFoundError(args.image)
    scale = min(1.0, 640.0 / max(image.width, image.height))
    if scale < 1.0:
        image = image.resize(
            (
                max(1, int(image.width * scale)),
                max(1, int(image.height * scale)),
            ),
            PillowImage.Resampling.LANCZOS,
        )
    rgba = np.ascontiguousarray(np.asarray(image, dtype=np.uint8))
    height, width = rgba.shape[:2]
    library = ctypes.CDLL(str(args.library))
    configure(library)
    error = ctypes.create_string_buffer(2048)
    pixels = rgba.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))

    face = library.rosto_mp_face_create(
        str(args.face_model).encode(), error, len(error)
    )
    if not face:
        raise RuntimeError(error.value.decode(errors="replace"))
    landmarks = np.zeros((LANDMARK_COUNT, 2), np.float32)
    confidence = ctypes.c_float()
    try:
        face_result = library.rosto_mp_face_detect(
            face, pixels, width, height, int(rgba.strides[0]),
            landmarks.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
            LANDMARK_COUNT, ctypes.byref(confidence), error, len(error),
        )
    finally:
        library.rosto_mp_face_destroy(face)
    if face_result < 0:
        raise RuntimeError(error.value.decode(errors="replace"))

    hair = library.rosto_mp_hair_create(
        str(args.hair_model).encode(), error, len(error)
    )
    if not hair:
        raise RuntimeError(error.value.decode(errors="replace"))
    probability = np.zeros((height, width), np.float32)
    try:
        hair_result = library.rosto_mp_hair_segment(
            hair, pixels, width, height, int(rgba.strides[0]),
            probability.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
            probability.size, error, len(error),
        )
    finally:
        library.rosto_mp_hair_destroy(hair)
    if hair_result < 0:
        raise RuntimeError(error.value.decode(errors="replace"))

    checks = {
        "face_detected": face_result == 1 and confidence.value > 0.35,
        "mesh_has_478_finite_points": (
            landmarks.shape == (LANDMARK_COUNT, 2)
            and bool(np.all(np.isfinite(landmarks)))
        ),
        "mesh_is_normalized": bool(
            np.all(landmarks > -0.25) and np.all(landmarks < 1.25)
        ),
        "hair_mask_returned": hair_result == 1,
        "hair_mask_is_finite_probability": bool(
            np.all(np.isfinite(probability))
            and np.min(probability) >= 0
            and np.max(probability) <= 1
        ),
        "hair_mask_has_foreground": float(np.mean(probability > 0.5)) > 0.01,
    }
    report = {
        "runtime": library.rosto_mp_runtime_version().decode(),
        "dimensions": [width, height],
        "faceConfidence": confidence.value,
        "landmarkBounds": [
            float(np.min(landmarks[:, 0])),
            float(np.min(landmarks[:, 1])),
            float(np.max(landmarks[:, 0])),
            float(np.max(landmarks[:, 1])),
        ],
        "hairProbability": {
            "minimum": float(np.min(probability)),
            "maximum": float(np.max(probability)),
            "mean": float(np.mean(probability)),
            "foregroundFraction": float(np.mean(probability > 0.5)),
        },
        "checks": checks,
        "passed": all(checks.values()),
    }
    args.output.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
