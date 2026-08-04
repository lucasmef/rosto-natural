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
    version_header = read("adobe/RostoNaturalVersion.h")
    pipl = read("adobe/RostoNaturalPiPL.r")
    adobe_cmake = read("adobe/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")
    panel = read("uxp/js/premiere.js")
    analysis = read("uxp/js/analysis.js")
    panel_main = read("uxp/js/main.js")
    preflight = read("scripts/preflight.sh")
    build_adobe = read("scripts/build_adobe.sh")
    build_metal = read("scripts/build_metal.sh")
    package_uxp = read("scripts/package_uxp.sh")
    package_ccx = read("scripts/package_uxp_ccx.sh")
    package_release = read("scripts/package_release.sh")
    prepare_mediapipe = read("scripts/prepare_mediapipe.sh")
    publish_lock = read("scripts/publish_lock.sh")
    installer = read("scripts/install_when_ready.sh")
    uninstaller = read("scripts/uninstall_when_ready.sh")
    payload_validator = read("scripts/validate_native_payload.sh")
    manifest = json.loads(read("uxp/manifest.json"))
    render_body = effect.split("PF_Err render", 1)[-1]

    version_parts = {}
    for key in ("MAJOR", "MINOR", "BUGFIX"):
        match = re.search(
            rf"#define\s+ROSTO_VERSION_{key}\s+(\d+)", version_header
        )
        if match:
            version_parts[key] = match.group(1)
    native_version = ".".join(
        version_parts.get(key, "") for key in ("MAJOR", "MINOR", "BUGFIX")
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
        "_Capture Token",
        "_Capture Session",
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
            and "out->my_version = ROSTO_EFFECT_VERSION" in effect
            and "AE_Effect_Version { ROSTO_EFFECT_VERSION }" in pipl
            and "ROSTO_VERSION_STAGE PF_Stage_RELEASE" in version_header
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
            and "out->out_flags = PF_OutFlag_NON_PARAM_VARY" in effect
            and "AE_Effect_Global_OutFlags { PF_OutFlag_NON_PARAM_VARY }"
            in pipl
        ),
        "pipl_is_compiled_into_bundle": (
            '#include "AE_General.r"' in pipl
            and '#include "AE_Effect.h"' not in pipl
            and '#include "AE_GeneralPlug.h"' not in pipl
            and "#ifdef Rez" in version_header
            and "RostoNaturalPiPL.rsrc" in adobe_cmake
            and "ROSTO_REZ_EXECUTABLE" in adobe_cmake
        ),
        "sdk_headers_and_entrypoints_are_enforced": (
            '#include "entry.h"' in effect
            and "PF_Err PluginDataEntryFunction2(" in effect
            and "result = PF_REGISTER_EFFECT_EXT2(" in effect
            and 'AE_Effect_Support_URL {' in pipl
            and '"${AE_SDK_ROOT}/Examples/Util"' in adobe_cmake
            and "LINKER:-exported_symbol,_EffectMain" in adobe_cmake
            and "LINKER:-exported_symbol,_PluginDataEntryFunction2"
            in adobe_cmake
            and "CXX_VISIBILITY_PRESET hidden" in adobe_cmake
            and "Examples/Util/Param_Utils.h" in build_adobe
            and "Examples/Util/entry.h" in build_adobe
            and "Examples/Resources/AE_General.r" in build_adobe
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
        "capture_protocol_is_keyframed_correlated_and_atomic": (
            "armCaptureTokens" in panel
            and "createSetTimeVaryingAction(true)" in panel
            and "InterpolationMode.HOLD" in panel
            and "createAddKeyframeAction" in panel
            and "validateCaptureResponses" in analysis
            and "keyframedHoldSchedule: true" in analysis
            and "PF_InData.current_time/time_scale" in analysis
            and "expiringLeaseSeconds: 10" in analysis
            and "expectedHostSeconds" in analysis
            and "scavengeExpiredCaptureSessions" in analysis
            and "startCaptureLeaseHeartbeat" in analysis
            and "capture_lease_is_active" in effect
            and '"capture.lease"' in effect
            and "capture_file_serial" in effect
            and "highQuality" in effect
            and "std::filesystem::rename(pending, published, error)" in effect
        ),
        "capture_is_a_render_independent_side_channel": (
            "analysis_id > 0" in render_body
            and "cache_version == rosto::kMeshCacheVersion" in render_body
            and "if (capture_mode || needs_render_detection)" in render_body
            and render_body.find("if (capture_mode)")
            < render_body.find("analyze_hair(")
            and "return PF_COPY" in render_body
            and "in_data->field" not in render_body
        ),
        "artifact_publication_is_scoped_locked_and_recoverable": (
            ".uxp-package.lock" in package_uxp
            and ".ccx-package.lock" in package_ccx
            and ".release.lock" in package_release
            and ".native-build.lock" in build_adobe
            and ".metal-build.lock" in build_metal
            and "ROSTO_PACKAGE_OUTPUT_DIR fora do stage de release"
            in package_uxp
            and "ROSTO_PACKAGE_OUTPUT_DIR fora do stage de release"
            in package_ccx
            and "rollback_failed" in package_release
            and "Bundle anterior pertence a outro produto" in build_adobe
            and ".publish.lock" in publish_lock
            and "rosto_publish_lock_preserve" in publish_lock
            and "ROSTO_PUBLISH_LOCK_TOKEN" in package_release
            and "OLD_MOVED" in prepare_mediapipe
            and "NEW_PUBLISHED" in prepare_mediapipe
            and "MEDIAPIPE_ITEMS" in prepare_mediapipe
        ),
        "installer_requires_confirmation_and_closed_host": (
            '--confirm' in installer
            and '--replace' in installer
            and 'require_host_closed "Premiere Pro"' in installer
            and 'require_host_closed "Adobe Media Encoder"' in installer
            and 'require_host_closed "After Effects"' in installer
            and "/usr/bin/pgrep -f" in installer
            and "Instalação recusada por segurança" in installer
            and installer.count("validate_native_bundle.sh") >= 2
            and "validate_native_payload.sh" in installer
            and "--norsrc --noextattr --noqtn --noacl" in installer
            and "validate_product_identity" in installer
            and "rosto_publish_lock_acquire" in installer
            and "PUBLICATION_PENDING=true" in installer
            and "OLD_MOVED" in installer
            and "NEW_PUBLISHED" in installer
            and ".rosto-install-stage." in installer
        ),
        "uninstaller_is_confirmed_closed_host_and_recoverable": (
            '--confirm' in uninstaller
            and 'require_host_closed "Premiere Pro"' in uninstaller
            and 'require_host_closed "Adobe Media Encoder"' in uninstaller
            and 'require_host_closed "After Effects"' in uninstaller
            and 'BACKUP_ROOT="$DIST_DIR/uninstall-backups"' in uninstaller
            and '/bin/mv "$TARGET" "$BACKUP"' in uninstaller
            and "validate_product_identity" in uninstaller
            and "rosto_publish_lock_acquire" in uninstaller
            and "PUBLICATION_PENDING=true" in uninstaller
            and "OLD_MOVED" in uninstaller
        ),
        "portable_native_payload_is_byte_checked_and_strictly_verified": (
            "rosto-native-payload-verify" in payload_validator
            and "validate_native_bundle.sh" in payload_validator
            and "/usr/bin/cmp -s" in payload_validator
            and "--norsrc --noextattr --noqtn --noacl" in payload_validator
            and "validate_native_payload.sh" in build_adobe
            and package_release.count("validate_native_payload.sh") >= 2
        ),
        "complete_metal_toolchain_is_required_and_built_in_release": (
            "/usr/bin/xcrun --find metal" in preflight
            and "/usr/bin/xcrun --find metallib" in preflight
            and re.search(
                r'ROSTO_PUBLISH_LOCK_TOKEN="\$ROSTO_PUBLISH_LOCK_TOKEN"\s*'
                r'\\\n\s*"\$PROJECT_DIR/scripts/build_metal\.sh"',
                package_release,
            )
            and "rosto_publish_lock_acquire" in build_metal
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
