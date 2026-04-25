# ACAV Docker/Podman Demo Image - User Guide {#demo_image}

**ACAV (Aurora Clang AST Viewer)** - an Abstract Syntax Tree visualization
tool for Clang's C-family languages

## Prerequisites

- Docker or Podman.
- A desktop display server for the GUI:
  - Linux: X11 or Wayland.
  - macOS: XQuartz.
  - Windows: WSLg, VcXsrv, or Xming.

The release image is currently built for `linux/amd64`. On ARM hosts such as
Apple Silicon, pass `--platform linux/amd64` to Docker or Podman.

Throughout this guide, examples use `v1.0.0`. Replace this with the release
version that you downloaded or pulled.

```bash
acav_version=v1.0.0
acav_llvm=22
image=ghcr.io/uvic-aurora/acav:${acav_version}-llvm${acav_llvm}
runtime=docker
```

Use `runtime=podman` if you prefer Podman. Release images are published for
LLVM 20, 21, and 22. LLVM 22 is the default recommended image. The shorter tag
`ghcr.io/uvic-aurora/acav:$acav_version` is also published as an alias for the
LLVM 22 image.

## Quick Start {#demo_image_quick_start}

The easiest way to get the image is to pull it from GitHub Container Registry:

```bash
$runtime pull --platform linux/amd64 $image
```

Confirm the command-line tools work:

```bash
$runtime run --rm --platform linux/amd64 $image make-ast --help
$runtime run --rm --platform linux/amd64 $image query-dependencies --help
```

To use the downloadable release archive instead, download these two files from
the [ACAV releases page](https://github.com/uvic-aurora/acav/releases):

- `acav-v1.0.0-llvm22.tar.gz`
- `SHA256SUMS-llvm22`

Verify and load the archive:

```bash
sha256sum -c SHA256SUMS-llvm$acav_llvm
$runtime load -i acav-$acav_version-llvm$acav_llvm.tar.gz
```

After loading the archive, use the local image tag:

```bash
image=acav:${acav_version}-llvm${acav_llvm}
```

Some Podman setups may tag loaded images as
`localhost/acav:${acav_version}-llvm${acav_llvm}`. If the `acav:` tag is not
found, try:

```bash
image=localhost/acav:${acav_version}-llvm${acav_llvm}
```

## Running the GUI Application {#demo_image_running_gui}

ACAV is a GUI application, so the container needs access to the host display.

### Linux with X11

```bash
$runtime run --rm -it --platform linux/amd64 \
  -e DISPLAY=$DISPLAY \
  -e QT_QPA_PLATFORM=xcb \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  $image \
  acav -c /opt/acav/examples/compile_commands.json
```

If your X server rejects the connection, allow local container clients before
running ACAV:

```bash
xhost +local:
```

### Linux with Wayland

```bash
$runtime run --rm -it --platform linux/amd64 \
  -e XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR \
  -e WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0} \
  -e QT_QPA_PLATFORM=wayland \
  -v "$XDG_RUNTIME_DIR/${WAYLAND_DISPLAY:-wayland-0}:$XDG_RUNTIME_DIR/${WAYLAND_DISPLAY:-wayland-0}:rw" \
  $image \
  acav -c /opt/acav/examples/compile_commands.json
```

If Wayland forwarding fails, use the X11 command above.

### macOS with XQuartz

Start XQuartz and allow local connections:

```bash
open -a XQuartz
xhost + 127.0.0.1
```

Then run ACAV with X11 forwarding through Docker Desktop:

```bash
$runtime run --rm -it --platform linux/amd64 \
  -e DISPLAY=host.docker.internal:0 \
  -e QT_QPA_PLATFORM=xcb \
  $image \
  acav -c /opt/acav/examples/compile_commands.json
```

For Podman on macOS, replace `host.docker.internal` with
`host.containers.internal`.

### What You Should See

1. A GUI window appears.
2. The left panel shows a source file list with `calculator.cpp`.
3. Double-click `calculator.cpp` to generate and view its AST.
4. Click AST nodes to highlight corresponding source code.

For detailed GUI usage, see the
[online manual](https://uvic-aurora.github.io/acav-manual/index.html).

## Working with Your Own Code

Mount your project directory and point ACAV to your compilation database:

```bash
project=/path/to/your/project

$runtime run --rm -it --platform linux/amd64 \
  -e DISPLAY=$DISPLAY \
  -e QT_QPA_PLATFORM=xcb \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v "$project:$project:rw" \
  -w "$project" \
  $image \
  acav -c "$project/compile_commands.json"
```

For command-line use:

```bash
project=/path/to/your/project

$runtime run --rm --platform linux/amd64 \
  -v "$project:$project:rw" \
  -w "$project" \
  $image \
  make-ast \
    --compilation-database "$project/compile_commands.json" \
    --source "$project/path/to/source.cpp" \
    --output "$project/source.ast"
```

Paths in `compile_commands.json` must match paths visible inside the container.
The examples above mount the project at the same absolute path to preserve path
compatibility.

The runtime image does not bundle your project compiler, C++ standard library
headers, SDKs, or cross toolchains. If your compilation database relies on
implicit toolchain include paths, make sure the recorded compiler is available
inside the container, or regenerate the database in the same container/mount
layout you use for ACAV.

## What's Included

### GitHub Release Assets

The GitHub Release provides:

- `acav-vX.Y.Z-llvm20.tar.gz`: compressed OCI image archive built with LLVM 20.
- `acav-vX.Y.Z-llvm21.tar.gz`: compressed OCI image archive built with LLVM 21.
- `acav-vX.Y.Z-llvm22.tar.gz`: compressed OCI image archive built with LLVM 22.
- `SHA256SUMS-llvm20`, `SHA256SUMS-llvm21`, and `SHA256SUMS-llvm22`: checksum
  files for verifying the matching image archives.

GitHub also provides source-code archives automatically.

The same images are published to GitHub Container Registry with tags
`vX.Y.Z-llvm20`, `vX.Y.Z-llvm21`, and `vX.Y.Z-llvm22`. The release tag
`vX.Y.Z` points to the LLVM 22 image. For non-prerelease builds, `latest` also
points to the LLVM 22 image.

### Inside the Container Image

Binaries in `/opt/acav/bin/`:

- `acav`: GUI application for interactive AST visualization.
- `make-ast`: CLI tool to generate AST cache files.
- `query-dependencies`: CLI tool to analyze header dependencies.

Example files in `/opt/acav/examples/`:

- `calculator.cpp` and `calculator.hpp`: sample C++ code.
- `compile_commands.json`: compilation database for the example.

## Troubleshooting

### "Cannot open display" or "No protocol specified"

Check that the container receives the right display environment and socket
mounts. On Linux/X11, verify:

```bash
echo $DISPLAY
ls /tmp/.X11-unix
```

If needed, allow local X11 clients:

```bash
xhost +local:
```

### "XDG_RUNTIME_DIR is invalid" or Wayland startup fails

The container is trying to use Wayland without a valid Wayland runtime socket.
Use X11 instead:

```bash
$runtime run --rm -it --platform linux/amd64 \
  -e DISPLAY=$DISPLAY \
  -e QT_QPA_PLATFORM=xcb \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  $image \
  acav -c /opt/acav/examples/compile_commands.json
```

### Image Naming Issues

If you loaded the archive with Podman and
`acav:${acav_version}-llvm${acav_llvm}` is not found, try
`localhost/acav:${acav_version}-llvm${acav_llvm}`.

## License

See `LICENSE.txt` and `NOTICE.txt` in the source repository for license and
attribution information.

## Support

For issues or questions, use the
[repository issue tracker](https://github.com/uvic-aurora/acav/issues).
