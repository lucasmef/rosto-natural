#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REFERENCE_DIR=${1:-"/Users/lucas/Movies/Vídeos/Edit"}

cd "$PROJECT_DIR"
./scripts/build_core.sh

PYTHONPATH=.python-legacy PYTHONPYCACHEPREFIX=/private/tmp/rosto-pycache \
  python3 tools/evaluate_ours_semantic.py "$REFERENCE_DIR" \
  --model models/lbfmodel.yaml --cli build-manual/rosto_cli \
  --output facetune_semantic_gate.json

PYTHONPATH=.python-legacy PYTHONPYCACHEPREFIX=/private/tmp/rosto-pycache \
  python3 tools/evaluate_contour.py "$REFERENCE_DIR" \
  --model models/lbfmodel.yaml --cli build-manual/rosto_cli \
  --output facetune_contour_gate.json

PYTHONPATH=.python-legacy PYTHONPYCACHEPREFIX=/private/tmp/rosto-pycache \
  python3 tools/evaluate_cheek_texture.py "$REFERENCE_DIR" \
  --model models/lbfmodel.yaml --cli build-manual/rosto_cli \
  --output facetune_texture_gate.json

PYTHONPATH=.python-legacy PYTHONPYCACHEPREFIX=/private/tmp/rosto-pycache \
  python3 tools/evaluate_visual_similarity.py "$REFERENCE_DIR" \
  --model models/lbfmodel.yaml --cli build-manual/rosto_cli \
  --output facetune_similarity_gate.json

PYTHONPATH=.python-legacy PYTHONPYCACHEPREFIX=/private/tmp/rosto-pycache \
  python3 tools/facetune_gate.py \
  --semantic facetune_semantic_gate.json \
  --contour facetune_contour_gate.json \
  --texture facetune_texture_gate.json \
  --similarity facetune_similarity_gate.json \
  --output facetune_validation_gate.json
