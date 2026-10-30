#!/usr/bin/env bash
# Assemble KisakCOD.app with its runtime (docs/decisions/0011).
#
# Every non-system dependency of the client is copied into Contents/Frameworks,
# its install names are rewritten to @rpath, the bundled Vulkan loader is
# pointed at the bundled KosmicKrisp ICD, and everything is ad-hoc signed.
# The result is checked: any remaining absolute path outside the app is an
# error, so a stray Homebrew reference cannot ship.
#
# Usage:
#   bundle.sh --app <out.app> --client <KisakCOD-mp> --runtime-prefix <dir>
#             --mesa-prefix <dir> --dxvk-lib <libdxvk_d3d9.0.dylib>
#             [--licenses <dir>] [--dxvk-license <file>] [--version <short version>]
#             [--dsym <out.dSYM>]
#
# --licenses defaults to <runtime-prefix>/share/licenses (one directory per
# component, from the runtime project); --dxvk-license is dxvk's LICENSE.
# --dsym gathers the client's DWARF into <out.dSYM> and strips the bundled copy,
# so the dSYM matches the binary that ships whenever the client was relinked.
set -euo pipefail

APP="" CLIENT="" RUNTIME="" MESA="" DXVK="" LICENSES="" DXVK_LICENSE="" VERSION="0.1.0" DSYM=""
while [ $# -gt 0 ]; do
    case "$1" in
        --app) APP="$2"; shift 2 ;;
        --client) CLIENT="$2"; shift 2 ;;
        --runtime-prefix) RUNTIME="$2"; shift 2 ;;
        --mesa-prefix) MESA="$2"; shift 2 ;;
        --dxvk-lib) DXVK="$2"; shift 2 ;;
        --licenses) LICENSES="$2"; shift 2 ;;
        --dxvk-license) DXVK_LICENSE="$2"; shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        --dsym) DSYM="$2"; shift 2 ;;
        *) echo "bundle: unknown argument $1" >&2; exit 2 ;;
    esac
done
for v in APP CLIENT RUNTIME MESA DXVK; do
    [ -n "${!v}" ] || { echo "bundle: --$(echo "$v" | tr 'A-Z_' 'a-z-') is required" >&2; exit 2; }
done
for f in "$CLIENT" "$DXVK" "$RUNTIME/lib/libvulkan.1.dylib" "$MESA/lib/libvulkan_kosmickrisp.dylib"; do
    [ -f "$f" ] || { echo "bundle: missing $f" >&2; exit 1; }
done
command -v install_name_tool >/dev/null || { echo "bundle: install_name_tool not found" >&2; exit 1; }
command -v codesign >/dev/null || { echo "bundle: codesign not found" >&2; exit 1; }

# Provenance banner. The runtime prefix is built by hand from the pinned
# tarballs. These probes report the bundling host's tools; they do not identify
# the toolchain that built a separately supplied runtime prefix. These are
# declared prerequisites (README), not pinned inputs; the bundle sha256 is the
# artifact of record.
tool_version() {
    local out=""
    if command -v "$1" >/dev/null 2>&1; then
        out="$("$@" 2>/dev/null | head -1)" || out=""
    fi
    printf '%s' "${out:-not found}"
}
echo "bundle: build-host toolchain"
# llvm-config lives in a keg-only Homebrew prefix, so PATH usually misses it.
LLVM_CONFIG="${LLVM_CONFIG:-$(command -v llvm-config || true)}"
if [ -z "$LLVM_CONFIG" ]; then
    for candidate in /opt/homebrew/opt/llvm/bin/llvm-config \
                     /usr/local/opt/llvm/bin/llvm-config; do
        if [ -x "$candidate" ]; then LLVM_CONFIG="$candidate"; break; fi
    done
fi
if [ -n "$LLVM_CONFIG" ]; then
    echo "  llvm:    $("$LLVM_CONFIG" --version 2>/dev/null || echo 'not found')"
else
    echo "  llvm:    not found (llvm-config)"
fi
echo "  glslang: $(tool_version glslangValidator --version)"
echo "  bison:   $(tool_version bison --version)"
echo "  python3: $(tool_version python3 --version)"
echo "  meson:   $(tool_version meson --version)"
echo "  ninja:   $(tool_version ninja --version)"

# Assemble into a fresh staging directory: an earlier run's frameworks must
# never survive into a new bundle (an old copy would hide a pin bump).
APP_DEST="$APP"
mkdir -p "$(dirname "$APP_DEST")"
STAGE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/kisakcod-bundle.XXXXXX")"
APP="$STAGE_ROOT/KisakCOD.app"
CONTENTS="$APP/Contents"
FRAMEWORKS="$CONTENTS/Frameworks"
MACOS="$CONTENTS/MacOS"
RESOURCES="$CONTENTS/Resources"
ICD_DIR="$RESOURCES/vulkan/icd.d"
mkdir -p "$MACOS" "$FRAMEWORKS" "$ICD_DIR" "$RESOURCES/licenses"
cp -f "$CLIENT" "$MACOS/KisakCOD-mp"
chmod +x "$MACOS/KisakCOD-mp"
if [ -n "$DSYM" ]; then
    # The UUID survives the install-name rewrites and the signing below.
    mkdir -p "$(dirname "$DSYM")"
    dsymutil "$CLIENT" -o "$DSYM"
    strip -S -x "$MACOS/KisakCOD-mp"
fi

is_system() {
    case "$1" in
        /usr/lib/*|/System/*) return 0 ;;
        *) return 1 ;;
    esac
}

# $1: Mach-O file. Prints its non-system dependency paths.
deps_of() {
    otool -L "$1" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        [ -n "$dep" ] || continue
        is_system "$dep" && continue
        echo "$dep"
    done
}

# Copy a library (following symlinks) under the name dyld will look for.
copy_lib() {
    local src="$1" name dst
    name="$(basename "$src")"
    dst="$FRAMEWORKS/$name"
    [ -f "$dst" ] && return 0
    cp -Lf "$src" "$dst"
    chmod u+w "$dst"
}

# Map a dyld reference to the file that implements it. @rpath names are
# resolved against the prefixes the build linked; anything else is an error.
resolve_dep() {
    local dep="$1" name
    case "$dep" in
        @rpath/*|@loader_path/*) name="${dep##*/}" ;;
        *) [ -f "$dep" ] && { echo "$dep"; return 0; }; return 1 ;;
    esac
    [ -n "$name" ] || return 1
    local d
    for d in "$FRAMEWORKS" "$RUNTIME/lib" "$MESA/lib" "$(dirname "$DXVK")"; do
        if [ -f "$d/$name" ]; then echo "$d/$name"; return 0; fi
    done
    return 1
}

# Close over the client's dependencies, then add the two libraries dyld never
# sees as links: the Vulkan loader (dxvk dlopens it) and the ICD (the loader
# opens it from the manifest).
QUEUE=()
seen=" "
add_src() {
    local src="$1" name
    if [ "$src" = "$CLIENT" ]; then
        case "$seen" in *" KisakCOD-mp "*) return 0 ;; esac
        seen="$seen KisakCOD-mp "
        QUEUE+=("$MACOS/KisakCOD-mp")
        return 0
    fi
    [ -f "$src" ] || { echo "bundle: cannot resolve $src" >&2; exit 1; }
    name="$(basename "$src")"
    case "$seen" in *" $name "*) return 0 ;; esac
    seen="$seen$name "
    copy_lib "$src"
    QUEUE+=("$FRAMEWORKS/$name")
}
add_src "$CLIENT"        # the client itself is rewritten in place, not copied again
add_src "$DXVK"
add_src "$RUNTIME/lib/libvulkan.1.dylib"
add_src "$MESA/lib/libvulkan_kosmickrisp.dylib"

# The client's own libraries: resolve each dependency against the prefixes.
i=0
while [ $i -lt ${#QUEUE[@]} ]; do
    lib="${QUEUE[$i]}"
    i=$((i + 1))
    orig="$lib"
    while read -r dep; do
        [ -n "$dep" ] || continue
        name="${dep##*/}"
        [ -f "$FRAMEWORKS/$name" ] && continue
        if ! src="$(resolve_dep "$dep")"; then
            echo "bundle: unresolved $dep in $orig" >&2
            exit 1
        fi
        add_src "$src"
    done < <(deps_of "$orig")
done

# Rewrite install names: ids to @rpath, references to @rpath/<name>.
rewrite_refs() {
    local file="$1" dep name rpath
    # Drop build-host run paths (CMake's build RPATH to the runtime prefix):
    # the bundle resolves only inside itself.
    while read -r rpath; do
        case "$rpath" in
            /*) install_name_tool -delete_rpath "$rpath" "$file" ;;
        esac
    done < <(python3 "$(dirname "$0")/native_closure.py" rpaths "$file")
    install_name_tool -add_rpath "@loader_path" "$file" 2>/dev/null || true
    while read -r dep; do
        [ -n "$dep" ] || continue
        case "$dep" in
            @rpath/*) continue ;;
        esac
        name="$(basename "$dep")"
        if [ -f "$FRAMEWORKS/$name" ]; then
            install_name_tool -change "$dep" "@rpath/$name" "$file"
        fi
    done < <(deps_of "$file")
}

for lib in "$FRAMEWORKS"/*.dylib; do
    install_name_tool -id "@rpath/$(basename "$lib")" "$lib"
done
for lib in "$FRAMEWORKS"/*.dylib; do
    rewrite_refs "$lib"
done

install_name_tool -add_rpath "@executable_path/../Frameworks" "$MACOS/KisakCOD-mp" 2>/dev/null || true
rewrite_refs "$MACOS/KisakCOD-mp"

# The bundled loader finds the bundled ICD relative to the manifest.
cat > "$ICD_DIR/kosmickrisp_mesa_icd.json" <<JSON
{
    "ICD": {
        "api_version": "1.4.354",
        "library_path": "../../../Frameworks/libvulkan_kosmickrisp.dylib"
    },
    "file_format_version": "1.0.1"
}
JSON

cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>KisakCOD-mp</string>
    <key>CFBundleIconFile</key><string>KisakCOD</string>
    <key>CFBundleIdentifier</key><string>org.kisakcod.client</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>KisakCOD</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$VERSION</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>LSMinimumSystemVersion</key><string>26.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
mkdir -p "$MACOS" && printf 'APPL????' > "$CONTENTS/PkgInfo"
# The icon on Apple's grid (scripts/packaging/icons/make_icons.py).
cp -f "$(dirname "$0")/../../packaging/icons/kisakcod.icns" "$RESOURCES/KisakCOD.icns" || {
    echo "bundle: FAILED copying the icon" >&2; exit 1; }

LICENSES="${LICENSES:-$RUNTIME/share/licenses}"
if [ -d "$LICENSES" ]; then
    cp -R "$LICENSES"/. "$RESOURCES/licenses/"
fi
if [ -n "$DXVK_LICENSE" ]; then
    mkdir -p "$RESOURCES/licenses/dxvk"
    cp -f "$DXVK_LICENSE" "$RESOURCES/licenses/dxvk/LICENSE"
fi
cat > "$RESOURCES/licenses/SOURCES.txt" <<SOURCES
KisakCOD.app bundles its runtime, built from source at these pins
(docs/decisions/0011-macos-client-bundles-its-runtime.md):

  Mesa (KosmicKrisp Vulkan driver)   26.2.4
  Vulkan Loader                      1.4.363.0
  SPIRV-Tools                        1.4.363.0   (linked into the driver)
  zstd                               1.5.7
  SDL3                               3.4.18
  OpenAL Soft                        1.25.2      (LGPL-2.1)
  FFmpeg                             9.0.2       (LGPL-2.1; bink demuxer,
                                                   bink/binkaudio/adpcm_ima_wav
                                                   decoders only)
  dxvk (jm2/dxvk kisakcod-macos)     dac6ce62dada1f9fd2a3c347cd66580deecd6bca

Device access is through Mesa's Metal driver; no GPU driver is bundled.
SOURCES

# Every framework must ship its component's license text.
bash "$(dirname "$0")/check_licenses.sh" "$APP" || {
    echo "bundle: FAILED license check" >&2; exit 1; }
# One file with the engine's third-party code and every bundled component.
python3 "$(dirname "$0")/../../packaging/third_party_notices.py" --client \
    --licenses "$RESOURCES/licenses" --out "$RESOURCES/THIRD-PARTY-NOTICES" || {
    echo "bundle: FAILED third-party notices" >&2; exit 1; }

# Ad-hoc sign inside out; the app's signature covers the Frameworks.
for lib in "$FRAMEWORKS"/*.dylib; do
    codesign --force --sign - "$lib" >/dev/null 2>&1
done
codesign --force --sign - "$MACOS/KisakCOD-mp" >/dev/null 2>&1
codesign --force --sign - "$APP" >/dev/null 2>&1

# Verification: every Mach-O in the app resolves only to bundled files or the
# OS, through its own run-path stack, and none needs a newer macOS than
# Info.plist declares (native_closure.py reads the load commands directly).
python3 "$(dirname "$0")/native_closure.py" bundle "$APP" || {
    echo "bundle: FAILED verification" >&2; exit 1; }
codesign --verify --deep --strict "$APP" >/dev/null 2>&1 || {
    echo "bundle: signature verification failed" >&2; exit 1; }
if [ -e "$APP_DEST" ]; then
    mv "$APP_DEST" "$APP_DEST.replaced-$(date +%Y%m%d%H%M%S)"
fi
mv "$APP" "$APP_DEST"
rmdir "$STAGE_ROOT"
echo "bundle: $APP_DEST ok ($(ls "$APP_DEST/Contents/Frameworks" | wc -l | tr -d ' ') frameworks)"
