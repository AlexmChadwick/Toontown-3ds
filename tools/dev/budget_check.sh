#!/usr/bin/env bash
# One-shot host budget gate: static footprint, per-zone residency, SD set size.
# Needs arm-none-eabi-size on PATH (see docs/PROGRESS.md round 11 for the toolchain exports).
set -u -o pipefail
cd "$(dirname "$0")/../.."
rc=0
tools/dev/static_size.sh || rc=1
python3 -I tools/pipeline/zone_resident.py | tail -3 || rc=1
python3 -I tools/pipeline/sd_budget.py | tail -3 || rc=1
[ $rc -eq 0 ] && echo "budget_check: OK" || echo "budget_check: FAIL" >&2
exit $rc
