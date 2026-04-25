# Overview

\tableofcontents

ACAV (Aurora Clang AST Viewer) is an interactive Abstract Syntax Tree (AST)
visualization tool for C, C++, and Objective-C, built with Clang and Qt. Given
a JSON compilation database such as `compile_commands.json`, ACAV lets you open
a real project, inspect the AST for a translation unit, and move directly
between source code and AST nodes.

![Screenshot of the ACAV interface showing the file explorer, source code view, AST tree view, declaration-context panels, and log panel.](images/acav-screenshot.png)

*Screenshot: ACAV displaying the file explorer, source-code panel, AST tree
view, declaration-context panels, and log panel.*

## Project Links

- [Project codebase](https://github.com/uvic-aurora/acav)
- [Project introduction page](https://uvic-aurora.github.io/acav/)
- [Online manual](https://uvic-aurora.github.io/acav-manual/index.html)

## At a Glance

With a valid compilation database, ACAV lets you:

- inspect the AST for a translation unit in a navigable tree,
- move in both directions between source locations and AST nodes,
- view declaration context while exploring program structure,
- search both source text and AST nodes, and
- reuse dependency and AST-cache artifacts across sessions.

ACAV follows a three-program architecture:

- `acav` is the interactive GUI application.
- `query-dependencies` extracts dependency information from a compilation
  database.
- `make-ast` builds and caches serialized AST files for individual source
  files.

## Purpose and Scope

ACAV addresses the gap between Clang's powerful front-end infrastructure and
the practical difficulty of exploring Clang ASTs interactively. It is designed
for real codebases rather than toy examples: it reads a JSON compilation
database, applies the recorded build settings for each source file, and keeps
the interface responsive through background processing and AST caching.

ACAV is useful for students learning compiler internals, researchers studying
program structure, and developers building or debugging Clang-based tools. Its
current scope is intentionally limited to read-only AST exploration. ACAV does
not modify source code, perform refactoring, or act as a general-purpose
editor, and it displays the AST of one translation unit at a time.

## Quick Start

The typical workflow is:

1. Generate or locate a compilation database for the target project.
2. Build or install ACAV by following [Installation](../../INSTALL.txt).
3. Launch ACAV:

   ```bash
   acav -c /path/to/compile_commands.json
   ```

4. Browse files in the file explorer, then double-click a file or press `F5`
   to generate or load its AST.
5. Use the source view, AST view, and declaration context view to navigate the
   program structure.

## Documentation Organization

- [License](license.md) points to the authoritative license file.
- [Installation](../../INSTALL.txt) covers prerequisites, native builds, and the
  containerized workflow.
- [Docker/Podman Demo Image](../../DOCKER_IMAGE_README.md) provides detailed
  OCI demo image instructions.
- [User Manual](../../ACAV_USER_MANUAL.md) describes the GUI, common
  workflows, the command-line programs, keyboard shortcuts, and configuration.
- [References](references.md) lists related technologies and resources.
- [Classes](annotated.html) provides the generated class reference.
- [Files](files.html) provides the generated file reference.
- [Changelog](../../CHANGELOG.md) provides public release notes for ACAV.
- [Notice](notice.md) provides project attribution, authorship, and licensing context.

## How to Use This Manual

If you are new to ACAV, start with this overview, then
[Installation](../../INSTALL.txt), and then [User Manual](../../ACAV_USER_MANUAL.md).
If you want to inspect the API surface, use [Classes](annotated.html) and
[Files](files.html). If you want to review public release notes, see the
[Changelog](../../CHANGELOG.md).
