#!/usr/bin/env bash
# Compatibility alias for build_macos.sh. Produces .app + VST3 + .pkg on macOS.
set -Eeuo pipefail
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build_macos.sh" "$@"
