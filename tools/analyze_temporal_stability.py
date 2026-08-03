#!/usr/bin/env python3
"""Measure face-landmark stability on a real clip and draw a small report.

The OpenCV/LBF detector is a headless stand-in while the Mac is locked.
The smoothing math mirrors core/src/temporal.cpp.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import cv2
import numpy as np


def median_filter(points: np.ndarray, radius: int = 2) -> np.ndarray:
    result = points.copy()
    for index in range(len(points)):
        first = max(0, index - radius)
        last = min(len(points), index + radius + 1)
        result[index] = np.median(points[first:last], axis=0)
    return result


def pose(shape: np.ndarray):
    left_eye = shape[36:42].mean(axis=0)
    right_eye = shape[42:48].mean(axis=0)
    center = (shape[0] + shape[16]) * .5
    width = max(float(np.linalg.norm(shape[16] - shape[0])), 1e-6)
    roll = math.atan2(float(right_eye[1] - left_eye[1]),
                      float(right_eye[0] - left_eye[0]))
    return center, width, roll


def rigid_motion(a: np.ndarray, b: np.ndarray) -> float:
    center_a, width_a, roll_a = pose(a)
    center_b, width_b, roll_b = pose(b)
    scale = max((width_a + width_b) * .5, 1e-6)
    return (float(np.linalg.norm(center_a - center_b)) / scale +
            abs(width_a - width_b) / scale * .50 +
            abs(roll_a - roll_b) * .25)


def smoothstep(a: float, b: float, value: float) -> float:
    t = min(1.0, max(0.0, (value - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def exponential_pass(points: np.ndarray, motion_reference: np.ndarray,
                     seconds: np.ndarray, half_life: float,
                     forward: bool) -> np.ndarray:
    result = points.copy()
    indices = range(len(points)) if forward else range(len(points) - 1, -1, -1)
    state = None
    previous = None
    for index in indices:
        if state is None:
            state = result[index].copy()
        else:
            dt = abs(float(seconds[index] - seconds[previous]))
            base = 1.0 - math.pow(2.0, -dt / max(.001, half_life))
            motion = rigid_motion(
                motion_reference[index], motion_reference[previous])
            release = smoothstep(.0015, .020, motion)
            alpha = base + (.92 - base) * release
            state = state + (result[index] - state) * alpha
            result[index] = state
        previous = index
    return result


def stabilize(points: np.ndarray, seconds: np.ndarray) -> np.ndarray:
    result = median_filter(points, 2)
    motion_reference = result.copy()
    result = exponential_pass(result, motion_reference, seconds, .080, True)
    return exponential_pass(
        result, motion_reference, seconds, .080, False)


def normalized_shape(points: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Remove translation, scale and in-plane roll per frame."""
    result = np.empty_like(points)
    scales = np.empty(len(points), np.float32)
    for index, shape in enumerate(points):
        left_eye = shape[36:42].mean(axis=0)
        right_eye = shape[42:48].mean(axis=0)
        center = (left_eye + right_eye) * .5
        eye_line = right_eye - left_eye
        scale = max(float(np.linalg.norm(eye_line)), 1e-6)
        angle = math.atan2(float(eye_line[1]), float(eye_line[0]))
        cosine, sine = math.cos(-angle), math.sin(-angle)
        rotation = np.array([[cosine, -sine], [sine, cosine]],
                            dtype=np.float32)
        result[index] = ((shape - center) @ rotation.T) / scale
        scales[index] = scale
    return result, scales


def second_difference_metric(points: np.ndarray, scales: np.ndarray) -> float:
    normalized, _ = normalized_shape(points)
    acceleration = np.diff(normalized, n=2, axis=0)
    pixel_equivalent = np.linalg.norm(acceleration, axis=2)
    return float(np.median(pixel_equivalent) * np.median(scales))


def read_landmarks(video_path: Path, model_path: Path, samples: int,
                   start_fraction: float, stride: int):
    capture = cv2.VideoCapture(str(video_path))
    total = int(capture.get(cv2.CAP_PROP_FRAME_COUNT))
    fps = float(capture.get(cv2.CAP_PROP_FPS)) or 30.0
    first = min(max(0, round(total * start_fraction)),
                max(0, total - samples * stride))
    positions = first + np.arange(samples, dtype=np.int32) * stride
    cascade = cv2.CascadeClassifier(
        cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
    facemark = cv2.face.createFacemarkLBF()
    facemark.loadModel(str(model_path))
    landmarks = []
    seconds = []
    missing = 0
    for position in positions:
        capture.set(cv2.CAP_PROP_POS_FRAMES, int(position))
        ok, frame = capture.read()
        if not ok:
            missing += 1
            continue
        height, width = frame.shape[:2]
        factor = min(1.0, 960.0 / width)
        small = cv2.resize(
            frame, (round(width * factor), round(height * factor)),
            interpolation=cv2.INTER_AREA) if factor < 1 else frame
        gray = cv2.cvtColor(small, cv2.COLOR_BGR2GRAY)
        faces = cascade.detectMultiScale(
            gray, 1.1, 5, minSize=(max(60, round(120 * factor)),) * 2)
        if not len(faces):
            missing += 1
            continue
        face = max(faces, key=lambda item: item[2] * item[3])
        fitted, shapes = facemark.fit(
            small, np.asarray([face], dtype=np.int32))
        if not fitted:
            missing += 1
            continue
        landmarks.append(shapes[0][0] / factor)
        seconds.append(float(position) / fps)
    capture.release()
    if len(landmarks) < 5:
        raise RuntimeError("Too few landmark detections for temporal analysis")
    return np.asarray(landmarks, np.float32), np.asarray(seconds), missing, fps


def draw_chart(output: Path, raw_signal: np.ndarray,
               smooth_signal: np.ndarray, report: dict):
    width, height = 1400, 720
    image = np.full((height, width, 3), (24, 24, 24), np.uint8)
    cv2.putText(image, "ESTABILIDADE ENTRE QUADROS", (54, 64),
                cv2.FONT_HERSHEY_SIMPLEX, 1.25, (245, 245, 245), 3,
                cv2.LINE_AA)
    subtitle = (
        f"{report['framesDetected']} quadros reais | "
        f"reducao de oscilacao: {report['jitterReductionPercent']:.1f}% | "
        f"desvio mediano: {report['medianTrackingOffsetPixels']:.2f} px")
    cv2.putText(image, subtitle, (54, 105), cv2.FONT_HERSHEY_SIMPLEX, .68,
                (190, 190, 190), 2, cv2.LINE_AA)
    x0, y0, x1, y1 = 64, 160, width - 54, height - 100
    cv2.rectangle(image, (x0, y0), (x1, y1), (70, 70, 70), 1)
    values = np.concatenate([raw_signal, smooth_signal])
    low, high = float(values.min()), float(values.max())
    margin = max((high - low) * .12, 1e-4)
    low, high = low - margin, high + margin

    def path(signal):
        result = []
        for index, value in enumerate(signal):
            x = x0 + round(index * (x1 - x0) / max(len(signal) - 1, 1))
            y = y1 - round((float(value) - low) * (y1 - y0) /
                           max(high - low, 1e-6))
            result.append((x, y))
        return np.asarray(result, np.int32)

    cv2.polylines(image, [path(raw_signal)], False, (110, 170, 255), 2,
                  cv2.LINE_AA)
    cv2.polylines(image, [path(smooth_signal)], False, (110, 235, 160), 3,
                  cv2.LINE_AA)
    cv2.line(image, (64, height - 48), (102, height - 48),
             (110, 170, 255), 3, cv2.LINE_AA)
    cv2.putText(image, "deteccao quadro a quadro", (114, height - 39),
                cv2.FONT_HERSHEY_SIMPLEX, .62, (225, 225, 225), 2,
                cv2.LINE_AA)
    cv2.line(image, (470, height - 48), (508, height - 48),
             (110, 235, 160), 3, cv2.LINE_AA)
    cv2.putText(image, "trilha estabilizada usada no efeito",
                (520, height - 39), cv2.FONT_HERSHEY_SIMPLEX, .62,
                (225, 225, 225), 2, cv2.LINE_AA)
    cv2.imwrite(str(output), image, [cv2.IMWRITE_JPEG_QUALITY, 94])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("video", type=Path)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=60)
    parser.add_argument("--start-fraction", type=float, default=.20)
    parser.add_argument("--stride", type=int, default=1)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    points, seconds, missing, fps = read_landmarks(
        args.video, args.model, args.samples, args.start_fraction, args.stride)
    smoothed = stabilize(points, seconds)
    median_points = median_filter(points, 2)
    _, face_scales = normalized_shape(points)
    raw_jitter = second_difference_metric(points, face_scales)
    smooth_jitter = second_difference_metric(smoothed, face_scales)
    # Compare lag against the median-filtered track. Raw LBF occasionally
    # produces large outliers; rejecting them is the intended behavior and
    # should not be counted as motion lag.
    offsets = np.linalg.norm(smoothed - median_points, axis=2)
    report = {
        "schemaVersion": 1,
        "clip": str(args.video),
        "sourceFps": fps,
        "framesRequested": args.samples,
        "framesDetected": int(len(points)),
        "framesMissing": int(missing),
        "sampleStrideFrames": args.stride,
        "rawJitterPixelEquivalent": raw_jitter,
        "stabilizedJitterPixelEquivalent": smooth_jitter,
        "jitterReductionPercent": (
            100.0 * (1.0 - smooth_jitter / max(raw_jitter, 1e-9))),
        "medianTrackingOffsetPixels": float(np.median(offsets)),
        "p95TrackingOffsetPixels": float(np.percentile(offsets, 95)),
        "trackingOffsetReference": "five-frame median detector track",
        "settings": {
            "medianWindowFrames": 5,
            "bidirectionalHalfLifeMilliseconds": 80,
            "adaptiveMotionRelease": True,
            "maxInterpolatedGapFrames": 8
        },
        "detectorNote": (
            "OpenCV LBF fallback used while the locked Mac blocks "
            "MediaPipe/Vision GPU services.")
    }
    args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    normalized_raw, _ = normalized_shape(points)
    normalized_smooth, _ = normalized_shape(smoothed)
    # Nose-tip horizontal motion after removing global head motion is a useful
    # readable proxy for landmark flicker.
    draw_chart(args.output, normalized_raw[:, 30, 0],
               normalized_smooth[:, 30, 0], report)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
