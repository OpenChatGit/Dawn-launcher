#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
sed -i 's/\r$//' "$ROOT"/scripts/*.sh "$ROOT"/scripts/*.py || true
chmod +x "$ROOT"/scripts/build-wsl.sh "$ROOT"/scripts/wsl-env.sh "$ROOT"/scripts/wsl-check.sh
python3 "$ROOT/scripts/wsl-install-bashrc.py"
echo "WSL scripts ready"
