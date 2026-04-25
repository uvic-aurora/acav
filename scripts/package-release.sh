#! /usr/bin/env bash

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
# ACAV Release Packaging Script
# =============================================================================
# This script packages ACAV binaries along with all required dependencies
# for distribution to systems without LLVM/Clang/Qt installed.
# =============================================================================

set -e

# Install patchelf if not present
if ! command -v patchelf &> /dev/null; then
    echo "Installing patchelf..."
    dnf install -y patchelf > /dev/null 2>&1
fi

VERSION="${1:-unknown}"
RELEASE_DIR="${2:-/workspace/release}"
BUILD_DIR="${3:-/workspace/out/linux-release}"

# Auto-detect LLVM version from installed llvm-config
LLVM_VERSION=$(llvm-config --version)                        # e.g. "22.1.0"
LLVM_MAJOR=$(llvm-config --version | cut -d. -f1)            # e.g. "22"
LLVM_MAJOR_MINOR=$(llvm-config --version | cut -d. -f1-2)    # e.g. "22.1"

echo "=== ACAV Release Packaging ==="
echo "Version: $VERSION"
echo "Release dir: $RELEASE_DIR"
echo "Build dir: $BUILD_DIR"
echo "LLVM version: $LLVM_VERSION (major: $LLVM_MAJOR)"
echo ""

# Create release directory structure
echo "Creating directory structure..."
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"/{bin,lib,lib/clang/${LLVM_MAJOR},plugins/platforms}

# =============================================================================
# 1. Copy binaries
# =============================================================================
echo "Copying binaries..."
cp "$BUILD_DIR/bin/acav" "$RELEASE_DIR/bin/"
cp "$BUILD_DIR/bin/make-ast" "$RELEASE_DIR/bin/"
cp "$BUILD_DIR/bin/query-dependencies" "$RELEASE_DIR/bin/"

# =============================================================================
# 2. Copy LLVM/Clang libraries (these are NOT on vanilla Fedora)
# =============================================================================
echo "Copying LLVM/Clang libraries..."
cp /lib64/libLLVM.so.${LLVM_MAJOR_MINOR} "$RELEASE_DIR/lib/"
cp /lib64/libclang-cpp.so.${LLVM_MAJOR_MINOR} "$RELEASE_DIR/lib/"

# Create symlinks for library loading
ln -sf libLLVM.so.${LLVM_MAJOR_MINOR} "$RELEASE_DIR/lib/libLLVM.so"
ln -sf libLLVM.so.${LLVM_MAJOR_MINOR} "$RELEASE_DIR/lib/libLLVM-${LLVM_MAJOR}.so"
ln -sf libclang-cpp.so.${LLVM_MAJOR_MINOR} "$RELEASE_DIR/lib/libclang-cpp.so"

# =============================================================================
# 3. Copy Qt6 libraries (these are NOT on vanilla Fedora)
# =============================================================================
echo "Copying Qt6 libraries..."

# Helper: copy a shared library by resolving its actual versioned filename,
# then create the standard .so.6 and .so symlinks automatically.
copy_lib_with_symlinks() {
    local pattern="$1"  # e.g. /lib64/libQt6Core.so.6.*
    local dest_dir="$2"
    local real_file
    real_file=$(ls $pattern 2>/dev/null | head -1)
    if [ -z "$real_file" ]; then
        return 1
    fi
    local base
    base=$(basename "$real_file")
    cp "$real_file" "$dest_dir/$base"
    # Create .so.MAJOR symlink (e.g. libQt6Core.so.6)
    local so_major
    so_major=$(echo "$base" | sed -E 's/(\.so\.[0-9]+)\..*/\1/')
    if [ "$so_major" != "$base" ]; then
        ln -sf "$base" "$dest_dir/$so_major"
    fi
    # Create bare .so symlink (e.g. libQt6Core.so)
    local so_bare
    so_bare=$(echo "$base" | sed -E 's/\.so\..*/\.so/')
    ln -sf "$base" "$dest_dir/$so_bare"
    echo "  $base"
}

# Required Qt6 libraries
for qt_lib in libQt6Core libQt6Gui libQt6Widgets libQt6DBus; do
    copy_lib_with_symlinks "/lib64/${qt_lib}.so.6.*" "$RELEASE_DIR/lib/" || \
        echo "  WARNING: ${qt_lib} not found"
done

# Optional Qt6 libraries
for qt_lib in libQt6XcbQpa libQt6OpenGL libQt6WaylandClient libQt6WaylandEglClientHwIntegration; do
    copy_lib_with_symlinks "/lib64/${qt_lib}.so.6.*" "$RELEASE_DIR/lib/" || true
done

# =============================================================================
# 4. Copy Qt platform plugins
# =============================================================================
echo "Copying Qt platform plugins..."
# Copy multiple platform plugins for flexibility
cp /usr/lib64/qt6/plugins/platforms/libqoffscreen.so "$RELEASE_DIR/plugins/platforms/"
cp /usr/lib64/qt6/plugins/platforms/libqminimal.so "$RELEASE_DIR/plugins/platforms/"
# XCB plugin for X11
if [ -f /usr/lib64/qt6/plugins/platforms/libqxcb.so ]; then
    cp /usr/lib64/qt6/plugins/platforms/libqxcb.so "$RELEASE_DIR/plugins/platforms/"
fi
# Wayland plugin for native Wayland support (e.g., Fedora KDE, GNOME on Wayland)
if [ -f /usr/lib64/qt6/plugins/platforms/libqwayland.so ]; then
    cp /usr/lib64/qt6/plugins/platforms/libqwayland.so "$RELEASE_DIR/plugins/platforms/"

    # Wayland shell integration plugins (required for desktop integration)
    mkdir -p "$RELEASE_DIR/plugins/wayland-shell-integration"
    if [ -d /usr/lib64/qt6/plugins/wayland-shell-integration ]; then
        cp /usr/lib64/qt6/plugins/wayland-shell-integration/*.so "$RELEASE_DIR/plugins/wayland-shell-integration/" 2>/dev/null || true
    fi

    # Wayland graphics integration (for rendering)
    mkdir -p "$RELEASE_DIR/plugins/wayland-graphics-integration-client"
    if [ -d /usr/lib64/qt6/plugins/wayland-graphics-integration-client ]; then
        cp /usr/lib64/qt6/plugins/wayland-graphics-integration-client/*.so "$RELEASE_DIR/plugins/wayland-graphics-integration-client/" 2>/dev/null || true
    fi

    # Wayland decoration client (for window decorations)
    mkdir -p "$RELEASE_DIR/plugins/wayland-decoration-client"
    if [ -d /usr/lib64/qt6/plugins/wayland-decoration-client ]; then
        cp /usr/lib64/qt6/plugins/wayland-decoration-client/*.so "$RELEASE_DIR/plugins/wayland-decoration-client/" 2>/dev/null || true
    fi
fi
# Linux framebuffer for headless servers
cp /usr/lib64/qt6/plugins/platforms/libqlinuxfb.so "$RELEASE_DIR/plugins/platforms/"

# =============================================================================
# 5. Copy clang resource directory (include files only - NO lib directory)
# =============================================================================
echo "Copying clang resource directory..."
cp -r /usr/lib/clang/${LLVM_MAJOR}/include "$RELEASE_DIR/lib/clang/${LLVM_MAJOR}/"
# Note: We intentionally skip /usr/lib/clang/${LLVM_MAJOR}/lib (sanitizer runtimes not needed)
# Note: We intentionally skip /usr/lib/clang/${LLVM_MAJOR}/share (not needed)

# =============================================================================
# 6. Copy additional dependencies that may not be on vanilla Fedora
# =============================================================================
echo "Copying additional shared libraries..."

# These libraries might not be on a minimal Fedora installation
# We'll copy them to be safe
# Library names to bundle (without version suffixes).
# The script resolves actual filenames via glob, so this works across
# Fedora versions where .so version numbers differ (e.g. libicuuc.so.77
# vs .so.76).
EXTRA_LIB_NAMES=(
    # ICU libraries (for Qt)
    libicui18n libicuuc libicudata
    # Double conversion (Qt dependency)
    libdouble-conversion
    # PCRE2 (Qt dependency)
    libpcre2-16
    # B2 (Qt dependency)
    libb2

    # LLVM dependencies
    libedit libxml2 libzstd libffi libtinfo liblzma

    # OpenGL/EGL (for Qt GUI)
    libGLX libOpenGL libGLdispatch libEGL

    # X11/GUI libraries
    libX11 libX11-xcb libXext libxcb libXau libxkbcommon

    # Wayland libraries (required by Qt Wayland plugin)
    libwayland-client libwayland-cursor libwayland-egl

    # X11 session management (required by Qt xcb plugin)
    libSM libICE

    # XCB utilities (required by Qt xcb platform plugin)
    libxcb-cursor libxcb-render libxcb-shape libxcb-xfixes
    libxcb-shm libxcb-icccm libxcb-image libxcb-keysyms
    libxcb-randr libxcb-render-util libxcb-sync libxcb-xinerama
    libxcb-xinput libxcb-xkb libxcb-util libxkbcommon-x11

    # Font/text rendering (fontconfig and dependencies)
    libfontconfig libfreetype libharfbuzz libpng16 libgraphite2
    libbrotlidec libbrotlicommon libbz2

    # Fontconfig cache and config dependencies
    libjson-c

    # D-Bus (for Qt)
    libdbus-1

    # GLib (for various dependencies)
    libglib-2.0 libpcre2-8

    # System libraries that might vary
    libexpat libuuid libsystemd libcap libgcrypt libgpg-error
)

for name in "${EXTRA_LIB_NAMES[@]}"; do
    # Find the real versioned .so file (e.g. /lib64/libicuuc.so.77)
    real_file=$(ls /lib64/${name}.so.[0-9]* 2>/dev/null | grep -v '\.so\.[0-9]*\.[0-9]' | head -1)
    if [ -z "$real_file" ]; then
        # Try fully-versioned (e.g. .so.1.2.3) as fallback
        real_file=$(ls /lib64/${name}.so.[0-9]* 2>/dev/null | head -1)
    fi
    if [ -n "$real_file" ] && [ -f "$real_file" ]; then
        cp "$real_file" "$RELEASE_DIR/lib/"
        base=$(basename "$real_file")
        # Create bare .so symlink
        ln -sf "$base" "$RELEASE_DIR/lib/${name}.so" 2>/dev/null || true
    fi
done

# =============================================================================
# 7. Bundle fontconfig configuration and fonts
# =============================================================================
echo "Copying fontconfig configuration..."
mkdir -p "$RELEASE_DIR/etc/fonts"
mkdir -p "$RELEASE_DIR/share/fonts"

# Copy fontconfig configuration
if [ -d /etc/fonts ]; then
    cp -r /etc/fonts/* "$RELEASE_DIR/etc/fonts/" 2>/dev/null || true
fi

# Copy fonts - try multiple possible locations
echo "Copying fonts..."
FONT_DIRS=(
    "/usr/share/fonts/dejavu-sans-fonts"
    "/usr/share/fonts/dejavu"
    "/usr/share/fonts/google-noto-sans-vf-fonts"
    "/usr/share/fonts/google-noto"
    "/usr/share/fonts/noto"
    "/usr/share/fonts/cantarell"
    "/usr/share/fonts/liberation-sans"
    "/usr/share/fonts/liberation"
    "/usr/share/fonts/TTF"
    "/usr/share/fonts/truetype"
)

FONTS_COPIED=0
for font_dir in "${FONT_DIRS[@]}"; do
    if [ -d "$font_dir" ]; then
        cp -r "$font_dir" "$RELEASE_DIR/share/fonts/" 2>/dev/null || true
        FONTS_COPIED=1
        echo "  Copied: $font_dir"
    fi
done

# If no fonts found in standard locations, try to find any TTF/OTF fonts
if [ "$FONTS_COPIED" -eq 0 ]; then
    echo "  Looking for any available fonts..."
    find /usr/share/fonts -name "*.ttf" -o -name "*.otf" 2>/dev/null | head -20 | while read font; do
        cp "$font" "$RELEASE_DIR/share/fonts/" 2>/dev/null || true
    done
fi

# Create a minimal fonts.conf that points to our bundled fonts
cat > "$RELEASE_DIR/etc/fonts/fonts.conf" << 'FONTCONF_EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <!-- Bundled font directories -->
  <dir>/opt/acav/share/fonts</dir>

  <!-- Font cache directory -->
  <cachedir>/tmp/fontconfig-cache</cachedir>

  <!-- Default font families -->
  <alias>
    <family>sans-serif</family>
    <prefer><family>Noto Sans</family></prefer>
    <prefer><family>Adwaita Sans</family></prefer>
    <prefer><family>Cantarell</family></prefer>
  </alias>
  <alias>
    <family>serif</family>
    <prefer><family>Noto Sans</family></prefer>
  </alias>
  <alias>
    <family>monospace</family>
    <prefer><family>Adwaita Mono</family></prefer>
  </alias>

  <!-- Enable antialiasing -->
  <match target="font">
    <edit name="antialias" mode="assign"><bool>true</bool></edit>
    <edit name="hinting" mode="assign"><bool>true</bool></edit>
    <edit name="hintstyle" mode="assign"><const>hintslight</const></edit>
    <edit name="rgba" mode="assign"><const>rgb</const></edit>
  </match>
</fontconfig>
FONTCONF_EOF

# Create cache directory
mkdir -p "$RELEASE_DIR/var/cache/fontconfig"

# =============================================================================
# 8. Set RPATH so binaries and libraries find bundled libraries automatically
# =============================================================================
echo "Setting RPATH in binaries and libraries..."

# Use patchelf to set RPATH relative to binary/library location
# $ORIGIN is a special token that means "directory containing the file"

# Binaries look in ../lib
patchelf --set-rpath '$ORIGIN/../lib' "$RELEASE_DIR/bin/acav"
patchelf --set-rpath '$ORIGIN/../lib' "$RELEASE_DIR/bin/make-ast"
patchelf --set-rpath '$ORIGIN/../lib' "$RELEASE_DIR/bin/query-dependencies"

# All bundled libraries need to find each other in the same directory
for lib in "$RELEASE_DIR/lib/"*.so*; do
    if [ -f "$lib" ] && [ ! -L "$lib" ]; then
        patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true
    fi
done

# Qt plugins look in ../../lib (platforms, wayland-shell-integration, etc.)
for plugin in "$RELEASE_DIR/plugins/platforms/"*.so; do
    patchelf --set-rpath '$ORIGIN/../../lib' "$plugin" 2>/dev/null || true
done
if [ -d "$RELEASE_DIR/plugins/wayland-shell-integration" ]; then
    for plugin in "$RELEASE_DIR/plugins/wayland-shell-integration/"*.so; do
        [ -f "$plugin" ] && patchelf --set-rpath '$ORIGIN/../../lib' "$plugin" 2>/dev/null || true
    done
fi
if [ -d "$RELEASE_DIR/plugins/wayland-graphics-integration-client" ]; then
    for plugin in "$RELEASE_DIR/plugins/wayland-graphics-integration-client/"*.so; do
        [ -f "$plugin" ] && patchelf --set-rpath '$ORIGIN/../../lib' "$plugin" 2>/dev/null || true
    done
fi
if [ -d "$RELEASE_DIR/plugins/wayland-decoration-client" ]; then
    for plugin in "$RELEASE_DIR/plugins/wayland-decoration-client/"*.so; do
        [ -f "$plugin" ] && patchelf --set-rpath '$ORIGIN/../../lib' "$plugin" 2>/dev/null || true
    done
fi

# =============================================================================
# 9. Create qt.conf so Qt finds plugins without env vars
# =============================================================================
echo "Creating qt.conf..."

cat > "$RELEASE_DIR/bin/qt.conf" << 'QTCONF_EOF'
[Paths]
Prefix = ..
Plugins = plugins
QTCONF_EOF

# =============================================================================
# 10. Create example files
# =============================================================================
echo "Creating example files..."
mkdir -p "$RELEASE_DIR/examples"

cat > "$RELEASE_DIR/examples/hello.cpp" << 'EXAMPLE_EOF'
// Example C++ file for ACAV testing
#include <iostream>

int main() {
    std::cout << "Hello, ACAV!" << std::endl;
    return 0;
}
EXAMPLE_EOF

# =============================================================================
# 11. Strip binaries to reduce size
# =============================================================================
echo "Stripping binaries..."
strip "$RELEASE_DIR/bin/acav" 2>/dev/null || true
strip "$RELEASE_DIR/bin/make-ast" 2>/dev/null || true
strip "$RELEASE_DIR/bin/query-dependencies" 2>/dev/null || true

# =============================================================================
# 12. Report package size
# =============================================================================
echo ""
echo "=== Package Contents ==="
echo ""
echo "Binaries:"
ls -lh "$RELEASE_DIR/bin/acav" "$RELEASE_DIR/bin/make-ast" "$RELEASE_DIR/bin/query-dependencies" 2>/dev/null | awk '{print "  " $5 " " $9}'
echo ""
echo "Libraries:"
ls -lh "$RELEASE_DIR/lib/"*.so* 2>/dev/null | head -20 | awk '{print "  " $5 " " $9}'
echo ""
echo "Qt Plugins:"
ls -lh "$RELEASE_DIR/plugins/platforms/"*.so 2>/dev/null | awk '{print "  " $5 " " $9}'
echo ""
echo "Clang Resource (include only):"
du -sh "$RELEASE_DIR/lib/clang/${LLVM_MAJOR}/include"
echo ""
echo "=== Total Package Size ==="
du -sh "$RELEASE_DIR"
echo ""
echo "=== Packaging Complete ==="
