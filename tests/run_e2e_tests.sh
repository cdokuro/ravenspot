#!/usr/bin/env bash
# ==============================================================================
# RavenSpot Native C++ Rewrite — Opaque-Box E2E Test Suite Runner
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Ensure python3 is available
if ! command -v python3 &>/dev/null; then
    echo "ERROR: python3 is required to run the E2E test suite." >&2
    exit 1
fi

# Forward all arguments directly to Python test orchestrator
export PYTHONPATH="${PROJECT_ROOT}:${PYTHONPATH:-}"
exec python3 "${SCRIPT_DIR}/e2e_runner.py" "$@"
