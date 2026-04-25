# Known Issues and Planned Improvements

**Last Updated:** 2026-02-10

---

## Summary

| Category            | Issue                          | Status  | Date       |
| ------------------- | ------------------------------ | ------- | ---------- |
| UI/UX               | Source Syntax Highlighting     | Fixed   | 2026-02-10 |
| UI/UX               | AST Search Popup & History     | Fixed   | 2026-02-10 |
| UI/UX               | AST Compilation Warning Banner | Fixed   | 2026-02-10 |
| AST Generation      | Stale AST Cache Auto-Recovery  | Fixed   | 2026-02-10 |
| Code Quality        | Aurora to ACAV Terminology     | Fixed   | 2026-02-10 |
| Build/Release       | Container Image Prefix         | Fixed   | 2026-02-04 |
| Build/Release       | Container Runner Script        | Fixed   | 2026-02-04 |
| UI/UX               | AST Node Search                | Fixed   | 2026-01-31 |
| File Explorer       | Collapse All Headers Bug       | Fixed   | 2026-01-26 |
| Documentation       | File Explorer Interactions     | Fixed   | 2026-01-26 |
| UI/UX               | Rename "AST Tree" to "AST"     | Fixed   | 2026-01-26 |
| UI/UX               | File Path in Source Code       | Fixed   | 2026-01-26 |
| UI/UX               | File Path in AST Window        | Fixed   | 2026-01-26 |
| UI/UX               | Per-Subwindow Font Size        | Fixed   | 2026-01-26 |
| UI/UX               | NodePtr Hex Format             | Fixed   | 2026-01-26 |
| UI/UX               | Extra Blank Line at EOF        | Fixed   | 2026-01-23 |
| AST Generation      | Comment Nodes                  | Fixed   | 2026-01-21 |
| AST Generation      | VarDecl isUsed Semantics       | Ongoing | 2026-01-31 |
| AST Generation      | Enum Label Properties          | Ongoing | 2026-01-31 |
| Build/Release       | Ship Dependencies              | Fixed   | 2026-01-21 |
| UI/UX               | Expand/Collapse Simplification | Fixed   | 2026-01-20 |
| UI/UX               | Reload Configuration           | Fixed   | 2026-01-20 |
| UI/UX               | Floating Dock Window Drag      | Fixed   | 2026-01-20 |
| UI/UX               | Interactive Zoom               | Fixed   | 2026-01-20 |
| AST Generation      | stddef.h Not Found in Docker   | Fixed   | 2026-01-20 |
| AST Generation      | C++20 Concepts                 | Fixed   | 2026-01-20 |
| AST Generation      | Node Information Display       | Fixed   | 2026-01-20 |
| Code Quality        | getClangResourceDir()          | Fixed   | 2026-01-20 |
| UI/UX               | AST Tree Placeholder           | Fixed   | 2026-01-19 |
| Performance         | TU Tree Loading                | Fixed   | 2026-01-19 |
| Navigation          | TranslationUnitDecl Sync       | Fixed   | 2026-01-19 |
| UI/UX               | Log Panel Features             | Fixed   | 2026-01-18 |
| AST Generation      | Node Type Names                | Fixed   | 2026-01-16 |
| AST Generation      | Macro Processing               | Fixed   | 2026-01-16 |
| Build/Release       | Automatic Versioning           | Fixed   | 2026-01-16 |
| Performance         | UI Responsiveness              | Fixed   | 2026-01-16 |
| UI/UX               | Keyboard Shortcuts             | Fixed   | 2026-01-15 |
| File Explorer       | Sync Issue                     | Fixed   | 2026-01-15 |
| UI/UX               | Log Dock Window                | Fixed   | 2026-01-14 |
| UI/UX               | Open Project Dialog            | Fixed   | 2026-01-14 |
| Declaration Context | Navigation                     | Fixed   | 2026-01-14 |
| Declaration Context | Applicability Rules            | Fixed   | 2026-01-13 |
| Performance         | AST Extraction                 | Fixed   | 2026-01-13 |
| Documentation       | Language Support               | Fixed   | 2026-01-10 |
| Documentation       | Compiler Compatibility         | Fixed   | 2026-01-10 |
| File Explorer       | Long Path Names                | Fixed   | 2026-01-08 |
| Documentation       | Testing Recommendations        | Ongoing | -          |

---

## Open Issues

### VarDecl isUsed Semantics (2026-01-31)

- In Clang, some `constexpr` variables are "referenced" but not "used" (ODR-used), so exporting `VarDecl::isUsed()` can show `false` even when the name appears in code (e.g., `kAppName` in `app/main.cpp`).
- Implemented: keep `isUsed` for `VarDecl` as the original Clang `VarDecl::isUsed()` (ODR-use), and also export `isReferenced` from `VarDecl::isReferenced()` for source-level usage.
- Needs verification in the GUI and with real project ASTs.

### Enum Label Properties (2026-01-31)

- Some extracted properties are raw enum codes (e.g., `valueKind`, `objectKind`, `initStyle`) which are hard to interpret in the UI.
- Implemented: export `valueKindName`, `objectKindName`, `initStyleName`, and `storageClassName` alongside the raw numeric values.
- Needs verification that the UI layout remains readable and that the new properties are consistent across Clang versions.

---

## Fixed Issues

### Source Syntax Highlighting (2026-02-10)

- Added lightweight C/C++ highlighting in Source Code view: keywords, comments, strings/chars, and function-like identifiers.
- Kept search/navigation highlights compatible with syntax coloring.
- Covered by `SourceCodeViewTests` syntax-highlight test.

### AST Search Popup and Query History (2026-02-10)

- AST search now opens in a floating popup from the quick search box and follows AST panel resize/move.
- Added per-session query history with completer suggestions and deduplication.
- Popup font follows configured base font size.

### AST Compilation Warning Banner (2026-02-10)

- Added inline warning banner in AST panel when `make-ast` reports compilation errors.
- Warning auto-clears on the next extraction without errors.

### Stale AST Cache Auto-Recovery (2026-02-10)

- On cached AST load failure, stale `.ast` files are deleted and regenerated automatically (no confirmation prompt).
- Non-cache extraction errors keep existing behavior.

### Aurora to ACAV Terminology (2026-02-10)

- Renamed Aurora terminology to ACAV across codebase, docs, and build files.
- Renamed core builder files (`AuroraAstBuilder` -> `AcavAstBuilder`) for consistency.

### Container Image Prefix Compatibility (2026-02-04)

- Runner scripts now resolve both `acav:<tag>` and `localhost/acav:<tag>` image names (Docker/Podman).
- Prevents false "image not found" failures caused by runtime-specific tagging.

### Container Runner Script Robustness (2026-02-04)

- Added `scripts/run_acav_container.sh` with explicit options for runtime/image/tar/workspace/user/shell modes.
- Hardened display option prechecks and runtime option construction for more reliable local launches.

### AST Node Search (2026-01-31)

- Added search bar in the AST panel for finding nodes by property (kind, name, type, etc.).
- Supports bare text search across all properties and qualified syntax (`kind:FunctionDecl name:main`) for targeted matching.
- Case-insensitive regex; qualified values use exact (anchored) matching; multiple qualifiers combined with AND logic.
- Prev/Next navigation with match counter (e.g., "3 of 15") and circular wrapping.
- "Project" filter checkbox (on by default) restricts results to project source files, excluding system headers.

### Collapse All Headers Bug (2026-01-26)

- "Collapse All" on directory didn't collapse header nodes inside source files; headers were only hidden, not collapsed.
- Fixed `collapseTuDirectories()` to traverse into source file subtrees and collapse expanded headers.

### File Explorer Interactions Documentation (2026-01-26)

- Added "File Explorer Interactions" section to user manual explaining the two-action model.
- Single-click: view source code only; Double-click: view source + load AST.
- Includes action table and example scenario.

### Rename "AST Tree" to "AST" (2026-01-26)

- Renamed all occurrences of "AST Tree" to "AST" in UI labels, menu items, and help dialog.
- The term "AST Tree" was redundant (Abstract Syntax Tree Tree).

### File Path in Source Code Window (2026-01-26)

- Added subtitle display in dock title bar showing file path.
- Format: `[project] relative/path` for project files, `[external] /absolute/path` for external files.
- Long paths are elided in the middle (e.g., `[project] src/.../Main.cpp`); full path shown in tooltip.

### File Path in AST Window (2026-01-26)

- Added subtitle display showing main source file path for the translation unit.
- Uses same format as Source Code window: `[project]` or `[external]` prefix.
- Long paths are elided in the middle; full path shown in tooltip.

### Per-Subwindow Font Size (2026-01-26)

- Ctrl+/- now adjusts font size only for the focused subwindow instead of globally.
- Each dock (File Explorer, Source Code, AST, Declaration Context, Logs) maintains its own font size.
- Ctrl+0 resets the focused subwindow to the default font size.

### NodePtr Hex Format (2026-01-26)

- `nodePtr` values in Node Details dialog now display in hex format (e.g., `0x7d7b25f40`).
- Other numeric fields remain in decimal format.

### Extra Blank Line at EOF (2026-01-23)

- Source file view showed an extra blank line/line number at EOF when the file ended with a trailing line break.
- Fixed by trimming exactly one trailing line break in `SourceCodeView::loadFile()` before calling `setPlainText()`.

### Ship Dependencies (2026-01-21)

- Bundled Clang/LLVM 21, Qt6, and all runtime dependencies into container image
- Fixed crashes from uninitialized pointers and added defensive validation for config values

### Comment Nodes (2026-01-21)

- Comments extracted via `ASTContext::getCommentForDecl()` and stored as `"comment"` property on Decl nodes
- Enabled by default; visible in Node Details dialog

### Expand/Collapse Simplification (2026-01-20)

- Context-aware "Expand All" / "Collapse All" in File Explorer
- Directory nodes: expand to source files only; file nodes: expand all descendants

### Reload Configuration (2026-01-20)

- Added "Reload Configuration" menu item for runtime config changes

### Floating Dock Window Drag (2026-01-20)

- Tested on macOS and Docker/Linux; no issues found

### stddef.h Not Found in Docker (2026-01-20)

- Added `-resource-dir` to command-line arguments in `createAstFromCDB()`

### C++20 Concepts (2026-01-20)

- Verified behavior is correct; no code changes needed

### getClangResourceDir() (2026-01-20)

- Discovers resource dir via `clang++ -print-resource-dir`

### Node Information Display (2026-01-20)

- Right-click node → "View Details..." opens non-modal dialog with all properties
- Tree view for hierarchical JSON; data copied to survive TU changes

### Interactive Zoom (2026-01-20)

- Added Ctrl++/Ctrl+- to zoom in/out, Ctrl+0 to reset

### AST Tree Placeholder (2026-01-19)

- Shows placeholder message when no AST is loaded

### TU Tree Loading (2026-01-19)

- Lazy header population; headers loaded on expand via `fetchMore()`

### TranslationUnitDecl Sync (2026-01-19)

- Main source file highlighted when navigating to nodes without source location

### Log Panel Features (2026-01-18)

- Added find, copy, clear, and auto-scroll toggle

### Node Type Names (2026-01-16)

- Uses full Clang class names (e.g., `NamespaceDecl`, `FunctionProtoType`)

### Macro Processing (2026-01-16)

- Displays spelling location instead of expansion location

### Automatic Versioning (2026-01-16)

- Version from `git describe`; displayed in CLI and GUI

### UI Responsiveness (2026-01-16)

- Replaced `setStyleSheet()` with `QPalette`; optimized expand/collapse

### Keyboard Shortcuts (2026-01-15)

- Full shortcut support; see Help > Keyboard Shortcuts

### Sync Issue (2026-01-15)

- Fixed path normalization to resolve symlinks consistently

### Log Dock Window (2026-01-14)

- Starts with ~4 lines height, user resizable

### Open Project Dialog (2026-01-14)

- Simplified labels, removed misleading placeholder text

### Declaration Context Navigation (2026-01-14)

- Clicking context entries navigates to that node in AST Tree

### Declaration Context Applicability (2026-01-13)

- Non-Decl nodes show "Not applicable"

### AST Extraction Performance (2026-01-13)

- Reduced per-node overhead; optimized source location conversion

### Language Support (2026-01-10)

- Documented support for C, C++, and Objective-C

### Compiler Compatibility (2026-01-10)

- Works with any compiler (GCC, Clang) if no C++20 modules or PCH

### Long Path Names (2026-01-08)

- Each path component is a separate tree node

---

## Keyboard Shortcuts Reference

| Shortcut         | Action                        |
| ---------------- | ----------------------------- |
| `Tab`            | Cycle focus between panes     |
| `Ctrl+1-6`       | Focus specific pane           |
| `Ctrl+Shift+E/C` | Expand/Collapse all children  |
| `Ctrl+[/]`       | Navigate back/forward         |
| `Ctrl+O`         | Open project                  |
| `Ctrl+Q`         | Quit                          |
| `F5`             | Extract AST for selected file |

---

## Tested Projects

| Project      | Language |  LOC |   TUs | Headers | QueryDependency Time |
| ------------ | :------: | ---: | ----: | ------: | -------------------: |
| linux        |    C     |  36M |   913 |    560K |                 7.6s |
| llvm-project |   C++    |  16M | 4,580 |    2.1M |                32.5s |
| mysql-server |   C++    | 9.5M | 4,887 |    2.7M |                39.8s |
| blender      |   C++    | 3.9M | 4,025 |    2.0M |                42.7s |

---

## Legend

| Status              | Meaning           |
| ------------------- | ----------------- |
| Open                | Not yet addressed |
| Under Investigation | Being researched  |
| Fixed               | Completed         |
| Ongoing             | Continuous effort |
