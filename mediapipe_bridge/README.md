# MediaPipe bridge

This directory is copied into the pinned MediaPipe v0.10.35 workspace by
`scripts/prepare_mediapipe.sh`. Bazel builds one CPU-only dynamic library with
a stable C ABI. The Adobe bundle links this bridge without exposing MediaPipe
or Abseil types across library boundaries.

The bridge exports Face Landmarker (478 points) and Hair Segmenter inference.
Its model files remain in the Adobe effect bundle's Resources directory.

The pinned source is MediaPipe 0.10.35 commit
`f8ef212d5c962c0e853db7e59d217056b187084b`. The build publishes the bridge and
the exact eight-library OpenCV allowlist as one staged `dist/native` directory;
arm64, ad-hoc signature, local dependency closure, `@loader_path` rpath, macOS
minimum version, and SHA-256 are checked before and after the recoverable swap.
