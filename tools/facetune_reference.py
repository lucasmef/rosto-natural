#!/usr/bin/env python3
"""Normalize Facetune's global export drift without changing the face effect."""

from __future__ import annotations

import cv2
import numpy as np


def untreated_control_mask(frame, points):
    """Select low-detail pixels well away from the Facetune face treatment."""
    height, width = frame.shape[:2]
    jaw, brows = points[:17], points[17:27]
    face_width = float(np.linalg.norm(jaw[16] - jaw[0]))
    chin_y = float(np.max(jaw[:, 1]))
    brow_y = float(np.min(brows[:, 1]))
    face_height = max(chin_y - brow_y, face_width)
    x0 = max(0, round(float(np.min(jaw[:, 0])) - face_width * .18))
    x1 = min(width, round(float(np.max(jaw[:, 0])) + face_width * .18))
    y0 = max(0, round(brow_y - face_height * .72))
    y1 = min(height, round(chin_y + face_height * .18))

    control = np.ones((height, width), dtype=bool)
    control[y0:y1, x0:x1] = False
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
    gx = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
    gradient = np.hypot(gx, gy)
    stable_limit = np.percentile(gradient[control], 70)
    stable = control & (gradient <= stable_limit)
    stable[:4, :] = False
    stable[-4:, :] = False
    stable[:, :4] = False
    stable[:, -4:] = False
    return stable


def normalize_reference_export(raw, reference, control):
    """Invert the affine color drift fitted outside the treated face."""
    normalized = reference.astype(np.float32)
    fits = []
    for channel in range(3):
        source = raw[..., channel][control].astype(np.float32)
        encoded = reference[..., channel][control].astype(np.float32)
        valid = (source > 4) & (source < 251) & (encoded > 4) & (encoded < 251)
        source, encoded = source[valid], encoded[valid]
        if len(source) > 120000:
            step = max(1, len(source) // 120000)
            source, encoded = source[::step], encoded[::step]
        design = np.column_stack([source, np.ones_like(source)])
        slope, intercept = np.linalg.lstsq(
            design, encoded, rcond=None)[0]
        residual = encoded - (source * slope + intercept)
        keep = np.abs(residual) <= np.percentile(np.abs(residual), 85)
        design = np.column_stack([source[keep], np.ones(np.count_nonzero(keep))])
        slope, intercept = np.linalg.lstsq(
            design, encoded[keep], rcond=None)[0]
        slope = float(np.clip(slope, .90, 1.10))
        intercept = float(np.clip(intercept, -18.0, 18.0))
        normalized[..., channel] = (
            normalized[..., channel] - intercept) / max(slope, 1e-6)
        fits.append({"slope": slope, "intercept": intercept})
    normalized = np.clip(normalized, 0, 255).astype(np.uint8)
    before = float(np.mean(cv2.absdiff(raw, reference)[control]))
    after = float(np.mean(cv2.absdiff(raw, normalized)[control]))
    return normalized, {
        "channelsBgr": fits,
        "controlMeanAbsoluteDifferenceBefore": before,
        "controlMeanAbsoluteDifferenceAfter": after
    }
