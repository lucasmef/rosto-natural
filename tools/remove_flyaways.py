#!/usr/bin/env python3
"""Detect and remove isolated flyaway hairs from a portrait.

The MediaPipe HairSegmenter supplies a semantic hair probability map.  This
tool deliberately does not inpaint the normal hairstyle boundary: it builds a
smoothed, protected hair mass first, then considers only thin components that
leave that mass and continue over the background.

This is the reference/prototyping implementation.  The Premiere effect uses
the same mask contract but runs the sparse reconstruction on the GPU.
"""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import asdict, dataclass
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np


@dataclass(frozen=True)
class FlyawaySettings:
    strength: float = 0.70
    hair_threshold: float = 0.54
    fringe_threshold: float = 0.075
    max_distance_ratio: float = 0.085
    max_component_ratio: float = 0.55
    max_thickness_ratio: float = 0.018
    min_component_pixels: int = 5
    inpaint_radius_ratio: float = 0.0042


def _odd(value: float, minimum: int = 3) -> int:
    result = max(minimum, int(round(value)))
    return result if result % 2 else result + 1


def _kernel(size: int) -> np.ndarray:
    return cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (size, size))


def _largest_components(mask: np.ndarray, min_area: int) -> np.ndarray:
    count, labels, stats, _ = cv2.connectedComponentsWithStats(
        mask.astype(np.uint8), 8
    )
    if count <= 1:
        return np.zeros_like(mask, dtype=np.uint8)

    areas = stats[1:, cv2.CC_STAT_AREA]
    largest = int(np.max(areas))
    # Hair can be split into left/right lobes around a face. Preserve every
    # substantial part, not just the numerically largest component.
    keep_floor = max(min_area, int(largest * 0.025))
    kept = np.zeros_like(mask, dtype=np.uint8)
    for label in range(1, count):
        if stats[label, cv2.CC_STAT_AREA] >= keep_floor:
            kept[labels == label] = 255
    return kept


def _segment_hair(image_bgr: np.ndarray, model_path: Path) -> np.ndarray:
    base_options = mp.tasks.BaseOptions(model_asset_path=str(model_path))
    options = mp.tasks.vision.ImageSegmenterOptions(
        base_options=base_options,
        running_mode=mp.tasks.vision.RunningMode.IMAGE,
        output_category_mask=False,
        output_confidence_masks=True,
    )
    image_rgb = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2RGB)
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=image_rgb)
    with mp.tasks.vision.ImageSegmenter.create_from_options(options) as segmenter:
        result = segmenter.segment(mp_image)

    if not result.confidence_masks or len(result.confidence_masks) < 2:
        raise RuntimeError("HairSegmenter did not return a hair confidence mask")

    probability = np.asarray(result.confidence_masks[1].numpy_view(), np.float32)
    if probability.shape != image_bgr.shape[:2]:
        probability = cv2.resize(
            probability,
            (image_bgr.shape[1], image_bgr.shape[0]),
            interpolation=cv2.INTER_LINEAR,
        )
    return np.clip(probability, 0.0, 1.0)


def _dark_line_response(gray: np.ndarray, scale: int) -> np.ndarray:
    """Return a multi-scale response for thin dark lines over a lighter field."""
    responses: list[np.ndarray] = []
    for width in (_odd(scale * 0.75), _odd(scale), _odd(scale * 1.45)):
        closed = cv2.morphologyEx(gray, cv2.MORPH_CLOSE, _kernel(width))
        responses.append(cv2.subtract(closed, gray))
    return np.maximum.reduce(responses)


def _dark_horizontal_response(gray: np.ndarray, scale: int) -> np.ndarray:
    """Detect horizontal/curved wisps without reacting to vertical curtains."""
    responses: list[np.ndarray] = []
    for length in (_odd(scale * 0.8), _odd(scale * 1.25), _odd(scale * 1.8)):
        vertical = np.ones((length, 1), dtype=np.uint8)
        closed = cv2.morphologyEx(gray, cv2.MORPH_CLOSE, vertical)
        responses.append(cv2.subtract(closed, gray))
    return np.maximum.reduce(responses)


def _dark_vertical_response(gray: np.ndarray, scale: int) -> np.ndarray:
    responses: list[np.ndarray] = []
    for length in (_odd(scale * 0.8), _odd(scale * 1.25), _odd(scale * 1.8)):
        horizontal = np.ones((1, length), dtype=np.uint8)
        closed = cv2.morphologyEx(gray, cv2.MORPH_CLOSE, horizontal)
        responses.append(cv2.subtract(closed, gray))
    return np.maximum.reduce(responses)


def detect_flyaways(
    image_bgr: np.ndarray,
    hair_probability: np.ndarray,
    settings: FlyawaySettings,
) -> tuple[np.ndarray, dict[str, np.ndarray], dict[str, float | int]]:
    height, width = image_bgr.shape[:2]
    short_side = float(min(height, width))
    min_hair_area = max(32, int(height * width * 0.0015))

    strong = (hair_probability >= settings.hair_threshold).astype(np.uint8) * 255
    strong = cv2.morphologyEx(strong, cv2.MORPH_CLOSE, _kernel(_odd(short_side * 0.012)))
    strong = _largest_components(strong, min_hair_area)

    # A relatively broad opening removes thin branches while retaining the
    # normal hair volume. The subsequent close gives the protected silhouette
    # a stable boundary that will not be sent to the inpainting stage.
    open_size = _odd(short_side * 0.025)
    close_size = _odd(short_side * 0.018)
    protected = cv2.morphologyEx(strong, cv2.MORPH_OPEN, _kernel(open_size))
    protected = cv2.morphologyEx(protected, cv2.MORPH_CLOSE, _kernel(close_size))
    protected = _largest_components(protected, min_hair_area)

    # The opened mass deliberately excludes thin protrusions. The exterior-only
    # geometry below protects the forehead/inner hairline, while this padded
    # smooth silhouette protects the normal outer volume.
    protect_pad = _odd(short_side * 0.012)
    protected_padded = cv2.dilate(protected, _kernel(protect_pad))

    outside = (protected_padded == 0).astype(np.uint8)
    distance_to_hair = cv2.distanceTransform(outside, cv2.DIST_L2, 5)
    max_distance = max(12.0, short_side * settings.max_distance_ratio)
    proximity_band = (distance_to_hair > 0.0) & (distance_to_hair <= max_distance)

    # Only inspect the *outside* of the hairstyle. A plain distance band also
    # follows the inner hairline around the forehead, which must never be
    # inpainted. Robust row/column bounds separate that inner boundary from the
    # outer silhouette without requiring a face detector.
    solid = protected_padded > 0
    exterior_lr = np.zeros_like(solid)
    exterior_top = np.zeros_like(solid)
    xs = np.arange(width)
    ys = np.arange(height)
    for y in range(height):
        row = np.flatnonzero(solid[y])
        if row.size:
            exterior_lr[y, xs < row[0]] = True
            exterior_lr[y, xs > row[-1]] = True
    for x in range(width):
        column = np.flatnonzero(solid[:, x])
        if column.size:
            exterior_top[ys < column[0], x] = True
    exterior = exterior_lr | exterior_top
    search_band = proximity_band & exterior

    gray = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2GRAY)
    line_response = _dark_line_response(gray, _odd(short_side * 0.013))
    horizontal_response = _dark_horizontal_response(
        gray, _odd(short_side * 0.013)
    )
    vertical_response = _dark_vertical_response(
        gray, _odd(short_side * 0.013)
    )
    if np.any(search_band):
        line_threshold = max(
            3.0, float(np.percentile(line_response[search_band], 67.0))
        )
    else:
        line_threshold = 3.0
    dark_line = line_response >= line_threshold

    # The semantic branch catches low-contrast wisps; the dark-line branch
    # catches individual strands the segmenter considers background.
    semantic_seed = hair_probability >= settings.fringe_threshold
    # Even strands missed by the binary semantic mask normally receive a small
    # non-zero hair probability. Requiring that weak evidence rejects unrelated
    # dark background structures such as curtain folds.
    missed_strand_seed = (
        (hair_probability >= settings.fringe_threshold * 0.05)
        & dark_line
    )
    raw_candidate = search_band & (semantic_seed | missed_strand_seed)

    # Suppress the soft probability halo around the regular silhouette. A
    # candidate must either be visibly line-like or have meaningful hair
    # confidence away from the protected contour.
    min_departure = max(1.5, short_side * 0.006)
    raw_candidate &= dark_line | (
        (hair_probability >= settings.fringe_threshold * 1.65)
        & (distance_to_hair >= min_departure)
    )

    raw_u8 = raw_candidate.astype(np.uint8) * 255
    bridge = _odd(short_side * 0.005)
    grouped = cv2.morphologyEx(raw_u8, cv2.MORPH_CLOSE, _kernel(bridge))

    count, labels, stats, _ = cv2.connectedComponentsWithStats(grouped, 8)
    selected_groups = np.zeros_like(raw_u8)
    max_component_extent = short_side * settings.max_component_ratio
    max_thickness = max(3.0, short_side * settings.max_thickness_ratio)
    broad_open_size = _odd(max_thickness)
    broad_core = cv2.morphologyEx(
        grouped, cv2.MORPH_OPEN, _kernel(broad_open_size)
    )
    thin_branches = (grouped > 0) & (broad_core == 0)
    root_distance = max(5.0, short_side * 0.018)
    distance_to_unpadded_hair = cv2.distanceTransform(
        (protected == 0).astype(np.uint8), cv2.DIST_L2, 5
    )

    accepted = 0
    rejected = 0
    for label in range(1, count):
        area = int(stats[label, cv2.CC_STAT_AREA])
        if area < settings.min_component_pixels:
            continue
        x = int(stats[label, cv2.CC_STAT_LEFT])
        y = int(stats[label, cv2.CC_STAT_TOP])
        w = int(stats[label, cv2.CC_STAT_WIDTH])
        h = int(stats[label, cv2.CC_STAT_HEIGHT])
        component = labels == label

        extent = float(max(w, h))
        thickness = float(area) / max(1.0, extent)
        touches_root = bool(np.min(distance_to_unpadded_hair[component]) <= root_distance)
        is_thin = thickness <= max_thickness
        is_local = extent <= max_component_extent
        has_semantic_support = bool(
            np.percentile(hair_probability[component], 80.0)
            >= settings.fringe_threshold * 0.75
        )
        visible_rooted_line = bool(
            np.percentile(line_response[component], 70.0) >= line_threshold
            and np.min(distance_to_unpadded_hair[component]) <= root_distance * 0.55
        )

        if (
            touches_root
            and is_thin
            and is_local
            and (has_semantic_support or visible_rooted_line)
        ):
            selected_groups[component] = 255
            accepted += 1
        elif touches_root and has_semantic_support:
            # Dense side hair can merge several wisps into one wide semantic
            # component. Keep only its line-like branches; reject the broad
            # probability halo and neutral background folds.
            branch = (
                component
                & thin_branches
                & dark_line
                & (hair_probability >= settings.fringe_threshold * 0.20)
            )
            if np.count_nonzero(branch) >= settings.min_component_pixels:
                selected_groups[branch] = 255
                accepted += 1
            else:
                rejected += 1
        else:
            rejected += 1

    # Recover the actual 1–3 px strands from the grouped components. This
    # prevents the closing operation from becoming an oversized erase mask.
    selected = ((selected_groups > 0) & raw_candidate).astype(np.uint8) * 255
    selected = cv2.morphologyEx(selected, cv2.MORPH_CLOSE, _kernel(3))

    # HairSegmenter intentionally prioritizes the main hair mass and can miss
    # very fine, low-contrast arcs above the head. Detect those with a vertical
    # closing response, which ignores the vertical curtain folds in this kind
    # of portrait background.
    top_distance = max(max_distance, short_side * 0.14)
    top_search = (
        exterior_top
        & (distance_to_hair > 0.0)
        & (distance_to_hair <= top_distance)
    )
    if np.any(top_search):
        top_threshold = max(
            2.0, float(np.percentile(horizontal_response[top_search], 72.0))
        )
        top_raw = (
            top_search
            & (horizontal_response >= top_threshold)
            & (
                horizontal_response.astype(np.float32)
                >= vertical_response.astype(np.float32) * 1.15
            )
            & (hair_probability < settings.hair_threshold)
        )
        top_raw_u8 = top_raw.astype(np.uint8) * 255
        top_grouped = cv2.morphologyEx(top_raw_u8, cv2.MORPH_CLOSE, _kernel(3))
        top_count, top_labels, top_stats, _ = cv2.connectedComponentsWithStats(
            top_grouped, 8
        )
        for label in range(1, top_count):
            area = int(top_stats[label, cv2.CC_STAT_AREA])
            w = int(top_stats[label, cv2.CC_STAT_WIDTH])
            h = int(top_stats[label, cv2.CC_STAT_HEIGHT])
            if (
                settings.min_component_pixels <= area <= int(short_side * 1.8)
                and w >= max(2, int(h * 0.55))
            ):
                component = top_labels == label
                selected[component & top_raw] = 255
    else:
        top_threshold = 2.0

    # A wider, color-aware pass catches faint curved strands beside the head.
    # Neutral/vertical curtain structures are rejected unless the semantic
    # model itself supplies hair evidence.
    side_distance = max(max_distance, short_side * 0.14)
    side_search = (
        exterior_lr
        & (distance_to_hair > 0.0)
        & (distance_to_hair <= side_distance)
    )
    b, g, r = cv2.split(image_bgr.astype(np.float32))
    brown = r - (g + b) * 0.5
    neutral_background = side_search & (hair_probability < 0.003)
    if np.any(neutral_background):
        brown_threshold = max(
            7.0, float(np.percentile(brown[neutral_background], 86.0))
        )
    else:
        brown_threshold = 8.0
    side_raw = (
        side_search
        & (line_response >= line_threshold)
        & (
            (hair_probability >= settings.fringe_threshold * 0.05)
            | (brown >= brown_threshold)
        )
        & (
            (vertical_response.astype(np.float32)
             <= line_response.astype(np.float32) * 1.20)
            | (hair_probability >= settings.fringe_threshold * 0.20)
        )
    )
    side_grouped = cv2.morphologyEx(
        side_raw.astype(np.uint8) * 255, cv2.MORPH_CLOSE, _kernel(3)
    )
    side_count, side_labels, side_stats, _ = cv2.connectedComponentsWithStats(
        side_grouped, 8
    )
    for label in range(1, side_count):
        area = int(side_stats[label, cv2.CC_STAT_AREA])
        w = int(side_stats[label, cv2.CC_STAT_WIDTH])
        h = int(side_stats[label, cv2.CC_STAT_HEIGHT])
        thickness = float(area) / max(1.0, float(max(w, h)))
        if (
            settings.min_component_pixels <= area <= int(short_side * 1.8)
            and thickness <= max_thickness * 0.70
        ):
            component = side_labels == label
            selected[component & side_raw] = 255

    # Collapse thick candidate clusters to their line structure, then restore
    # at most a narrow 3 px support. Sparse masks are crucial for believable
    # background reconstruction.
    if np.any(selected) and hasattr(cv2, "ximgproc"):
        skeleton = cv2.ximgproc.thinning(
            selected, thinningType=cv2.ximgproc.THINNING_GUOHALL
        )
        selected = cv2.bitwise_and(
            cv2.dilate(skeleton, _kernel(3)), selected
        )

    # Never reconstruct the visible hairstyle boundary. Earlier prototypes
    # allowed candidates one pixel outside the protected mass; even a correct
    # inpaint then looked like a gray halo or a dark clump. Leave a clean moat
    # and remove only detached portions of flyaway strands.
    safety_moat = max(5.0, short_side * 0.014)
    selected[distance_to_hair < safety_moat] = 0

    # Strength governs coverage, not blur. At low values only the highest
    # confidence portion of the mask is used.
    strength = float(np.clip(settings.strength, 0.0, 1.0))
    confidence = np.maximum(
        np.clip(hair_probability / max(settings.fringe_threshold * 2.2, 1e-5), 0, 1),
        np.clip(line_response.astype(np.float32) / max(line_threshold * 2.0, 1), 0, 1),
    )
    if strength < 0.999:
        keep_threshold = 0.78 - strength * 0.58
        selected[(confidence < keep_threshold) & (hair_probability < 0.42)] = 0

    diagnostics: dict[str, np.ndarray] = {
        "probability": hair_probability,
        "strong": strong,
        "protected": protected_padded,
        "search_band": search_band.astype(np.uint8) * 255,
        "exterior_top": exterior_top.astype(np.uint8) * 255,
        "exterior_lr": exterior_lr.astype(np.uint8) * 255,
        "distance_to_hair": distance_to_hair,
        "line_response": line_response,
        "horizontal_response": horizontal_response,
        "vertical_response": vertical_response,
        "raw_candidate": raw_u8,
        "grouped_candidate": grouped,
        "selected": selected,
    }
    metrics: dict[str, float | int] = {
        "width": width,
        "height": height,
        "hair_pixels": int(np.count_nonzero(strong)),
        "protected_pixels": int(np.count_nonzero(protected_padded)),
        "candidate_pixels": int(np.count_nonzero(selected)),
        "accepted_components": accepted,
        "rejected_components": rejected,
        "line_threshold": round(line_threshold, 3),
        "top_line_threshold": round(top_threshold, 3),
        "brown_threshold": round(brown_threshold, 3),
        "max_search_distance": round(max_distance, 3),
        "safety_moat": round(safety_moat, 3),
    }
    return selected, diagnostics, metrics


def refine_guided_mask(
    hair_probability: np.ndarray,
    base_mask: np.ndarray,
    diagnostics: dict[str, np.ndarray],
    guide_roi: np.ndarray,
    safety_moat: float,
) -> np.ndarray:
    """Join faint line fragments inside an explicitly authorized region."""
    short_side = float(min(base_mask.shape))
    distance = diagnostics["distance_to_hair"]
    line = diagnostics["line_response"].astype(np.float32)
    horizontal = diagnostics["horizontal_response"].astype(np.float32)
    vertical = diagnostics["vertical_response"].astype(np.float32)
    safe = (
        (guide_roi > 0)
        & (
            (diagnostics["exterior_top"] > 0)
            | (diagnostics["exterior_lr"] > 0)
        )
        & (distance >= safety_moat)
        & (distance <= short_side * 0.15)
    )
    non_vertical = horizontal >= vertical * 0.92
    top = diagnostics["exterior_top"] > 0

    # Semantic evidence may be weak along a translucent strand. Use a lower
    # threshold only when the local direction is not a curtain-like vertical
    # fold, or when the top-of-head detector supplies horizontal evidence.
    raw = safe & (
        (hair_probability >= 0.004)
        | ((hair_probability >= 0.0025) & (line >= 1.0))
        | (top & non_vertical & (horizontal >= 2.0))
    )
    grouped = cv2.morphologyEx(
        raw.astype(np.uint8) * 255, cv2.MORPH_CLOSE, _kernel(7)
    )
    count, labels, stats, _ = cv2.connectedComponentsWithStats(grouped, 8)
    accepted = np.zeros_like(grouped)
    for label in range(1, count):
        area = int(stats[label, cv2.CC_STAT_AREA])
        w = int(stats[label, cv2.CC_STAT_WIDTH])
        h = int(stats[label, cv2.CC_STAT_HEIGHT])
        component = labels == label
        extent = float(max(w, h))
        thickness = area / max(1.0, extent)
        semantic_support = float(
            np.percentile(hair_probability[component], 75.0)
        )
        directional_support = float(
            np.percentile((horizontal - vertical)[component], 70.0)
        )
        if (
            3 <= area <= int(short_side * 1.5)
            and thickness <= short_side * 0.014
            and (semantic_support >= 0.0035 or directional_support >= 0.5)
        ):
            accepted[component] = 255

    if np.any(accepted) and hasattr(cv2, "ximgproc"):
        skeleton = cv2.ximgproc.thinning(
            accepted, thinningType=cv2.ximgproc.THINNING_GUOHALL
        )
        accepted = cv2.dilate(skeleton, _kernel(5))
    accepted[~safe] = 0
    return cv2.bitwise_or(base_mask, accepted)


def guide_roi_from_annotation(
    clean_bgr: np.ndarray, annotated_bgr: np.ndarray
) -> np.ndarray:
    """Convert red freehand circles from an annotated copy into a filled ROI."""
    if annotated_bgr.shape != clean_bgr.shape:
        annotated_bgr = cv2.resize(
            annotated_bgr,
            (clean_bgr.shape[1], clean_bgr.shape[0]),
            interpolation=cv2.INTER_LINEAR,
        )
    clean = clean_bgr.astype(np.int16)
    marked = annotated_bgr.astype(np.int16)
    delta = np.max(np.abs(marked - clean), axis=2)
    red_delta = (
        (marked[..., 2] - clean[..., 2])
        - np.maximum(
            marked[..., 1] - clean[..., 1],
            marked[..., 0] - clean[..., 0],
        )
    )
    strokes = ((delta > 12) & (red_delta > 10)).astype(np.uint8) * 255
    strokes = cv2.morphologyEx(strokes, cv2.MORPH_CLOSE, _kernel(3))

    count, labels, stats, _ = cv2.connectedComponentsWithStats(strokes, 8)
    roi = np.zeros_like(strokes)
    min_stroke_area = max(30, int(clean_bgr.shape[0] * clean_bgr.shape[1] * 0.0001))
    for label in range(1, count):
        if int(stats[label, cv2.CC_STAT_AREA]) < min_stroke_area:
            continue
        points = np.column_stack(np.where(labels == label)[::-1]).astype(np.int32)
        if len(points) < 3:
            continue
        hull = cv2.convexHull(points)
        cv2.fillConvexPoly(roi, hull, 255)
    return roi


def reconstruct_background(
    image_bgr: np.ndarray,
    flyaway_mask: np.ndarray,
    settings: FlyawaySettings,
    protected_hair: np.ndarray | None = None,
    allowed_roi: np.ndarray | None = None,
) -> np.ndarray:
    if not np.any(flyaway_mask):
        return image_bgr.copy()

    erase_mask = cv2.dilate(flyaway_mask, _kernel(3))
    short_side = float(min(image_bgr.shape[:2]))
    radius = max(2.0, short_side * settings.inpaint_radius_ratio)
    # With the safety moat above, every side of the sparse mask is background.
    # Fast marching replaces the strand from its surroundings. Navier–Stokes
    # tended to interpret long flyaways as curtain edges and continue them.
    reconstructed = cv2.inpaint(
        image_bgr, erase_mask, radius, cv2.INPAINT_TELEA
    )

    # Blend only across a sub-pixel feather around the sparse mask. This avoids
    # the visibly soft patches produced by blurring a broad matte.
    alpha = cv2.GaussianBlur(
        erase_mask.astype(np.float32) / 255.0, (0, 0), sigmaX=0.65
    )[..., None]
    if allowed_roi is not None:
        alpha *= (allowed_roi.astype(np.float32) / 255.0)[..., None]
    return np.clip(
        image_bgr.astype(np.float32) * (1.0 - alpha)
        + reconstructed.astype(np.float32) * alpha,
        0,
        255,
    ).astype(np.uint8)


def reconstruct_guided_background(
    image_bgr: np.ndarray,
    guide_roi: np.ndarray,
    diagnostics: dict[str, np.ndarray],
    settings: FlyawaySettings,
) -> tuple[np.ndarray, np.ndarray, dict[str, float | int]]:
    """Remove curved dark interruptions while preserving vertical background."""
    short_side = float(min(image_bgr.shape[:2]))
    strength = float(np.clip(settings.strength, 0.0, 1.0))
    if strength <= 0.0:
        return image_bgr.copy(), np.zeros(image_bgr.shape[:2], np.uint8), {
            "guided_strength": 0.0,
            "vertical_model_kernel": 0,
            "dark_interruption_threshold": 0,
            "guided_reconstruction_pixels": 0,
            "protected_overlap_pixels": 0,
        }

    distance = diagnostics["distance_to_hair"]
    exterior = (
        (diagnostics["exterior_top"] > 0)
        | (diagnostics["exterior_lr"] > 0)
    )
    # Intensity controls coverage as well as opacity. At 70% these values
    # reproduce the conservative v18 result; at 100% they include fainter,
    # slightly longer crown wisps without crossing the protected silhouette.
    max_distance_ratio = 0.105 + 0.065 * strength
    allowed = (
        (guide_roi > 0)
        & exterior
        & (distance > 0.0)
        & (distance <= short_side * max_distance_ratio)
    )

    # A long vertical closing retains persistent curtain folds and the main
    # hair mass, but removes short/curved dark interruptions in each column.
    vertical_extent_ratio = 0.120 + 0.050 * strength
    vertical_extent = _odd(short_side * vertical_extent_ratio)
    background = cv2.morphologyEx(
        image_bgr,
        cv2.MORPH_CLOSE,
        np.ones((vertical_extent, 1), dtype=np.uint8),
    )
    gray = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2GRAY)
    background_gray = cv2.cvtColor(background, cv2.COLOR_BGR2GRAY)
    dark_interruption = (
        background_gray.astype(np.int16) - gray.astype(np.int16)
    )
    dark_threshold = max(1, int(round(5.0 - 4.0 * strength)))
    mask = (
        allowed & (dark_interruption >= dark_threshold)
    ).astype(np.uint8) * 255
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, _kernel(3))
    mask[~allowed] = 0

    # 70% is the calibrated natural preset. Lower values fade the correction;
    # 70–100% increase coverage while keeping selected pixels fully rebuilt.
    blend_gain = min(1.0, strength / 0.70)
    alpha = cv2.GaussianBlur(
        (mask > 0).astype(np.float32), (0, 0), sigmaX=0.65
    )[..., None]
    alpha *= blend_gain
    alpha *= (guide_roi.astype(np.float32) / 255.0)[..., None]
    result = np.clip(
        image_bgr.astype(np.float32) * (1.0 - alpha)
        + background.astype(np.float32) * alpha,
        0,
        255,
    ).astype(np.uint8)
    metrics: dict[str, float | int] = {
        "guided_strength": round(strength, 4),
        "guided_blend_gain": round(blend_gain, 4),
        "max_guided_distance": round(short_side * max_distance_ratio, 3),
        "vertical_model_kernel": vertical_extent,
        "dark_interruption_threshold": dark_threshold,
        "guided_reconstruction_pixels": int(np.count_nonzero(mask)),
        "protected_overlap_pixels": int(
            np.count_nonzero((mask > 0) & (diagnostics["protected"] > 0))
        ),
    }
    return result, mask, metrics


def _colorize_probability(probability: np.ndarray) -> np.ndarray:
    value = np.clip(probability * 255.0, 0, 255).astype(np.uint8)
    return cv2.applyColorMap(value, cv2.COLORMAP_TURBO)


def _fit_tile(image: np.ndarray, width: int, height: int) -> np.ndarray:
    scale = min(width / image.shape[1], height / image.shape[0])
    resized = cv2.resize(
        image,
        (max(1, int(round(image.shape[1] * scale))), max(1, int(round(image.shape[0] * scale)))),
        interpolation=cv2.INTER_AREA,
    )
    canvas = np.full((height, width, 3), 28, dtype=np.uint8)
    y = (height - resized.shape[0]) // 2
    x = (width - resized.shape[1]) // 2
    canvas[y : y + resized.shape[0], x : x + resized.shape[1]] = resized
    return canvas


def make_debug_sheet(
    original: np.ndarray,
    result: np.ndarray,
    diagnostics: dict[str, np.ndarray],
) -> np.ndarray:
    overlay = original.copy()
    mask = diagnostics["selected"] > 0
    overlay[mask] = (
        overlay[mask].astype(np.float32) * 0.22
        + np.array([35, 35, 255], np.float32) * 0.78
    ).astype(np.uint8)

    protected_overlay = original.copy()
    protected = diagnostics["protected"] > 0
    protected_overlay[protected] = (
        protected_overlay[protected].astype(np.float32) * 0.50
        + np.array([70, 210, 70], np.float32) * 0.50
    ).astype(np.uint8)

    raw_overlay = original.copy()
    raw = diagnostics["raw_candidate"] > 0
    raw_overlay[raw] = (
        raw_overlay[raw].astype(np.float32) * 0.25
        + np.array([0, 210, 255], np.float32) * 0.75
    ).astype(np.uint8)

    panels = [
        ("Original", original),
        ("Resultado", result),
        ("Mascara a remover", overlay),
        ("Cabelo protegido", protected_overlay),
        ("Probabilidade cabelo", _colorize_probability(diagnostics["probability"])),
        ("Candidatos antes do filtro", raw_overlay),
    ]
    tile_w = 360
    tile_h = 520
    rendered: list[np.ndarray] = []
    for title, panel in panels:
        tile = _fit_tile(panel, tile_w, tile_h)
        cv2.rectangle(tile, (0, 0), (tile_w, 42), (18, 18, 18), -1)
        cv2.putText(
            tile,
            title,
            (14, 28),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.62,
            (245, 245, 245),
            1,
            cv2.LINE_AA,
        )
        rendered.append(tile)
    return np.vstack(
        (np.hstack(rendered[:3]), np.hstack(rendered[3:]))
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--model",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "models" / "hair_segmenter.tflite",
    )
    parser.add_argument("--mask-output", type=Path)
    parser.add_argument("--debug-output", type=Path)
    parser.add_argument("--metrics-output", type=Path)
    parser.add_argument(
        "--guide-image",
        type=Path,
        help="Optional copy with red circles; limits processing to those regions.",
    )
    parser.add_argument("--strength", type=float, default=0.70)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    os.environ.setdefault("MEDIAPIPE_DISABLE_GPU", "1")
    image = cv2.imread(str(args.input), cv2.IMREAD_COLOR)
    if image is None:
        raise FileNotFoundError(f"Could not read input image: {args.input}")
    if not args.model.is_file():
        raise FileNotFoundError(f"HairSegmenter model not found: {args.model}")

    settings = FlyawaySettings(strength=float(np.clip(args.strength, 0.0, 1.0)))
    probability = _segment_hair(image, args.model)
    mask, diagnostics, metrics = detect_flyaways(image, probability, settings)
    guide_roi = None
    guided_result = None
    if args.guide_image:
        annotated = cv2.imread(str(args.guide_image), cv2.IMREAD_COLOR)
        if annotated is None:
            raise FileNotFoundError(
                f"Could not read guide image: {args.guide_image}"
            )
        guide_roi = guide_roi_from_annotation(image, annotated)
        mask = cv2.bitwise_and(mask, guide_roi)
        mask = refine_guided_mask(
            probability,
            mask,
            diagnostics,
            guide_roi,
            float(metrics["safety_moat"]),
        )
        guided_result, mask, guided_metrics = reconstruct_guided_background(
            image, guide_roi, diagnostics, settings
        )
        metrics.update(guided_metrics)
        diagnostics["selected"] = mask
        diagnostics["guide_roi"] = guide_roi
        metrics["guide_roi_pixels"] = int(np.count_nonzero(guide_roi))
        metrics["candidate_pixels"] = int(np.count_nonzero(mask))
    if guided_result is not None:
        result = guided_result
    else:
        result = reconstruct_background(
            image,
            mask,
            settings,
            protected_hair=diagnostics["protected"],
            allowed_roi=guide_roi,
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(args.output), result):
        raise RuntimeError(f"Could not write output image: {args.output}")
    if args.mask_output:
        args.mask_output.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(args.mask_output), mask)
    if args.debug_output:
        args.debug_output.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(
            str(args.debug_output),
            make_debug_sheet(image, result, diagnostics),
            [cv2.IMWRITE_JPEG_QUALITY, 94],
        )
    if args.metrics_output:
        args.metrics_output.parent.mkdir(parents=True, exist_ok=True)
        args.metrics_output.write_text(
            json.dumps(
                {"settings": asdict(settings), "metrics": metrics},
                indent=2,
                ensure_ascii=False,
            )
            + "\n",
            encoding="utf-8",
        )

    print(json.dumps(metrics, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
