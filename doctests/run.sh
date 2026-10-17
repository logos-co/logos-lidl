#!/usr/bin/env bash
#
# Execute the logos-lidl doc-test end-to-end and regenerate its Markdown.
#
# The spec (using-logos-lidl.test.yaml) scaffolds a tiny downstream `lidl-tour`
# project that depends on logos-lidl through its Nix flake + CMake package,
# builds three demo programs, and drives the library's four operations
# (tokenize/parse/validate/serialize) — asserting on their output.
#
# The runner is the shared `doctest` CLI
# (https://github.com/logos-co/logos-doctest), invoked directly via its flake.
# The spec runs into ./outputs/ via --output-dir; `doctest generate` renders the
# .md; `doctest clean` then strips build artifacts, keeping only the .md.
#
# To run against a local logos-doctest checkout instead of the published flake,
# set DOCTEST, e.g.:  DOCTEST="nix run path:../../logos-doctest --" ./run.sh
#
set -euo pipefail

# Run from this doctests/ directory regardless of where the script is invoked from.
cd "$(dirname "$0")"

# The doctest CLI. Override by exporting DOCTEST (space-separated command).
read -r -a DOCTEST <<< "${DOCTEST:-nix run github:logos-co/logos-doctest --}"
OUTPUT_DIR="./outputs"

# Build the doc-test against THIS repo's current commit rather than the latest
# published flake. The spec pins `github:logos-co/logos-lidl{release}` to
# $COMMIT via --release-for, so the consumer links exactly what is checked out
# here. Override by exporting COMMIT (e.g. a tag), or set COMMIT="" to fall back
# to latest master.
#
# Note: nix fetches the commit from the GitHub remote, so $COMMIT must be pushed
# to logos-co/logos-lidl. A local-only / uncommitted HEAD won't resolve; export
# COMMIT="" (or push first) in that case.
COMMIT="${COMMIT-$(git rev-parse HEAD)}"
RELEASE_FOR=()
if [ -n "${COMMIT}" ]; then
  RELEASE_FOR=(--release-for "logos-lidl=${COMMIT}")
  echo "==> Pinning logos-lidl to ${COMMIT}"
else
  echo "==> COMMIT empty; building from latest logos-lidl master"
fi

echo "==> Clearing previous ${OUTPUT_DIR}/"
# A prior run may copy artifacts out of the read-only nix store, landing them
# read-only too; restore write permission before removing.
if [ -e "${OUTPUT_DIR}" ]; then
  chmod -R u+w "${OUTPUT_DIR}" 2>/dev/null || true
fi
rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"

for spec in *.test.yaml; do
  name="$(basename "${spec%.test.yaml}")"
  echo "==> Running ${spec} into ${OUTPUT_DIR}/"
  # ${RELEASE_FOR[@]+...} guards the expansion so an empty array doesn't trip
  # `set -u` on older bash (e.g. macOS's stock 3.2).
  "${DOCTEST[@]}" run "${spec}" \
    --verbose \
    --continue-on-fail \
    ${RELEASE_FOR[@]+"${RELEASE_FOR[@]}"} \
    --output-dir "${OUTPUT_DIR}/"

  echo "==> Generating ${OUTPUT_DIR}/${name}.md"
  "${DOCTEST[@]}" generate "${spec}" \
    ${RELEASE_FOR[@]+"${RELEASE_FOR[@]}"} \
    -o "${OUTPUT_DIR}/${name}.md"
done

echo "==> Cleaning build artifacts from ${OUTPUT_DIR}/ (keeps .md)"
"${DOCTEST[@]}" clean "${OUTPUT_DIR}" --verbose

echo "==> Done. Rendered docs are in ${OUTPUT_DIR}/"
