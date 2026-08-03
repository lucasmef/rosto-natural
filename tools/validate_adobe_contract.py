#!/usr/bin/env python3
"""Validate the Adobe adapter/panel contract without launching Premiere."""

from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def main() -> int:
    effect = read("adobe/EffectMain.cpp")
    pipl = read("adobe/RostoNaturalPiPL.r")
    adobe_cmake = read("adobe/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")
    panel = read("uxp/js/premiere.js")
    panel_main = read("uxp/js/main.js")
    build_adobe = read("scripts/build_adobe.sh")
    installer = read("scripts/install_when_ready.sh")
    manifest = json.loads(read("uxp/manifest.json"))

    version_match = re.search(
        r"PF_VERSION\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)", effect
    )
    native_version = (
        ".".join(version_match.groups()) if version_match else ""
    )
    cmake_version_match = re.search(
        r"project\(RostoNatural VERSION ([0-9.]+)", root_cmake
    )
    cmake_version = (
        cmake_version_match.group(1) if cmake_version_match else ""
    )
    pipl_match = re.search(
        r'AE_Effect_Match_Name\s*\{\s*"([^"]+)"', pipl
    )
    panel_match = re.search(r'const MATCH_NAME = "([^"]+)"', panel)
    pipl_name = pipl_match.group(1) if pipl_match else ""
    panel_name = panel_match.group(1) if panel_match else ""

    native_labels = set(
        re.findall(r'PF_ADD_(?:SLIDER|COLOR|CHECKBOX)\("([^"]+)"', effect)
    )
    panel_labels = set(
        re.findall(r'\b\w+:\s*"([^"]+)"', panel.split("};", 1)[0])
    )
    required_labels = {
        "Intensidade",
        "Pele",
        "Preservar textura",
        "Olheiras",
        "Uniformizar tom",
        "Brilho da pele",
        "Olhos",
        "Cabelos rebeldes",
        "Blush",
        "Cor do blush",
        "Lábios",
        "Cor dos lábios",
        "Antes / depois",
        "_Analysis ID",
        "_Capture Mode",
        "_Cache Version",
    }

    unsafe_render_flags = {
        "PF_OutFlag_PIX_INDEPENDENT",
        "PF_OutFlag_USE_OUTPUT_EXTENT",
        "PF_OutFlag2_FLOAT_COLOR_AWARE",
        "PF_OutFlag2_SUPPORTS_SMART_RENDER",
    }
    declared_flags = {
        flag for flag in unsafe_render_flags if flag in effect or flag in pipl
    }
    # Explanatory comments are allowed to mention flags; only assignments and
    # the PiPL resource are authoritative.
    active_flag_text = re.sub(r"//[^\n]*", "", effect) + "\n" + pipl
    active_flags = {
        flag for flag in unsafe_render_flags if flag in active_flag_text
    }

    defaults = {
        key: int(value)
        for key, value in re.findall(
            r"\b(master|skin|texture|underEye|tone|shine|eyes|flyaways|"
            r"blush|lips):\s*(\d+)",
            panel_main.split("};", 1)[0],
        )
    }
    expected_defaults = {
        "master": 72,
        "skin": 58,
        "texture": 75,
        "underEye": 42,
        "tone": 24,
        "shine": 30,
        "eyes": 18,
        "flyaways": 0,
        "blush": 0,
        "lips": 20,
    }

    checks = {
        "manifest_v5_for_premiere": (
            manifest.get("manifestVersion") == 5
            and manifest.get("host", {}).get("app") == "premierepro"
            and manifest.get("host", {}).get("minVersion") == "26.3.0"
        ),
        "native_and_panel_versions_match": (
            native_version
            == cmake_version
            == manifest.get("version")
            == "0.11.0"
        ),
        "match_names_agree": (
            pipl_name == panel_name == "com.lucas.RostoNatural"
        ),
        "parameter_labels_agree": (
            required_labels <= native_labels
            and required_labels <= panel_labels
        ),
        "panel_defaults_match_approved_preset": defaults == expected_defaults,
        "render_capabilities_are_not_overdeclared": not active_flags,
        "classic_8_bit_render_is_implemented": (
            "PF_Cmd_RENDER" in effect
            and "PF_Pixel8" in effect
            and "PF_Cmd_SMART_RENDER" not in effect
        ),
        "pipl_is_compiled_into_bundle": (
            '#include "AE_General.r"' in pipl
            and "RostoNaturalPiPL.rsrc" in adobe_cmake
            and "ROSTO_REZ_EXECUTABLE" in adobe_cmake
        ),
        "models_are_bundled": (
            "face_landmarker.task" in adobe_cmake
            and "hair_segmenter.tflite" in adobe_cmake
        ),
        "mediapipe_bridge_is_required": (
            "ROSTO_MEDIAPIPE_BRIDGE_LIBRARY" in root_cmake
            and "-DROSTO_WITH_MEDIAPIPE=ON" in build_adobe
            and "libRostoMediaPipe.dylib" in build_adobe
        ),
        "installer_requires_confirmation_and_closed_host": (
            '--confirm' in installer
            and 'pgrep -x "Adobe Premiere Pro 2026"' in installer
            and 'pgrep -x "Adobe After Effects 2026"' in installer
            and "dist/backups" in installer
        ),
    }
    report = {
        "nativeVersion": native_version,
        "cmakeVersion": cmake_version,
        "manifestVersion": manifest.get("version"),
        "nativeMatchName": pipl_name,
        "panelMatchName": panel_name,
        "mentionedUnsafeFlags": sorted(declared_flags),
        "activeUnsafeFlags": sorted(active_flags),
        "checks": checks,
        "passed": all(checks.values()),
    }
    (ROOT / "adobe_contract_gate.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    for name, passed in checks.items():
        print(f"{'PASS' if passed else 'FAIL'} {name}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
