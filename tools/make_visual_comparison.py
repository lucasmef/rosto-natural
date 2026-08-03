#!/usr/bin/env python3
"""Create a labeled raw / Rosto Natural / Facetune comparison image."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import subprocess

import cv2


def middle_frame(path: Path, fraction: float = .5):
    cap = cv2.VideoCapture(str(path))
    count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    cap.set(cv2.CAP_PROP_POS_FRAMES, max(0, round((count - 1) * fraction)))
    ok, frame = cap.read()
    cap.release()
    if not ok:
        raise RuntimeError(f"Could not decode {path}")
    return frame


def panel(frame, title, height=700):
    h, w = frame.shape[:2]
    frame = cv2.resize(frame, (round(w * height / h), height),
                       interpolation=cv2.INTER_AREA)
    canvas = cv2.copyMakeBorder(frame, 58, 0, 0, 0, cv2.BORDER_CONSTANT,
                                value=(25, 25, 25))
    cv2.putText(canvas, title, (18, 38), cv2.FONT_HERSHEY_SIMPLEX, .82,
                (245, 245, 245), 2, cv2.LINE_AA)
    return canvas


def largest_face(frame):
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    cascade = cv2.CascadeClassifier(
        cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
    faces = cascade.detectMultiScale(gray, 1.1, 5, minSize=(120, 120))
    if not len(faces):
        return None
    return tuple(max(faces, key=lambda item: item[2] * item[3]))


def face_crop(frame, face, horizontal_pad=.28, top_pad=.38,
              bottom_pad=.24):
    x, y, w, h = face
    x0 = max(0, round(x - w * horizontal_pad))
    x1 = min(frame.shape[1], round(x + w * (1 + horizontal_pad)))
    y0 = max(0, round(y - h * top_pad))
    y1 = min(frame.shape[0], round(y + h * (1 + bottom_pad)))
    return frame[y0:y1, x0:x1]


def jaw_crop(frame, face, horizontal_pad=.26, top_fraction=.32,
             bottom_pad=.22):
    """Crop cheeks, jaw and chin so contour leakage is easy to inspect."""
    x, y, w, h = face
    x0 = max(0, round(x - w * horizontal_pad))
    x1 = min(frame.shape[1], round(x + w * (1 + horizontal_pad)))
    y0 = max(0, round(y + h * top_fraction))
    y1 = min(frame.shape[0], round(y + h * (1 + bottom_pad)))
    return frame[y0:y1, x0:x1]


class MediaPipeMeshDetector:
    """Reusable 478-point detector for batch validation."""

    def __init__(self, model_path: Path):
        import mediapipe as mp
        self.mp = mp
        if hasattr(mp, "tasks"):
            self.mode = "tasks"
            options = mp.tasks.vision.FaceLandmarkerOptions(
                base_options=mp.tasks.BaseOptions(
                    model_asset_path=str(model_path),
                    delegate=mp.tasks.BaseOptions.Delegate.CPU),
                running_mode=mp.tasks.vision.RunningMode.IMAGE,
                num_faces=1,
                min_face_detection_confidence=.35,
                min_face_presence_confidence=.35,
                min_tracking_confidence=.35)
            self.detector = (
                mp.tasks.vision.FaceLandmarker.create_from_options(options))
        else:
            self.mode = "solutions"
            self.detector = mp.solutions.face_mesh.FaceMesh(
                static_image_mode=True, max_num_faces=1,
                refine_landmarks=True, min_detection_confidence=.35)

    def landmarks(self, frame):
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        if self.mode == "solutions":
            result = self.detector.process(rgb)
            if not result.multi_face_landmarks:
                return None
            return result.multi_face_landmarks[0].landmark
        image = self.mp.Image(
            image_format=self.mp.ImageFormat.SRGB, data=rgb)
        result = self.detector.detect(image)
        return result.face_landmarks[0] if result.face_landmarks else None

    def write(self, frame, output_path: Path):
        points = self.landmarks(frame)
        if points is None:
            return False
        if len(points) != 478:
            raise RuntimeError(f"Expected 478 landmarks, got {len(points)}")
        with output_path.open("wb") as stream:
            stream.write(struct.pack(
                "<8sIf", b"RSTMESH1", len(points), 1.0))
            for point in points:
                stream.write(struct.pack("<ff", point.x, point.y))
        return True

    def close(self):
        self.detector.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def write_face_mesh(frame, model_path: Path, output_path: Path,
                    detector=None):
    if detector is not None:
        return detector.write(frame, output_path)
    with MediaPipeMeshDetector(model_path) as local_detector:
        return local_detector.write(frame, output_path)


def _resample_loop(source, count):
    import numpy as np
    source = np.asarray(source, dtype=np.float32)
    closed = np.vstack([source, source[0]])
    lengths = np.linalg.norm(np.diff(closed, axis=0), axis=1)
    cumulative = np.concatenate([[0], np.cumsum(lengths)])
    targets = np.linspace(0, cumulative[-1], count, endpoint=False)
    result = []
    for target in targets:
        segment = min(np.searchsorted(cumulative, target, side="right") - 1,
                      len(source) - 1)
        t = (target - cumulative[segment]) / max(lengths[segment], 1e-6)
        result.append(closed[segment] * (1 - t) + closed[segment + 1] * t)
    return result


def _resample_path(source, count):
    import numpy as np
    source = np.asarray(source, dtype=np.float32)
    lengths = np.linalg.norm(np.diff(source, axis=0), axis=1)
    cumulative = np.concatenate([[0], np.cumsum(lengths)])
    targets = np.linspace(0, cumulative[-1], count)
    result = []
    for target in targets:
        segment = min(np.searchsorted(
            cumulative, target, side="right") - 1, len(source) - 2)
        segment = max(0, segment)
        t = (target - cumulative[segment]) / max(lengths[segment], 1e-6)
        result.append(
            source[segment] * (1 - t) + source[segment + 1] * t)
    return result


def write_lbf_mesh(frame, model_path: Path, output_path: Path, facemark=None):
    import numpy as np
    face = largest_face(frame)
    if face is None:
        return None
    if facemark is None:
        facemark = cv2.face.createFacemarkLBF()
        facemark.loadModel(str(model_path))
    ok, fitted = facemark.fit(frame, np.asarray([face], dtype=np.int32))
    if not ok:
        return None
    p = fitted[0][0]
    h, w = frame.shape[:2]
    p[:, 0] /= w
    p[:, 1] /= h
    mesh = np.zeros((478, 2), dtype=np.float32)
    oval_ids = [10,338,297,332,284,251,389,356,454,323,361,288,397,365,
                379,378,400,377,152,148,176,149,150,136,172,58,132,93,
                234,127,162,21,54,103,67,109]
    left_eye_ids = [33,7,163,144,145,153,154,155,133,173,157,158,159,160,
                    161,246]
    right_eye_ids = [362,382,381,380,374,373,390,249,263,466,388,387,386,
                     385,384,398]
    lip_ids = [61,146,91,181,84,17,314,405,321,375,291,308,324,318,402,
               317,14,87,178,88]
    left_brow_ids = [70,63,105,66,107]
    right_brow_ids = [336,296,334,293,300]

    jaw = p[:17]
    left_x, right_x = jaw[:, 0].min(), jaw[:, 0].max()
    chin_y = jaw[:, 1].max()
    brow_y = p[17:27, 1].min()
    face_height = chin_y - brow_y
    top_y = brow_y - face_height * .42
    center = np.array([(left_x + right_x) / 2, (top_y + chin_y) / 2])
    rx, ry = (right_x - left_x) * .52, (chin_y - top_y) * .52
    for i, index in enumerate(oval_ids):
        theta = -np.pi / 2 + i * 2 * np.pi / len(oval_ids)
        mesh[index] = center + [rx * np.cos(theta), ry * np.sin(theta)]
    # MediaPipe's oval travels down the right jaw, through the chin and back
    # up the left. Replace the lower synthetic ellipse with the fitted jaw.
    jaw_ids = oval_ids[8:29]
    jaw_path = p[15:0:-1]
    for index, value in zip(
            jaw_ids, _resample_path(jaw_path, len(jaw_ids))):
        mesh[index] = value
    for ids, source in [
            (left_eye_ids, p[36:42]), (right_eye_ids, p[42:48]),
            (lip_ids, p[48:60]), (left_brow_ids, p[17:22]),
            (right_brow_ids, p[22:27])]:
        for index, value in zip(ids, _resample_loop(source, len(ids))):
            mesh[index] = value
    # Explicit feature anchors consumed by the C++ mask generator.
    mesh[33], mesh[133] = p[36], p[39]
    mesh[362], mesh[263] = p[42], p[45]
    mesh[1], mesh[2] = p[30], p[33]
    mesh[98], mesh[327] = p[31], p[35]
    mesh[205] = (p[3] + p[31]) * .5
    mesh[425] = (p[13] + p[35]) * .5
    mesh[234], mesh[454] = p[1], p[15]
    with output_path.open("wb") as stream:
        stream.write(struct.pack("<8sIf", b"RSTMESH1", 478, 1.0))
        stream.write(mesh.astype("<f4").tobytes())
    return face


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("raw", type=Path)
    ap.add_argument("facetune", type=Path)
    ap.add_argument("cli", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--fraction", type=float, default=.5)
    ap.add_argument("--zoom", action="store_true",
                    help="Crop all three panels around the detected face")
    ap.add_argument("--jaw-zoom", action="store_true",
                    help="Crop cheeks, jaw and chin for contour inspection")
    ap.add_argument("--landmarks", choices=("mediapipe", "lbf"),
                    default="lbf")
    ap.add_argument("--panel-height", type=int, default=700)
    ap.add_argument("--preserve-texture", type=float)
    args = ap.parse_args()
    raw = middle_frame(args.raw, args.fraction)
    treated = middle_frame(args.facetune, args.fraction)
    treated = cv2.resize(treated, (raw.shape[1], raw.shape[0]))
    work = args.output.parent
    prefix = args.output.stem
    raw_ppm = work / f"{prefix}-raw.ppm"
    ours_ppm = work / f"{prefix}-ours.ppm"
    mesh_bin = work / f"{prefix}-mesh.bin"
    cv2.imwrite(str(raw_ppm), raw)
    project = args.cli.resolve().parent.parent
    face = largest_face(raw)
    if args.landmarks == "mediapipe":
        mesh_ok = write_face_mesh(
            raw, project / "models/face_landmarker.task", mesh_bin)
    else:
        mesh_ok = write_lbf_mesh(
            raw, project / "models/lbfmodel.yaml", mesh_bin) is not None
    if mesh_ok:
        command = [str(args.cli), str(raw_ppm), str(ours_ppm),
                   "--mesh", str(mesh_bin)]
    else:
        command = [str(args.cli), str(raw_ppm), str(ours_ppm)]
    if args.preserve_texture is not None:
        command.extend([
            "--preserve-texture", str(args.preserve_texture)])
    subprocess.run(command, check=True)
    ours = cv2.imread(str(ours_ppm))
    if args.jaw_zoom and face is not None:
        raw = jaw_crop(raw, face)
        ours = jaw_crop(ours, face)
        treated = jaw_crop(treated, face)
    elif args.zoom and face is not None:
        raw = face_crop(raw, face)
        ours = face_crop(ours, face)
        treated = face_crop(treated, face)
    combined = cv2.hconcat([
        panel(raw, "1  CRU", args.panel_height),
        panel(ours, "2  ROSTO NATURAL  v0.10", args.panel_height),
        panel(treated, "3  FACETUNE (REFERENCIA)", args.panel_height)
    ])
    footer = cv2.copyMakeBorder(combined, 0, 50, 0, 0, cv2.BORDER_CONSTANT,
                                value=(25, 25, 25))
    cv2.putText(footer,
                "Contorno protegido; tratamento zerado fora do rosto. "
                "Sem remodelagem aplicada.",
                (18, footer.shape[0] - 16), cv2.FONT_HERSHEY_SIMPLEX, .62,
                (190, 190, 190), 1, cv2.LINE_AA)
    cv2.imwrite(str(args.output), footer,
                [cv2.IMWRITE_JPEG_QUALITY, 94])
    raw_ppm.unlink(missing_ok=True)
    ours_ppm.unlink(missing_ok=True)
    mesh_bin.unlink(missing_ok=True)
    print(args.output)


if __name__ == "__main__":
    main()
