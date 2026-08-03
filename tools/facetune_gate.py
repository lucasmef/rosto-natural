#!/usr/bin/env python3
"""Fail validation when Rosto Natural moves away from Facetune."""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics


def check(name, actual, target, tolerance, mode="distance"):
    if mode == "minimum":
        passed = actual >= target - tolerance
        error = actual - target
    elif mode == "maximum":
        passed = actual <= target + tolerance
        error = actual - target
    else:
        error = actual - target
        passed = abs(error) <= tolerance
    return {
        "name": name,
        "actual": actual,
        "facetune": target,
        "error": error,
        "tolerance": tolerance,
        "passed": passed
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--semantic", type=Path, required=True)
    parser.add_argument("--contour", type=Path, required=True)
    parser.add_argument("--texture", type=Path, required=True)
    parser.add_argument("--similarity", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    semantic = json.loads(args.semantic.read_text(encoding="utf-8"))
    contour = json.loads(args.contour.read_text(encoding="utf-8"))
    texture = json.loads(args.texture.read_text(encoding="utf-8"))
    similarity = json.loads(
        args.similarity.read_text(encoding="utf-8"))
    ours = semantic["rostoNaturalMedian"]
    reference = semantic["facetuneMedian"]
    edge = contour["aggregateMedian"]
    detail = texture["rostoNaturalMedian"]
    detail_reference = texture["facetuneMedian"]
    direct = similarity["aggregateMedian"]
    per_clip = defaultdict(list)
    for sample in similarity["samples"]:
        per_clip[sample["clip"]].append(sample)
    per_clip_similarity = {
        clip: {
            "colorImprovementPercent": statistics.median(
                sample["deltaEImprovementPercent"] for sample in samples),
            "structureImprovementPercent": statistics.median(
                sample["gradientImprovementPercent"] for sample in samples)
        }
        for clip, samples in per_clip.items()
    }
    worst_clip_color = min(
        item["colorImprovementPercent"]
        for item in per_clip_similarity.values())
    worst_clip_structure = min(
        item["structureImprovementPercent"]
        for item in per_clip_similarity.values())

    checks = [
        check("microtextura das bochechas",
              detail["microTextureRatio"],
              detail_reference["microTextureRatio"], .030),
        check("textura fina das bochechas",
              detail["fineTextureRatio"],
              detail_reference["fineTextureRatio"], .040),
        check("textura média das bochechas",
              detail["midTextureRatio"],
              detail_reference["midTextureRatio"], .050),
        check("nitidez da mandíbula",
              edge["oursJawSharpnessRatio"],
              edge["facetuneJawSharpnessRatio"], .020, "minimum"),
        check("alteração fora do rosto",
              edge["oursOutsideMeanCodeValue"],
              edge["facetuneOutsideMeanCodeValue"], .250, "maximum"),
        check("luminância da pele",
              ours["skinLumaDelta"], reference["skinLumaDelta"], .0010),
        check("crominância A da pele",
              ours["skinChromaADelta"],
              reference["skinChromaADelta"], .0005),
        check("crominância B da pele",
              ours["skinChromaBDelta"],
              reference["skinChromaBDelta"], .0005),
        check("luminância das olheiras",
              ours["underEyeLumaDelta"],
              reference["underEyeLumaDelta"], .0010),
        check("saturação dos olhos",
              ours["eyesSaturationDelta"],
              reference["eyesSaturationDelta"], .0010),
        check("luminância dos olhos",
              ours["eyesLumaDelta"],
              reference["eyesLumaDelta"], .0010),
        check("saturação dos lábios",
              ours["lipsSaturationDelta"],
              reference["lipsSaturationDelta"], .0010),
        check("luminância dos lábios",
              ours["lipsLumaDelta"],
              reference["lipsLumaDelta"], .0010),
        check("ganho direto de similaridade de cor",
              direct["deltaEImprovementPercent"], 3.0, 0.0, "minimum"),
        check("ganho direto de similaridade estrutural",
              direct["gradientImprovementPercent"], 10.0, 0.0, "minimum"),
        check("pior ganho mediano de cor por vídeo",
              worst_clip_color, 0.0, 0.0, "minimum"),
        check("pior ganho mediano estrutural por vídeo",
              worst_clip_structure, 0.0, 0.0, "minimum")
    ]
    passed = all(item["passed"] for item in checks)
    report = {
        "schemaVersion": 1,
        "reference": "Facetune exports paired with the raw videos",
        "normalization":
            "global export drift fitted only outside the face",
        "sampleCount": min(
            semantic["sampleCount"], contour["sampleCount"],
            texture["sampleCount"], similarity["sampleCount"]),
        "perClipSimilarity": per_clip_similarity,
        "passed": passed,
        "checks": checks
    }
    args.output.write_text(
        json.dumps(report, indent=2, ensure_ascii=False),
        encoding="utf-8")
    for item in checks:
        status = "PASS" if item["passed"] else "FAIL"
        print(
            f"{status:4} {item['name']}: "
            f"{item['actual']:.6f} vs {item['facetune']:.6f}")
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
