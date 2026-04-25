/*$!{
* Aurora Clang AST Viewer (ACAV)
* 
* Copyright (c) 2026 Min Liu
* Copyright (c) 2026 Michael David Adams
* 
* SPDX-License-Identifier: GPL-2.0-or-later
* 
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License along
* with this program; if not, see <https://www.gnu.org/licenses/>.
}$!*/

/// \file ClangUtils.h
/// \brief Utilities for interacting with Clang at runtime
/// This includes runtime detection of Clang paths and AST operations
#pragma once

#include <clang/Basic/Diagnostic.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
namespace clang {
class ASTUnit;
class DiagnosticsEngine;
} // namespace clang

namespace acav {

struct DiagnosticMessage {
  clang::DiagnosticsEngine::Level level = clang::DiagnosticsEngine::Note;
  std::string message;
  std::string file;
  unsigned line = 0;
  unsigned column = 0;
};

using DiagnosticCallback = std::function<void(const DiagnosticMessage &)>;

/// \brief Get clang resource directory
///
/// Discovery order:
///   1. Use overrideResourceDir if provided
///   2. Check for bundled directory: ../lib/clang/<version>/ relative to exe
///   3. Fallback: query system clang++ -print-resource-dir
///
/// \param overrideResourceDir Optional explicit path to use
/// \return clang resource directory path if found, empty string on failure
std::string getClangResourceDir(const std::string &overrideResourceDir = "");

/// \brief Normalize a Clang command line for ACAV's embedded Clang.
///
/// This removes stale resource-dir flags, forces ACAV's resource dir, and on
/// macOS adds the active SDK sysroot when the compile command does not already
/// specify one. Standard library, system include, and framework discovery is
/// left to Clang's driver/tooling APIs.
std::vector<std::string>
buildToolchainAdjustedCommandLine(const std::vector<std::string> &commandLine,
                                  const std::string &clangResourceDir,
                                  std::string &diagnostic);

/// \brief Create AST from a given compilation database
/// This function provides a easy way to generate clang AST
/// for a given file and it's compile command. There are several
/// steps:
///   1. Load compilation database and handle response file
///   2. Acquire the compile command for the given file
///   3. Inject the clang resource directory to compile command
///   4. Get clang::ASTUnit using CreateASTUnitFromCommandLine
/// \return unique_ptr of type ASTUnit. nullptr if any error occured
std::unique_ptr<clang::ASTUnit>
createAstFromCDB(const std::string &compilationDatabase,
                 const std::string &sourcePath, std::string &errorMessage,
                 const DiagnosticCallback &diagnosticCallback = nullptr,
                 const std::string &clangResourceDirOverride = "");

/// \brief Save ast to local file
bool saveAst(clang::ASTUnit &astUnit, const std::string &outputPath,
             std::string &errorMessage);

/// \brief Load AST from local file.
/// \param astFilePath Path to the .ast file
/// \param errorMessage Output parameter for error details
/// \param compilationDbPath Path to compile_commands.json for C++20 module
///        resolution. If empty, module resolution may fail for module imports.
/// \param sourcePath Source file path to extract module mappings for.
///        Required if compilationDbPath is provided.
std::unique_ptr<clang::ASTUnit>
loadAstFromFile(const std::string &astFilePath, std::string &errorMessage,
                const std::string &compilationDbPath = "",
                const std::string &sourcePath = "",
                const DiagnosticCallback &diagnosticCallback = nullptr);

/// \brief Extract source file paths from a compilation database
/// \param compDbPath Path to compile_commands.json
/// \param errorMessage Output parameter for error details
/// \return List of source file paths, or empty vector on error
///
/// Thread-safe: Can be called from any thread
std::vector<std::string>
getSourceFilesFromCompilationDatabase(const std::string &compDbPath,
                                      std::string &errorMessage);

} // namespace acav
