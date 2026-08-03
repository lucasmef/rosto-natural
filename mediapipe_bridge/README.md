# MediaPipe bridge

This directory is copied into the pinned MediaPipe v0.10.35 workspace by
`scripts/prepare_mediapipe.sh`. Bazel builds one CPU-only dynamic library with
a stable C ABI. The Adobe bundle links this bridge without exposing MediaPipe
or Abseil types across library boundaries.

The bridge exports Face Landmarker (478 points) and Hair Segmenter inference.
Its model files remain in the Adobe effect bundle's Resources directory.
