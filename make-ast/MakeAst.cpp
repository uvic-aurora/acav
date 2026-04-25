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

/// \file MakeAst.cpp
/// \brief Entry point for the make-ast tool.
/// This tool generates and saves AST for a given source file.

#include "common/ClangUtils.h"
#include "common/DiagnosticLogFormat.h"
#include <clang/Frontend/ASTUnit.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <string>

namespace cl = llvm::cl;

namespace {

void emitStructuredMessage(const std::string &level,
                           const std::string &message) {
  llvm::errs() << acav::logfmt::formatDiagnosticLine(level, "", 0, 0, message)
               << "\n";
}

} // namespace

// Define command line options
static llvm::cl::OptionCategory makeAstCategory("make-ast options");

static cl::opt<std::string> CompilationDatabasePath(
    "compilation-database",
    cl::desc("Path to the compilation database in json format"), cl::Required,
    cl::value_desc("/path/to/compile_commands.json"), cl::cat(makeAstCategory));

static cl::opt<std::string> OutputPath("output",
                                       cl::desc("Path to the output AST file"),
                                       cl::value_desc("/path/to/output.ast"),
                                       cl::Required, cl::cat(makeAstCategory));

static cl::opt<std::string>
    SourcePath("source", cl::desc("Path to the source file to process"),
               cl::value_desc("/path/to/source.cpp"), cl::Required,
               cl::cat(makeAstCategory));

static cl::opt<std::string>
    ClangResourceDir("clang-resource-dir",
                     cl::desc("Clang resource dir (lib/clang/<ver>). "
                              "If not provided, checks bundled dir first, "
                              "then falls back to clang++ -print-resource-dir."),
                     cl::value_desc("/path/to/lib/clang/<ver>"),
                     cl::init(""), cl::cat(makeAstCategory));

int main(int argc, char *argv[]) {
  // Parse command line options
  cl::HideUnrelatedOptions(makeAstCategory);
  if (!cl::ParseCommandLineOptions(argc, argv,
                                   "make-ast - Generate and save AST\n")) {
    emitStructuredMessage("error", "Error parsing command line options.");
    return 1;
  }

  // Get command line arguments as strings
  std::string compdbPath = CompilationDatabasePath;
  std::string outputPath = OutputPath;
  std::string sourcePath = SourcePath;

  // Create AST from compilation database
  std::string errorMessage;
  auto diagCallback = [](const acav::DiagnosticMessage &diag) {
    llvm::errs() << acav::logfmt::formatDiagnosticLine(
                        diag.level, diag.file, diag.line, diag.column,
                        diag.message)
                 << "\n";
  };

  std::unique_ptr<clang::ASTUnit> astUnit =
      acav::createAstFromCDB(compdbPath, sourcePath, errorMessage,
                               diagCallback, ClangResourceDir);

  if (!astUnit) {
    emitStructuredMessage("error", "Error: " + errorMessage);
    return 1;
  }

  // Save AST to file
  if (!acav::saveAst(*astUnit, outputPath, errorMessage)) {
    emitStructuredMessage("error", "Error: " + errorMessage);
    return 1;
  }

  llvm::outs()
      << acav::logfmt::formatDiagnosticLine(
             "info", "", 0, 0, "Successfully saved AST to: " + outputPath)
      << "\n";
  return 0;
}
