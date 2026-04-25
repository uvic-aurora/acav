# LLVM/Clang Upgrade Guide

## 1. How to Upgrade

### Docker Release

1. **Build a new base image** with the target LLVM/Clang version and Qt installed
   on Fedora (or your chosen distro). Publish it to your container registry, e.g.
   `ghcr.io/smartai/llvm-qt-dev-env:fedora-llvmXX`.

2. **Update the image tag** in these three files:

   | File | Line to change |
   |------|---------------|
   | `scripts/build.sh` | `DOCKER_IMAGE="ghcr.io/smartai/llvm-qt-dev-env:fedora-llvmXX"` |
   | `Dockerfile.release` | `ARG BASE_IMAGE="ghcr.io/smartai/llvm-qt-dev-env:fedora-llvmXX"` |
   | `.github/workflows/ci.yml` | `image: ghcr.io/smartai/llvm-qt-dev-env:fedora-llvmXX` |

3. **Build and run tests** in Docker:

   ```bash
   ./scripts/build.sh build
   ./scripts/build.sh test
   ```

4. **Fix any API breakage** (see upgrade history below for common issues).

5. **Update documentation** — version references in `README.md` and `CLAUDE.md`.

Note: `scripts/package-release.sh` and `Dockerfile.release` auto-detect the LLVM
version at build time (`llvm-config --version` / `clang --version`), so they do
not need version edits.

### Native Build (from source)

1. **Upgrade LLVM/Clang** to the target version on your system:
   - macOS: `brew install llvm` (or `brew upgrade llvm`)
   - Ubuntu/Debian: install from [LLVM apt repository](https://apt.llvm.org/)
   - Fedora: `dnf install llvm-devel clang-devel`

2. **Configure and build:**

   ```bash
   cmake --preset=macos-debug    # or linux-debug
   cmake --build out/macos-debug
   ```

3. **Fix any compilation errors** — typically in:
   - `common/ClangUtils.cpp` — direct Clang API calls (`ASTUnit`, `DiagnosticsEngine`)
   - `app/core/AcavAstBuilder.cpp` — `RecursiveASTVisitor` overrides (signatures change frequently)
   - Any file mixing Qt and Clang headers — check for macro conflicts

4. **Run tests:**

   ```bash
   ctest --test-dir out/macos-debug
   ```

5. **Update documentation** — version references in `README.md` and `CLAUDE.md`.

### Files That Are Already Version-Agnostic (No Edits Needed)

| File | How it detects the version |
|------|---------------------------|
| `scripts/package-release.sh` | `llvm-config --version` at package time |
| `Dockerfile.release` (resource-dir) | `clang --version` at Docker build time |
| `CMakeLists.txt` / `CMakePresets.json` | `find_package(LLVM CONFIG)` |
| `common/ClangUtils.cpp` (version checks) | `LLVM_VERSION_MAJOR` compile-time macro |

---

## 2. Multi-Version Support

ACAV supports the last N LLVM versions (currently N=3: LLVM 20, 21, and 22) using
conditional compilation with `#if LLVM_VERSION_MAJOR >= X` guards. The macro
comes from `<llvm/Config/llvm-config.h>` (transitively included via Clang
headers).

### Supported Versions

| LLVM Version | Status |
|-------------|--------|
| 22 | Current |
| 21 | Supported |
| 20 | Supported |
| 19 and below | Rejected at CMake configure time |

### Where the Guards Live

Only two source files need version guards:

| File | Guards | What changes |
|------|--------|-------------|
| `common/ClangUtils.cpp` | 4 | Header include, `scope_exit`, `CreateASTUnitFromCommandLine`, `LoadFromASTFile` param order |
| `app/core/AcavAstBuilder.cpp` | ~8 | `TraverseType`/`TraverseTypeLoc` signatures, `TraverseNestedNameSpecifier` value-vs-pointer, `createNodeFromNestedNameSpec` |

### Adding LLVM 23

1. Bump `ACAV_LLVM_MAX_VERSION` to 23 in `CMakeLists.txt`
2. Add `#elif LLVM_VERSION_MAJOR >= 23` branches for any new API changes
3. Add LLVM 23 row to CI matrix in `.github/workflows/ci.yml`
4. Optionally drop LLVM 21 by bumping `ACAV_LLVM_MIN_VERSION`

### Building with a Specific LLVM Version

```bash
# Docker (default: LLVM 22)
ACAV_LLVM_VERSION=21 ./scripts/build.sh build

# Dockerfile.release
docker build --build-arg LLVM_VERSION=21 -f Dockerfile.release .
```

### Reference Pattern

The `#if` guard pattern follows the approach from the
[clang_libraries_companion](https://github.com/mdadams/clang_libraries_companion/blob/master/miscellany/examples/dump_ast_2/)
repository.

---

## 3. Upgrade History

### LLVM 21 → 22 (March 2026)

#### API Removals / Relocations

- **`ASTUnit::LoadFromCommandLine` removed** — replaced by free function
  `clang::CreateASTUnitFromCommandLine` in new header `<clang/Driver/CreateASTUnitFromArgs.h>`.
  Same signature, different location.

- **`ASTUnit::LoadFromASTFile` parameter reorder** — `VFS` moved from last position
  to 4th parameter (before `DiagOpts`).

#### AST Representation Changes

- **`NestedNameSpecifier` changed from pointer to value type** — was a heap-allocated
  linked list (`NestedNameSpecifier *`), now a lightweight value handle. Qualifiers are
  stored directly inside `Type` nodes. `ElaboratedType` node was removed entirely.
  Overrides of `TraverseNestedNameSpecifier` must take by value; null check via
  `operator bool()`.

- **`TraverseType` / `TraverseTypeLoc` gained `bool TraverseQualifier` parameter** —
  needed because qualifiers now live inside type nodes. The flag prevents double-visiting
  when the visitor is already inside `TraverseNestedNameSpecifier`. Overrides must add
  the parameter and forward it to the base class.

#### Deprecations

- **`llvm::make_scope_exit` deprecated** — use `llvm::scope_exit` constructor directly
  (CTAD). Removal planned in LLVM 24.

#### Qt Integration Issues

- **Qt `emit` macro conflict with `Sema.h`** — LLVM 22 added a transitive include
  `ASTUnit.h → ASTWriter.h → Sema.h`, and `Sema.h` has a method named `emit()` that
  conflicts with Qt's `#define emit`. Fix: `#undef emit` before Clang includes, optionally
  `#define emit` after if the file uses Qt signals.

---

## 4. API Change References

### LLVM 21 → 22

| Change | PR / Link |
|--------|-----------|
| `LoadFromCommandLine` → `CreateASTUnitFromCommandLine` | [PR #165277](https://github.com/llvm/llvm-project/pull/165277) — Remove clangDriver dependency from clangFrontend |
| `NestedNameSpecifier` value type + `ElaboratedType` removal + `TraverseQualifier` param | [PR #147835](https://github.com/llvm/llvm-project/pull/147835) — Improve nested name specifier AST representation |
| `make_scope_exit` deprecated | [PR #174109](https://github.com/llvm/llvm-project/pull/174109) — Deprecate `make_scope_exit` in favour of CTAD |
| Qt `emit` conflict (transitive Sema.h include) | Side effect of [PR #165277](https://github.com/llvm/llvm-project/pull/165277) |

**Why `NestedNameSpecifier` was changed** (from the PR description):
The old pointer-based linked list required heap allocation for every qualifier.
The new value handle stores qualifiers inline in types, making canonicalization
allocation-free. `ElaboratedType` was removed — qualifiers are now embedded in
each type node directly. The PR measured ~7-12% compilation speedup on heavy
template code. `TagType` can now point to the exact declaration found during
lookup (not just the canonical one), which improves type sugar for tools that
care about source origins.

**Why `TraverseQualifier` was added:**
Since qualifiers now live inside type nodes, `TraverseNestedNameSpecifier` calls
`TraverseType(T, /*TraverseQualifier=*/false)` to traverse the type component
without re-traversing its qualifier (which would cause double-visiting or infinite
recursion). Normal top-level type traversal uses `TraverseQualifier=true` (the
default), which traverses both the qualifier and the type.

**General references:**
- LLVM release notes: https://releases.llvm.org/
- Clang 22.1.0 release notes: https://releases.llvm.org/22.1.0/tools/clang/docs/ReleaseNotes.html
- LLVM 22.1.0 release notes: https://releases.llvm.org/22.1.0/docs/ReleaseNotes.html
- Clang API docs (Doxygen): https://clang.llvm.org/doxygen/
- Clang ASTUnit API: https://clang.llvm.org/doxygen/classclang_1_1ASTUnit.html
- LLVM Discourse (RFCs): https://discourse.llvm.org/c/clang/6
- LLVM GitHub PRs: https://github.com/llvm/llvm-project/pulls
