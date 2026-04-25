# Changelog {#changelog}

All notable changes to public releases of ACAV will be documented in this file.

Pre-public development history is intentionally omitted.

## [v1.0.0] - 2026-04-28

Initial public release.

### Added

- Qt-based `acav` GUI for exploring Clang ASTs in C, C++, Objective-C, and
  Objective-C++ projects that provide a JSON compilation database.
- File explorer view for browsing source files and included headers.
- Source-code view with lightweight C/C++ syntax highlighting.
- AST tree view with source-location synchronization.
- Bidirectional navigation between source code and AST nodes.
- Navigation history with back/forward controls.
- Declaration-context panel for selected AST nodes.
- Source-code search with result navigation and highlighting.
- AST-node search with property-qualified queries, regular-expression
  matching, project-file filtering, result navigation, and query history.
- Region selection in the source-code view for focusing relevant AST nodes.
- Node details dialog for inspecting AST-node properties.
- JSON export for selected AST subtrees.
- Background dependency analysis through `query-dependencies`.
- Background AST generation and caching through `make-ast`.
- Automatic stale AST-cache recovery.
- Compilation-warning banner when generated AST data may be incomplete.
- Configurable cache directory, font family, font size, comment extraction,
  and parallel processing settings.
- Native build support for macOS and Linux with CMake presets.
- Containerized build and demo workflows for Docker and Podman.
- Online manual with user, CLI, installation, source/API, and release
  documentation.
