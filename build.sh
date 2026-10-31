#!/usr/bin/env bash
# KisakCOD: configure and build on Linux (amd64, arm64) or macOS (arm64).
# The POSIX counterpart of build-win.ps1. It picks the CMakePresets.json preset
# for this host, so a manual `cmake --preset` build gives the same result.
set -euo pipefail

usage() {
    cat <<'EOF'
KisakCOD: Linux and macOS build helper.

Usage:
  ./build.sh [client] [server] [tests] [options] [-- client args...]

Components (default: client server):
  client            The experimental 64-bit MP client (KisakCOD-mp): SDL3 and
                    dxvk-native D3D9, FFmpeg cinematics, OpenAL sound.
  server            The headless dedicated server (KisakCOD-dedi).
  tests             The portable test suite, then run it with ctest.

Options:
  --config C        Debug, Release (default) or RelWithDebInfo.
  --clean           Delete the component's build directory first.
  --configure-only  Configure, then stop.
  --no-configure    Build an existing build directory without configuring.
  --jobs N          Parallel build jobs (default: logical CPUs).
  --install-deps    Install missing build dependencies first: Homebrew on
                    macOS; apt, dnf or pacman (through sudo) on Linux. On Linux
                    it also builds the pinned dxvk-native into .deps/.
  --dxvk-prefix D   Linux: use the dxvk-native install in D (default:
                    .deps/dxvk-native-<arch> when it exists).
  --run             After building, launch the client (or the server when
                    it is the only component) from bin/ with the runtime
                    environment set. Arguments after -- go to the game, e.g.
                    -- +set fs_basepath <copy of your install> +devmap mp_crash
  --dry-run         Print the commands without running them.
  -h, --help        Show this help.

Binaries land in bin/. Run the client against a copy-on-write clone of your
install (cp -cR on APFS, cp --reflink elsewhere), never the original; see
docs/design/CLIENT.md.
EOF
}

if [ -t 2 ]; then c_ok='\033[0;32m' c_warn='\033[1;33m' c_err='\033[0;31m' c_off='\033[0m'
else c_ok='' c_warn='' c_err='' c_off=''
fi
info() { printf "${c_ok}[kisakcod]${c_off} %s\n" "$*"; }
warn() { printf "${c_warn}[kisakcod]${c_off} warning: %s\n" "$*" >&2; }
fail() { printf "${c_err}[kisakcod]${c_off} %s\n" "$*" >&2; exit 1; }
# Flags hold true or false.
is() { [ "$1" = true ]; }
usage_error() { printf '[kisakcod] %s\n\n' "$*" >&2; usage >&2; exit 2; }

# Pinned dxvk-native for the Linux client (docs/design/CLIENT.md): the v3.1.1
# tag, built with only its SDL3 WSI. Bump deliberately.
readonly DXVK_NATIVE_REPO=https://github.com/doitsujin/dxvk.git
readonly DXVK_NATIVE_COMMIT=b1a1c99ab52b687cf950d62c88bc2fa316b41663

components=()
config=Release
clean=false
configure=true
build=true
jobs=
install_deps=false
dxvk_prefix=
run=false
dry_run=false
game_args=()

while [ $# -gt 0 ]; do
    case "$1" in
        client|server|tests) components+=("$1") ;;
        --config)
            [ $# -ge 2 ] || usage_error '--config needs a value.'
            config=$2; shift ;;
        --config=*) config=${1#*=} ;;
        --clean) clean=true ;;
        --configure-only) build=false ;;
        --no-configure) configure=false ;;
        --jobs)
            [ $# -ge 2 ] || usage_error '--jobs needs a value.'
            jobs=$2; shift ;;
        --jobs=*) jobs=${1#*=} ;;
        --install-deps) install_deps=true ;;
        --dxvk-prefix)
            [ $# -ge 2 ] || usage_error '--dxvk-prefix needs a directory.'
            dxvk_prefix=$2; shift ;;
        --dxvk-prefix=*) dxvk_prefix=${1#*=} ;;
        --run) run=true ;;
        --dry-run) dry_run=true ;;
        -h|--help) usage; exit 0 ;;
        --) shift; game_args=("$@"); break ;;
        *) usage_error "Unknown argument: $1" ;;
    esac
    shift
done

[ ${#components[@]} -gt 0 ] || components=(client server)
case "$config" in
    Debug|Release|RelWithDebInfo) ;;
    *) usage_error "--config must be Debug, Release or RelWithDebInfo, not '$config'." ;;
esac
if [ -n "$jobs" ] && ! [[ "$jobs" =~ ^[1-9][0-9]{0,3}$ ]]; then
    usage_error "--jobs must be a positive number, not '$jobs'."
fi
if ! is "$build" && ! is "$configure"; then
    usage_error '--configure-only and --no-configure cannot be used together.'
fi
if is "$clean" && ! is "$configure"; then
    usage_error '--clean deletes the build directory, so it needs a configure; drop --no-configure.'
fi
if is "$run" && ! is "$build"; then
    usage_error '--run needs a build; drop --configure-only.'
fi
if [ ${#game_args[@]} -gt 0 ] && ! is "$run"; then
    usage_error 'Arguments after -- are for the game; add --run.'
fi
has_component() {
    local c
    for c in "${components[@]}"; do [ "$c" = "$1" ] && return 0; done
    return 1
}

repo_root=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
# Paths given on the command line are relative to the caller, not the checkout.
case "$dxvk_prefix" in
    ''|/*) ;;
    *) dxvk_prefix="$PWD/$dxvk_prefix" ;;
esac
cd "$repo_root"
[ -f CMakeLists.txt ] && [ -f CMakePresets.json ] || fail "Run build.sh from a KisakCOD checkout ($repo_root)."

# KISAK_BUILD_SH_HOST overrides `uname -sm` so the routing can be tested anywhere.
read -r host_os host_arch <<<"${KISAK_BUILD_SH_HOST:-$(uname -sm)}"
case "$host_arch" in
    x86_64|amd64) arch=amd64 ;;
    aarch64|arm64) arch=arm64 ;;
    *) fail "Unsupported CPU '$host_arch'; the POSIX targets are amd64 and arm64." ;;
esac
case "$host_os" in
    Linux) platform=linux ;;
    Darwin)
        platform=macos
        [ "$arch" = arm64 ] || fail 'macOS builds target Apple silicon (arm64) only.' ;;
    *) fail "Unsupported OS '$host_os'. On Windows use build-win.ps1." ;;
esac

if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

# Print a command, then run it unless this is a dry run.
run_cmd() {
    printf '    %s\n' "$*"
    is "$dry_run" || "$@"
}

# ---- Dependencies -----------------------------------------------------------

# Linux package names per manager: tool, client, server/tests.
linux_pkg_manager() {
    if command -v apt-get >/dev/null 2>&1; then echo apt
    elif command -v dnf >/dev/null 2>&1; then echo dnf
    elif command -v pacman >/dev/null 2>&1; then echo pacman
    else echo unknown
    fi
}
linux_packages() {
    local manager=$1 want_client=$2
    case "$manager" in
        apt)
            printf '%s ' cmake make clang lld pkg-config git
            if is "$want_client"; then printf '%s ' meson ninja-build glslang-tools libsdl3-dev \
                libopenal-dev libavformat-dev libavcodec-dev libavutil-dev; fi ;;
        dnf)
            printf '%s ' cmake make clang lld pkgconf-pkg-config git
            if is "$want_client"; then printf '%s ' meson ninja-build glslang SDL3-devel \
                openal-soft-devel libavformat-free-devel libavcodec-free-devel libavutil-free-devel; fi ;;
        pacman)
            printf '%s ' cmake make clang lld pkgconf git
            if is "$want_client"; then printf '%s ' meson ninja glslang sdl3 openal ffmpeg; fi ;;
    esac
}
linux_install_hint() {
    local manager=$1 want_client=$2 pkgs
    pkgs=$(linux_packages "$manager" "$want_client")
    case "$manager" in
        apt) echo "sudo apt-get install $pkgs" ;;
        dnf) echo "sudo dnf install $pkgs" ;;
        pacman) echo "sudo pacman -S --needed $pkgs" ;;
        *) local extra=''
           if is "$want_client"; then extra=', meson, ninja, glslang, SDL3, OpenAL Soft and FFmpeg development files'; fi
           echo "install CMake, clang, pkg-config$extra" ;;
    esac
}

brew_formulas() {
    printf '%s ' cmake pkgconf
    if is "$1"; then printf '%s ' meson ninja ffmpeg openal-soft mesa; fi
}

install_dependencies() {
    local want_client=$1 manager pkgs
    if [ "$platform" = macos ]; then
        command -v brew >/dev/null 2>&1 || fail 'Homebrew is required for --install-deps: https://brew.sh'
        command -v clang >/dev/null 2>&1 || fail 'Install the Xcode Command Line Tools first: xcode-select --install'
        local missing=() formula
        for formula in $(brew_formulas "$want_client"); do
            brew list --formula "$formula" >/dev/null 2>&1 || missing+=("$formula")
        done
        if [ ${#missing[@]} -gt 0 ]; then
            info "Installing Homebrew formulas: ${missing[*]}"
            run_cmd brew install "${missing[@]}"
        else
            info 'Homebrew dependencies are already installed.'
        fi
        return
    fi
    manager=$(linux_pkg_manager)
    [ "$manager" != unknown ] || fail "No supported package manager (apt, dnf, pacman); $(linux_install_hint unknown "$want_client")."
    read -r -a pkgs <<<"$(linux_packages "$manager" "$want_client")"
    info "Installing ${manager} packages: ${pkgs[*]}"
    case "$manager" in
        apt) run_cmd sudo apt-get update; run_cmd sudo apt-get install -y "${pkgs[@]}" ;;
        dnf) run_cmd sudo dnf install -y "${pkgs[@]}" ;;
        pacman) run_cmd sudo pacman -S --needed --noconfirm "${pkgs[@]}" ;;
    esac
}

# Build the pinned dxvk-native into .deps/ (Linux client only).
build_dxvk_native() {
    local prefix=$1 src="$repo_root/.deps/src/dxvk-native" bld="$repo_root/.deps/build/dxvk-native-$arch"
    info "Building dxvk-native ${DXVK_NATIVE_COMMIT:0:12} into $prefix"
    if [ ! -d "$src/.git" ]; then
        run_cmd git clone --filter=blob:none "$DXVK_NATIVE_REPO" "$src"
    fi
    run_cmd git -C "$src" fetch --quiet origin "$DXVK_NATIVE_COMMIT"
    # The Linux runtime's Direct3DCreate9 patch (#970), on a clean checkout.
    run_cmd git -C "$src" checkout --quiet -- src/d3d9/d3d9_main.cpp
    run_cmd git -C "$src" checkout --quiet --detach "$DXVK_NATIVE_COMMIT"
    run_cmd git -C "$src" submodule update --init --recursive --quiet
    run_cmd git -C "$src" apply "$repo_root/scripts/platform/linux/runtime/dxvk-create-d3d9-catch.patch"
    run_cmd rm -rf "$bld"
    run_cmd meson setup "$bld" "$src" --buildtype=release --prefix="$prefix" --libdir=lib \
        -Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled
    run_cmd ninja -C "$bld" install
}

require() {
    command -v "$1" >/dev/null 2>&1 || fail "'$1' was not found. $2"
}

check_dependencies() {
    local want_client=$1 hint
    if [ "$platform" = macos ]; then
        hint="Run ./build.sh --install-deps, or: brew install $(brew_formulas "$want_client")"
        require clang 'Install the Xcode Command Line Tools: xcode-select --install'
    else
        hint="Run ./build.sh --install-deps, or: $(linux_install_hint "$(linux_pkg_manager)" "$want_client")"
        # The client and server presets pick clang; the tests take the default compiler.
        if is "$want_client" || has_component server; then require clang "$hint"; fi
    fi
    require cmake "$hint"
    local cmake_version
    cmake_version=$(cmake --version | sed -n '1s/[^0-9]*\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')
    if [ "${cmake_version%%.*}" -lt 3 ] || { [ "${cmake_version%%.*}" -eq 3 ] && [ "${cmake_version#*.}" -lt 25 ]; }; then
        fail "CMake $cmake_version is too old for CMakePresets.json (3.25 or newer). $hint"
    fi
    is "$want_client" || return 0

    require pkg-config "$hint"
    pkg-config --exists libavformat libavcodec libavutil || fail "FFmpeg development files were not found through pkg-config. $hint"
    if [ "$platform" = macos ]; then
        require meson "$hint"
        require ninja "$hint"
        # openal-soft is keg-only; CMake adds its pkg-config directory itself.
        local prefix
        prefix=$(brew --prefix openal-soft 2>/dev/null || true)
        [ -n "$prefix" ] && [ -f "$prefix/lib/pkgconfig/openal.pc" ] || fail "OpenAL Soft was not found. $hint"
        [ -f "$(brew --prefix mesa 2>/dev/null)/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json" ] \
            || warn 'Homebrew mesa (KosmicKrisp) is missing; the client builds but needs it to run: brew install mesa'
    else
        pkg-config --exists openal || fail "OpenAL Soft development files were not found through pkg-config. $hint"
        pkg-config --exists dxvk-d3d9 || fail "dxvk-native (dxvk-d3d9.pc) was not found. Run ./build.sh --install-deps to build the pinned version into .deps/, or pass --dxvk-prefix <install>."
    fi
}

# ---- Main -------------------------------------------------------------------

want_client=false
has_component client && want_client=true

if [ "$platform" = linux ] && is "$want_client"; then
    [ -n "$dxvk_prefix" ] || dxvk_prefix="$repo_root/.deps/dxvk-native-$arch"
fi

if is "$install_deps"; then
    install_dependencies "$want_client"
    if [ "$platform" = linux ] && is "$want_client" && [ ! -f "$dxvk_prefix/lib/pkgconfig/dxvk-d3d9.pc" ]; then
        build_dxvk_native "$dxvk_prefix"
    fi
fi

if [ -n "$dxvk_prefix" ]; then
    # dxvk installs into lib/ here, lib64/ or lib/<triplet>/ under some prefixes.
    for pc_dir in "$dxvk_prefix"/lib/pkgconfig "$dxvk_prefix"/lib64/pkgconfig "$dxvk_prefix"/lib/*/pkgconfig; do
        [ -d "$pc_dir" ] && PKG_CONFIG_PATH="$pc_dir${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    done
    export PKG_CONFIG_PATH
fi

is "$dry_run" || ! is "$configure" || check_dependencies "$want_client"

preset_for() {
    case "$1" in
        client) echo "$platform-$arch-client" ;;
        server) echo "$platform-$arch-dedi" ;;
        tests) echo "$platform-$arch-mp" ;;
    esac
}
target_for() {
    case "$1" in
        client) echo KisakCOD-mp ;;
        server) echo KisakCOD-dedi ;;
        tests) echo all ;;
    esac
}

for component in "${components[@]}"; do
    preset=$(preset_for "$component")
    build_dir="$repo_root/build-$preset"
    info "$component: preset $preset ($config)"
    if is "$clean" && [ -d "$build_dir" ]; then
        run_cmd rm -rf "$build_dir"
    fi
    if is "$configure"; then
        run_cmd cmake --preset "$preset" "-DCMAKE_BUILD_TYPE=$config"
    elif [ ! -f "$build_dir/CMakeCache.txt" ] && ! is "$dry_run"; then
        fail "--no-configure: $build_dir is not configured yet."
    fi
    is "$build" || continue
    run_cmd cmake --build "$build_dir" --target "$(target_for "$component")" --parallel "$jobs"
    if [ "$component" = tests ]; then
        run_cmd ctest --test-dir "$build_dir" --output-on-failure --parallel "$jobs"
    fi
done

if ! is "$build"; then
    info 'Configure complete.'
    exit 0
fi
info "Build complete: ${components[*]} [$config] in $repo_root/bin"

is "$run" || exit 0

if is "$want_client"; then
    exe="$repo_root/bin/KisakCOD-mp"
    if [ "$platform" = macos ]; then
        export VK_DRIVER_FILES="${VK_DRIVER_FILES:-$(brew --prefix mesa 2>/dev/null)/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json}"
        brew_prefix=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
        export DYLD_FALLBACK_LIBRARY_PATH="$repo_root/build-macos-arm64-client/dxvk-macos/lib:$brew_prefix/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"
    else
        for lib_dir in "$dxvk_prefix"/lib "$dxvk_prefix"/lib64 "$dxvk_prefix"/lib/*-linux-gnu; do
            [ -d "$lib_dir" ] && LD_LIBRARY_PATH="$lib_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        done
        export LD_LIBRARY_PATH
    fi
    export DXVK_WSI_DRIVER="${DXVK_WSI_DRIVER:-SDL3}"
elif has_component server; then
    exe="$repo_root/bin/KisakCOD-dedi"
else
    usage_error '--run needs the client or server component.'
fi
info "Launching $(basename "$exe")"
if is "$dry_run"; then
    printf '    %s\n' "$exe ${game_args[*]-}"
    exit 0
fi
cd "$repo_root/bin"
exec "$exe" ${game_args[@]+"${game_args[@]}"}
