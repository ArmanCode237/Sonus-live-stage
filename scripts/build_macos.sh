#!/usr/bin/env bash
# macOS 11+, Apple Silicon. Builds a standalone .app and JUCE VST3, then a .pkg.
# Usage: ./scripts/build_macos.sh [--skip-ui] [--skip-pkg]
# Optional for public distribution:
#   STAGE_DEVELOPER_ID_APP="Developer ID Application: ..." \
#   STAGE_DEVELOPER_ID_INSTALLER="Developer ID Installer: ..." \
#   STAGE_NOTARY_PROFILE="live-stage-notary" ./scripts/build_macos.sh
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${STAGE_MAC_BUILD_DIR:-$ROOT/build/macos-arm64}"
OUT="${STAGE_MAC_OUT_DIR:-$ROOT/dist/macos-arm64}"
ARCH="${STAGE_MAC_ARCH:-arm64}"
MINIMUM="${STAGE_MAC_MIN_VERSION:-11.0}"
SKIP_UI=0; SKIP_PKG=0
for arg in "$@"; do
  case "$arg" in
    --skip-ui) SKIP_UI=1 ;;
    --skip-pkg) SKIP_PKG=1 ;;
    *) echo "Uso: $0 [--skip-ui] [--skip-pkg]" >&2; exit 2 ;;
  esac
done
if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "ERROR: .app/.vst3/.pkg arm64 requieren Xcode en macOS (M1 o runner macOS arm64)." >&2
  exit 1
fi
for cmd in cmake ninja git xcrun pkgbuild ditto codesign; do
  command -v "$cmd" >/dev/null || { echo "Falta herramienta: $cmd" >&2; exit 1; }
done
[[ -d "$ROOT/ui/app" ]] || { echo "Falta ui/app" >&2; exit 1; }
if (( ! SKIP_UI )); then
  command -v npm >/dev/null || { echo "Instala Node.js / npm, o usa --skip-ui" >&2; exit 1; }
  pushd "$ROOT/webui" >/dev/null
  if [[ ! -d node_modules ]]; then npm ci; fi
  npm run build
  popd >/dev/null
fi
mkdir -p "$OUT"
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$MINIMUM" \
  -DSTAGE_BUILD_PLUGIN=ON -DSTAGE_NATIVE_WINDOW=ON
cmake --build "$BUILD" --parallel "$(sysctl -n hw.ncpu)"

STAGING="$OUT/payload"
rm -rf "$STAGING"
mkdir -p "$STAGING"
cmake --install "$BUILD" --component app --prefix "$STAGING"
APP="$STAGING/Applications/Sonus Live Stage.app"
[[ -f "$APP/Contents/MacOS/Sonus Live Stage" ]] || {
  echo "Falta binario de .app: $APP" >&2; exit 1;
}
[[ -f "$APP/Contents/Resources/ui/app/index.html" ]] || {
  echo "Faltan recursos React dentro de .app" >&2; exit 1;
}

# JUCE may place artefacts under <build>/LiveStagePlugin_artefacts/ or
# <build>/.../Release/. Restrict search to this build tree.
VST3="$(find "$BUILD" -type d -name 'Sonus Live Stage.vst3' -print -quit)"
[[ -n "$VST3" ]] || { echo "Falta VST3 compilado en $BUILD" >&2; exit 1; }
VST3_DEST="$STAGING/Library/Audio/Plug-Ins/VST3/Sonus Live Stage.vst3"
mkdir -p "$(dirname "$VST3_DEST")"
ditto "$VST3" "$VST3_DEST"

# The plug-in is a dynamically loaded binary: all assets must live INSIDE it.
package_resources() {
  local dest="$1/Contents/Resources"
  mkdir -p "$dest/ui" "$dest/config" "$dest/docs"
  ditto "$ROOT/ui/app" "$dest/ui/app"
  ditto "$ROOT/presets" "$dest/presets"
  cp "$ROOT/config/links.json" "$dest/config/links.json"
  cp "$ROOT/programs.json" "$ROOT/hardware_config.json" "$dest/"
  cp "$ROOT/LICENSE" "$ROOT/docs/GUIA-DE-USO.md" "$ROOT/docs/THIRD_PARTY_MACOS.md" "$dest/docs/"
  if [[ -f "$ROOT/NOTICE.md" ]]; then cp "$ROOT/NOTICE.md" "$dest/docs/"; fi
}
package_resources "$VST3_DEST"

# Optional Audio Unit from the current JUCE build (VST3 is always required).
AU="$(find "$BUILD" -type d -name 'Sonus Live Stage.component' -print -quit)"
AU_DEST=''
if [[ -n "$AU" ]]; then
  AU_DEST="$STAGING/Library/Audio/Plug-Ins/Components/Sonus Live Stage.component"
  mkdir -p "$(dirname "$AU_DEST")"
  ditto "$AU" "$AU_DEST"
  package_resources "$AU_DEST"
fi

if [[ -n "${STAGE_DEVELOPER_ID_APP:-}" ]]; then
  echo "Firmando .app y VST3 con Developer ID Application ..."
  for package in "$APP" "$VST3_DEST"; do
    codesign --force --sign "$STAGE_DEVELOPER_ID_APP" --timestamp --options runtime "$package"
    codesign --verify --deep --strict --verbose=2 "$package"
  done
  if [[ -n "$AU_DEST" ]]; then
    codesign --force --sign "$STAGE_DEVELOPER_ID_APP" --timestamp --options runtime "$AU_DEST"
    codesign --verify --deep --strict --verbose=2 "$AU_DEST"
  fi
elif [[ -n "${STAGE_DEVELOPER_ID_INSTALLER:-}" || -n "${STAGE_NOTARY_PROFILE:-}" ]]; then
  echo "Para firmar o notarizar se requiere STAGE_DEVELOPER_ID_APP" >&2
  exit 1
else
  echo "AVISO: distribución local sin Developer ID; Gatekeeper podría bloquearla en otros Mac."
fi

VERSION="$(sed -nE 's/^project\(SonusLiveStage VERSION ([0-9.]+).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
VERSION="${VERSION:-1.0.0}"
PKG="$OUT/SonusLiveStage-${VERSION}-macOS-${ARCH}.pkg"
if (( ! SKIP_PKG )); then
  args=(--root "$STAGING" --identifier com.sonus.livestage.installer --version "$VERSION" --install-location /)
  if [[ -n "${STAGE_DEVELOPER_ID_INSTALLER:-}" ]]; then
    args+=(--sign "$STAGE_DEVELOPER_ID_INSTALLER")
  fi
  pkgbuild "${args[@]}" "$PKG"
  if [[ -n "${STAGE_NOTARY_PROFILE:-}" ]]; then
    [[ -n "${STAGE_DEVELOPER_ID_APP:-}" && -n "${STAGE_DEVELOPER_ID_INSTALLER:-}" ]] || {
      echo "La notarización requiere ambas identidades Developer ID." >&2; exit 1;
    }
    xcrun notarytool submit "$PKG" --keychain-profile "$STAGE_NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
    pkgutil --check-signature "$PKG"
  fi
fi

echo ""
echo "App:       $APP"
echo "VST3:      $VST3_DEST"
[[ -n "$AU_DEST" ]] && echo "AU:        $AU_DEST"
if (( ! SKIP_PKG )); then echo "Installer: $PKG"; fi
echo "VALIDACIÓN PENDIENTE: abrir .app en M1, probar MIDI/Audio y validar VST3 en el DAW."
