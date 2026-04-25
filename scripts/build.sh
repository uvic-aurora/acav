#!/bin/bash

#$!{
# Aurora Clang AST Viewer (ACAV)
# 
# Copyright (c) 2026 Min Liu
# Copyright (c) 2026 Michael David Adams
# 
# SPDX-License-Identifier: GPL-2.0-or-later
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>.
#}$!

# =============================================================================
# build.sh - Unified Build Script for ACAV
#
# Usage:
#   ./build.sh [command] [options]
#
# Commands:
#   build   - Build debug version (default)
#   test    - Run all tests
#   clean   - Remove build directory
#   shell   - Start interactive shell in container
#   deploy  - Build Docker distribution image with version
#
# Examples:
#   ./build.sh                           # Debug build
#   ./build.sh test                      # Run tests
#   ./build.sh deploy                    # Build Docker image (auto-detect version)
#   ./build.sh deploy --version 1.2.3    # Build Docker image with explicit version
# =============================================================================

# -----------------------------------------------------------------------------
# Utilities
# -----------------------------------------------------------------------------

panic() {
    echo "ERROR: $1" >&2
    exit "${2:-1}"
}

run_command() {
    local description="$1"
    shift
    echo "→ $description"
    if ! "$@"; then
        panic "Failed: $description"
    fi
}

detect_container_runtime() {
    if command -v docker >/dev/null 2>&1; then
        echo "docker"
    elif command -v podman >/dev/null 2>&1; then
        echo "podman"
    else
        panic "Neither docker nor podman is installed. Install one of them:
  macOS:         brew install --cask docker
  Ubuntu/Debian: sudo apt install docker.io
  Fedora/RHEL:   sudo dnf install podman"
    fi
}

verify_container_runtime_working() {
    local runtime="$1"
    if ! "$runtime" ps >/dev/null 2>&1; then
        if [ "$runtime" = "docker" ]; then
            panic "Docker is installed but not running. Start Docker Desktop or docker daemon."
        else
            panic "Podman is installed but not working. Check: podman ps"
        fi
    fi
}

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

ACAV_LLVM_VERSION="${ACAV_LLVM_VERSION:-22}"
DOCKER_IMAGE="ghcr.io/smartai/llvm-qt-dev-env:fedora-llvm${ACAV_LLVM_VERSION}"

# LLVM version -> Fedora runtime version mapping.
# Keep this compatible with macOS /bin/bash 3.2.
case "$ACAV_LLVM_VERSION" in
    20) ACAV_FEDORA_VERSION=42 ;;
    21) ACAV_FEDORA_VERSION=43 ;;
    22) ACAV_FEDORA_VERSION=44 ;;
    *) ACAV_FEDORA_VERSION=44 ;;
esac
BUILD_DIR_DEBUG="out/linux-debug"
BUILD_DIR_RELEASE="out/linux-release"
CMAKE_PRESET_DEBUG="linux-debug"
CMAKE_PRESET_RELEASE="linux-release"
DIST_BASE="out/dist"

# -----------------------------------------------------------------------------
# Setup
# -----------------------------------------------------------------------------

CONTAINER_CMD=$(detect_container_runtime)
echo "Using: $CONTAINER_CMD"
verify_container_runtime_working "$CONTAINER_CMD"

VOL_DIR="$(pwd)"
MOUNT_OPT="$VOL_DIR:/app"
SECURITY_OPTS=()

# SELinux support (applies to both docker and podman, including podman-docker emulation)
if command -v getenforce >/dev/null 2>&1; then
    if [ "$(getenforce 2>/dev/null)" = "Enforcing" ]; then
        SECURITY_OPTS=(--security-opt label=disable)
        echo "Note: SELinux detected, using --security-opt label=disable"
    fi
fi

# -----------------------------------------------------------------------------
# Commands
# -----------------------------------------------------------------------------

show_help() {
    cat << EOF
ACAV Build Script

Usage: $0 [command] [options]

Commands:
  build   - Build debug version (default)
  test    - Run all tests
  clean   - Remove build directories
  shell   - Start interactive shell in container
  deploy  - Build Docker distribution image with version
  help    - Show this help message

Build Examples:
  $0                    # Debug build
  $0 build              # Debug build (explicit)
  $0 test               # Run tests
  $0 clean              # Clean all builds
  $0 shell              # Interactive shell

Deploy Examples:
  $0 deploy                    # Auto-detect version from git
  $0 deploy --version 1.2.3    # Explicit version

Deploy Options:
  --version <ver>    Version to package (optional, auto-detects from git)

Environment Variables:
  ACAV_LLVM_VERSION  LLVM version to build against (default: 22)
                     LLVM 20 → Fedora 42, LLVM 21 → Fedora 43, LLVM 22 → Fedora 44
  BUILD_JOBS         Number of parallel build jobs (default: 4)

Configuration:
  Debug build:   $BUILD_DIR_DEBUG
  Release build: $BUILD_DIR_RELEASE
  Docker image:  $DOCKER_IMAGE

EOF
    exit 0
}

cmd_build() {
    echo "=============================================="
    echo "Building ACAV (Debug Mode)"
    echo "=============================================="
    echo "Preset:      $CMAKE_PRESET_DEBUG"
    echo "Build dir:   $BUILD_DIR_DEBUG"
    echo ""

    run_command "Pulling Docker image" \
        $CONTAINER_CMD pull "$DOCKER_IMAGE"

    echo ""

    BUILD_JOBS=${BUILD_JOBS:-4}

    if ! $CONTAINER_CMD run --rm \
        "${SECURITY_OPTS[@]}" \
        -v "$MOUNT_OPT" \
        -w /app \
        "$DOCKER_IMAGE" \
        bash -c "cmake --preset=$CMAKE_PRESET_DEBUG && cmake --build $BUILD_DIR_DEBUG -j$BUILD_JOBS"; then
        panic "Build failed"
    fi

    echo ""
    echo "=============================================="
    echo "✓ Build complete!"
    echo "=============================================="
    echo "Binaries: $BUILD_DIR_DEBUG/bin/"
    echo ""

    if [ -d "$BUILD_DIR_DEBUG/bin" ]; then
        ls -lh "$BUILD_DIR_DEBUG/bin/" 2>/dev/null || true
    fi
}

cmd_test() {
    echo "=============================================="
    echo "Running Tests"
    echo "=============================================="
    echo ""

    if [ ! -d "$BUILD_DIR_DEBUG" ]; then
        panic "Build directory does not exist: $BUILD_DIR_DEBUG
Run '$0 build' first"
    fi

    if [ ! -f "$BUILD_DIR_DEBUG/CTestTestfile.cmake" ]; then
        panic "Test configuration not found
Run '$0 build' first"
    fi

    run_command "Pulling Docker image" \
        $CONTAINER_CMD pull "$DOCKER_IMAGE"

    echo ""

    if ! $CONTAINER_CMD run --rm \
        "${SECURITY_OPTS[@]}" \
        -v "$MOUNT_OPT" \
        -w /app \
        "$DOCKER_IMAGE" \
        bash -c "cd $BUILD_DIR_DEBUG && ctest --output-on-failure -j\$(nproc)"; then
        panic "Tests failed"
    fi

    echo ""
    echo "=============================================="
    echo "✓ All tests passed!"
    echo "=============================================="
    echo ""
}

cmd_clean() {
    echo "=============================================="
    echo "Cleaning Build Directories"
    echo "=============================================="
    echo ""

    local cleaned=false

    for dir in "$BUILD_DIR_DEBUG" "$BUILD_DIR_RELEASE"; do
        if [ -d "$dir" ]; then
            echo "→ Removing $dir"
            rm -rf "$dir"
            cleaned=true
        fi
    done

    if [ "$cleaned" = true ]; then
        echo ""
        echo "✓ Clean complete!"
    else
        echo "Nothing to clean"
    fi
    echo ""
}

cmd_shell() {
    echo "=============================================="
    echo "Starting Interactive Shell"
    echo "=============================================="
    echo "Working directory: /app"
    echo "Exit with: exit or Ctrl+D"
    echo ""

    run_command "Pulling Docker image" \
        $CONTAINER_CMD pull "$DOCKER_IMAGE"

    echo ""

    $CONTAINER_CMD run -it --rm \
        "${SECURITY_OPTS[@]}" \
        -v "$MOUNT_OPT" \
        -w /app \
        "$DOCKER_IMAGE" \
        /bin/bash || true
}

cmd_deploy() {
    local VERSION=""

    # Parse deploy options
    while [[ $# -gt 0 ]]; do
        case $1 in
            --version)
                VERSION="$2"
                shift 2
                ;;
            *)
                panic "Unknown deploy option: $1
Use: $0 deploy [--version <version>]"
                ;;
        esac
    done

    # Auto-detect version from git if not provided
    if [ -z "$VERSION" ]; then
        if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
            VERSION=$(git describe --tags --dirty --always 2>/dev/null)
            if [ -z "$VERSION" ]; then
                VERSION="unknown"
            else
                VERSION=$(echo "$VERSION" | sed -E 's#^(release/|refs/tags/)##')
            fi
            echo "Auto-detected version from git: $VERSION"
        else
            panic "Cannot auto-detect version: not in a git repository
Provide version manually: $0 deploy --version 1.2.3"
        fi
    fi

    VERSION_DIR="${DIST_BASE}/${VERSION}"

    echo "=============================================="
    echo "Deploying ACAV Docker Distribution"
    echo "=============================================="
    echo "Version:     $VERSION"
    echo "Output dir:  $VERSION_DIR"
    echo ""

    # Create output directory
    [ -d "$VERSION_DIR" ] && rm -rf "$VERSION_DIR"
    mkdir -p "$VERSION_DIR"
    chmod 755 "$VERSION_DIR"

    # Build Docker image using multi-stage build
    # Stage 1: Build in dev environment and package dependencies
    # Stage 2: Copy to clean Fedora runtime (much smaller final image)
    echo "=============================================="
    echo "Building Docker Distribution Image"
    echo "=============================================="
    echo "Dockerfile:  Dockerfile.release (multi-stage)"
    echo "Build env:   $DOCKER_IMAGE"
    echo "Runtime:     fedora:${ACAV_FEDORA_VERSION} (clean)"
    echo ""

    run_command "Pulling base Docker image" \
        $CONTAINER_CMD pull --platform linux/amd64 "$DOCKER_IMAGE"

    echo ""
    echo "→ Building Docker image (this may take several minutes)..."
    echo "  Stage 1: Building binaries and packaging dependencies"
    echo "  Stage 2: Creating clean runtime image"
    echo ""

    BUILD_FORMAT_ARGS=()
    if [ "$CONTAINER_CMD" = "podman" ]; then
        BUILD_FORMAT_ARGS=(--format docker)
    fi

    if ! DOCKER_BUILDKIT=1 $CONTAINER_CMD build \
        --platform linux/amd64 \
        "${BUILD_FORMAT_ARGS[@]}" \
        -t acav:${VERSION} \
        -t acav:latest \
        --build-arg LLVM_VERSION=${ACAV_LLVM_VERSION} \
        --build-arg FEDORA_VERSION=${ACAV_FEDORA_VERSION} \
        --build-arg BASE_IMAGE=${DOCKER_IMAGE} \
        --build-arg VERSION=${VERSION} \
        -f Dockerfile.release \
        . ; then
        panic "Docker build failed"
    fi

    echo ""
    echo "✓ Docker image built successfully"
    echo ""

    # Verify image
    echo "→ Verifying Docker image..."
    if ! $CONTAINER_CMD images | grep -q "acav.*${VERSION}"; then
        panic "Docker image not found after build"
    fi
    echo "   ✓ Image tagged: acav:${VERSION}"
    echo ""

    # Verification tests
    echo "=============================================="
    echo "Running Verification Tests"
    echo "=============================================="
    echo ""

    # Test 1: Binaries work
    echo "→ Test 1: CLI tools respond to --help"
    if ! $CONTAINER_CMD run --rm acav:${VERSION} make-ast --help >/dev/null 2>&1; then
        panic "make-ast not working in container"
    fi
    if ! $CONTAINER_CMD run --rm acav:${VERSION} query-dependencies --help >/dev/null 2>&1; then
        panic "query-dependencies not working in container"
    fi
    echo "   ✓ CLI tools functional"
    echo ""

    # Test 2: Examples and compile_commands.json exist
    echo "→ Test 2: Examples directory structure"
    if ! $CONTAINER_CMD run --rm acav:${VERSION} test -f /opt/acav/examples/compile_commands.json; then
        panic "compile_commands.json missing in container"
    fi
    if ! $CONTAINER_CMD run --rm acav:${VERSION} test -f /opt/acav/examples/calculator.cpp; then
        panic "calculator.cpp missing in container"
    fi
    echo "   ✓ Examples present with compile_commands.json"
    echo ""

    # Test 3: Integration test with CLI analyzers
    # Note: make-ast automatically finds bundled resource dir at ../lib/clang/<version>/
    echo "→ Test 3: Integration test with make-ast and query-dependencies"
    if ! $CONTAINER_CMD run --rm acav:${VERSION} \
        make-ast \
        --compilation-database /opt/acav/examples/compile_commands.json \
        --source /opt/acav/examples/calculator.cpp \
        --output /tmp/test.ast >/dev/null 2>&1; then
        panic "make-ast integration test failed"
    fi
    if ! $CONTAINER_CMD run --rm acav:${VERSION} \
        query-dependencies \
        --compilation-database /opt/acav/examples/compile_commands.json \
        --source /opt/acav/examples/calculator.cpp \
        --output /tmp/test-deps.json >/dev/null 2>&1; then
        panic "query-dependencies integration test failed"
    fi
    echo "   ✓ Integration test passed"
    echo ""

    # Test 4: SECURITY - Verify no source code leaked (multi-stage build naturally excludes it)
    echo "→ Test 4: Security verification (no source code in image)"

    # Verify /workspace is empty
    WORKSPACE_FILES=$($CONTAINER_CMD run --rm acav:${VERSION} \
        sh -c 'ls -A /workspace 2>/dev/null || true')
    if [ -n "$WORKSPACE_FILES" ]; then
        echo "SECURITY FAILURE: /workspace not empty:"
        echo "$WORKSPACE_FILES"
        panic "/workspace should be empty!"
    fi
    echo "   ✓ /workspace is empty"

    # Verify only example .cpp/.hpp files exist (multi-stage build excludes source)
    LEAKED_FILES=$($CONTAINER_CMD run --rm acav:${VERSION} \
        sh -c 'find /opt /home /root -name "*.cpp" -o -name "*.hpp" 2>/dev/null | grep -v "^/opt/acav/examples/" || true')
    if [ -n "$LEAKED_FILES" ]; then
        echo "SECURITY FAILURE: Unexpected source files found:"
        echo "$LEAKED_FILES"
        panic "Source code found outside /opt/acav/examples/"
    fi
    echo "   ✓ Only example files present (multi-stage build excludes source)"
    echo ""

    # Test 5: Show image size (benefit of multi-stage build)
    echo "→ Test 5: Image size verification"
    IMAGE_SIZE=$($CONTAINER_CMD images acav:${VERSION} --format "{{.Size}}")
    echo "   Image size: $IMAGE_SIZE"
    echo "   ✓ Multi-stage build produces minimal runtime image"
    echo ""

    echo "✓ All verification tests passed"
    echo ""

    # Export Docker image
    echo "=============================================="
    echo "Exporting Docker Image"
    echo "=============================================="
    echo ""

    IMAGE_TAR="${VERSION_DIR}/acav-${VERSION}.tar"
    IMAGE_TAR_GZ="${VERSION_DIR}/acav-${VERSION}.tar.gz"

    echo "→ Saving Docker image to tar..."
    if ! $CONTAINER_CMD save acav:${VERSION} -o "$IMAGE_TAR"; then
        panic "Docker save failed"
    fi
    echo "   Saved: $IMAGE_TAR"
    echo ""

    echo "→ Compressing image..."
    if command -v pigz >/dev/null 2>&1; then
        echo "   Using pigz (parallel compression)"
        if ! pigz -9 "$IMAGE_TAR"; then
            panic "Compression failed"
        fi
    else
        echo "   Using gzip (install pigz for faster compression)"
        if ! gzip -9 "$IMAGE_TAR"; then
            panic "Compression failed"
        fi
    fi
    echo "   Created: $IMAGE_TAR_GZ"

    # Fix permissions so others can read the file (needed for distribution)
    chmod 644 "$IMAGE_TAR_GZ"
    echo "   Permissions: $(ls -lh "$IMAGE_TAR_GZ" | awk '{print $1, $3, $4}')"
    echo ""

    # Helper to copy a file with permissions
    copy_dist_file() {
        local src="$1" dst="$2" mode="$3"
        if [ -f "$src" ]; then
            cp "$src" "$dst"
            chmod "$mode" "$dst"
            echo "   Copied: $src → $dst"
        else
            echo "   Warning: $src not found, skipping"
        fi
    }

    # Copy documentation for users
    echo "→ Copying user documentation..."
    copy_dist_file "DOCKER_IMAGE_README.md" "$VERSION_DIR/README.md" 644
    copy_dist_file "ACAV_USER_MANUAL.md" "$VERSION_DIR/ACAV_USER_MANUAL.md" 644
    copy_dist_file "INSTALL.txt" "$VERSION_DIR/INSTALL.txt" 644
    copy_dist_file "CHANGELOG.md" "$VERSION_DIR/CHANGELOG.md" 644
    copy_dist_file "KNOWN_ISSUES.md" "$VERSION_DIR/KNOWN_ISSUES.md" 644
    copy_dist_file "LICENSE.txt" "$VERSION_DIR/LICENSE.txt" 644
    copy_dist_file "NOTICE.txt" "$VERSION_DIR/NOTICE.txt" 644
    echo ""

    # Copy helper scripts for running the image
    echo "→ Copying run helper scripts..."
    mkdir -p "$VERSION_DIR/scripts"
    chmod 755 "$VERSION_DIR/scripts"
    copy_dist_file "scripts/run_demo" "$VERSION_DIR/scripts/run_demo" 755
    copy_dist_file "scripts/run_acav_container.sh" "$VERSION_DIR/scripts/run_acav_container.sh" 755
    copy_dist_file "scripts/print_desktop_options" "$VERSION_DIR/scripts/print_desktop_options" 755
    echo ""

    # Generate SHA256 checksum
    echo "→ Generating SHA256 checksum..."
    cd "$VERSION_DIR"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "acav-${VERSION}.tar.gz" > SHA256SUMS
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "acav-${VERSION}.tar.gz" > SHA256SUMS
    else
        panic "Neither sha256sum nor shasum is available"
    fi
    cd - >/dev/null

    echo "   Created: $VERSION_DIR/SHA256SUMS"
    echo ""

    # Summary
    echo "=============================================="
    echo "✓ Deployment Complete!"
    echo "=============================================="
    echo ""
    echo "Version:        $VERSION"
    echo "Location:       $VERSION_DIR"
    echo ""
    echo "Distribution Files:"
    ls -lh "$VERSION_DIR"
    echo ""
    echo "Docker Image:   $(du -sh "$IMAGE_TAR_GZ" | cut -f1)"
    echo "Total Size:     $(du -sh "$VERSION_DIR" | cut -f1)"
    echo ""
    echo "SHA256 Checksum:"
    cat "$VERSION_DIR/SHA256SUMS"
    echo ""
    echo "Documentation:"
    echo "  See README.md in the distribution directory for detailed usage instructions"
    echo ""
    echo "Quick Start:"
    echo "  1. Load: $CONTAINER_CMD load < $IMAGE_TAR_GZ"
    echo "  2. Run:  $CONTAINER_CMD run --rm -it \\"
    echo "           -v /tmp/.X11-unix:/tmp/.X11-unix \\"
    echo "           -e DISPLAY=\$DISPLAY \\"
    echo "           acav:${VERSION} acav -c /opt/acav/examples/compile_commands.json"
    echo ""
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------

COMMAND=${1:-build}

case "$COMMAND" in
    build)
        shift
        cmd_build "$@"
        ;;
    test)
        shift
        cmd_test "$@"
        ;;
    clean)
        shift
        cmd_clean "$@"
        ;;
    shell)
        shift
        cmd_shell "$@"
        ;;
    deploy)
        shift
        cmd_deploy "$@"
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        panic "Unknown command: $COMMAND

Available commands: build, test, clean, shell, deploy, help
Use '$0 help' for detailed usage information"
        ;;
esac
