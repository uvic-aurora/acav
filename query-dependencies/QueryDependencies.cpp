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

/// \file QueryDependencies.cpp
/// \brief Entry point for the query_dependencies tool.
#include "common/ClangUtils.h"
#include "common/DependencyTypes.h"
#include "common/DiagnosticLogFormat.h"
#include <algorithm>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <fstream>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ct = clang::tooling;
namespace cl = llvm::cl;
using acav::FileDependencies;
using acav::ProcessingError;

namespace {

void emitStructuredMessage(const std::string &level,
                           const std::string &message) {
  llvm::errs() << acav::logfmt::formatDiagnosticLine(level, "", 0, 0, message)
               << "\n";
}

} // namespace

// Define command line options
static llvm::cl::OptionCategory
    queryDependenciesCategory("query-dependencies options");

static cl::opt<std::string> compilationDatabasePath(
    "compilation-database",
    cl::desc("Path to the compilation database in json format"), cl::Required,
    cl::value_desc("/path/to/compile_commands.json"),
    cl::cat(queryDependenciesCategory));

static cl::opt<std::string>
    outputPath("output", cl::desc("Path to the output file .json"),
               cl::value_desc("/path/to/filename.json"), cl::Required,
               cl::cat(queryDependenciesCategory));

static cl::list<std::string> sourceFiles(
    "source",
    cl::desc(
        "Source files to process (can be specified multiple times). "
        "If not specified, all files from compilation database are processed."),
    cl::value_desc("path/to/source.cpp"), cl::ZeroOrMore,
    cl::cat(queryDependenciesCategory));

static cl::opt<std::string> ClangResourceDir(
    "clang-resource-dir",
    cl::desc("Clang resource dir (lib/clang/<ver>). "
             "If not provided, checks bundled dir first, "
             "then falls back to clang++ -print-resource-dir."),
    cl::value_desc("/path/to/lib/clang/<ver>"), cl::init(""),
    cl::cat(queryDependenciesCategory));

/// Captures include directives for a single translation unit.
class DependencyPPCallbacks : public clang::PPCallbacks {
public:
  DependencyPPCallbacks(const clang::SourceManager &sm, FileDependencies &deps)
      : sourceManager_(sm), deps_(deps) {}

  void InclusionDirective(clang::SourceLocation hashLoc,
                          const clang::Token & /*includeTok*/,
                          llvm::StringRef /*fileName*/, bool /*isAngled*/,
                          clang::CharSourceRange /*filenameRange*/,
                          clang::OptionalFileEntryRef file,
                          llvm::StringRef /*searchPath*/,
                          llvm::StringRef /*relativePath*/,
                          const clang::Module * /*suggestedModule*/,
                          bool /*moduleImported*/,
                          clang::SrcMgr::CharacteristicKind fileType) override {
    if (!file)
      return;

    // Get canonical path for the header
    std::string headerPath = file->getFileEntry().tryGetRealPathName().str();
    if (headerPath.empty())
      headerPath = file->getName().str();

    bool isDirect = sourceManager_.isInMainFile(hashLoc);

    // Deduplicate: update existing entry or add new one
    auto it = headerIndex_.find(headerPath);
    if (it != headerIndex_.end()) {
      // Header already seen, update direct flag
      // Some headers might be included for more than once,
      // it should be marked as direct as true if
      // it is included as direct once.
      deps_.headers[it->second].direct_ |= isDirect;
    } else {
      // New header, add to list
      acav::HeaderInfo info;
      info.path_ = headerPath;
      info.direct_ = isDirect;
      info.inclusionKind_ = includeKindToString(fileType);

      size_t index = deps_.headers.size();
      deps_.headers.push_back(std::move(info));
      headerIndex_[headerPath] = index;
    }
  }

private:
  const clang::SourceManager &sourceManager_;
  acav::FileDependencies &deps_;
  std::unordered_map<std::string, size_t> headerIndex_;

  static std::string
  includeKindToString(clang::SrcMgr::CharacteristicKind kind) {
    switch (kind) {
    case clang::SrcMgr::C_User:
      return "C_User";
    case clang::SrcMgr::C_System:
      return "C_System";
    case clang::SrcMgr::C_ExternCSystem:
      return "C_ExternCSystem";
    case clang::SrcMgr::C_User_ModuleMap:
      return "C_User_ModuleMap";
    case clang::SrcMgr::C_System_ModuleMap:
      return "C_System_ModuleMap";
    }
    return "Unknown";
  }
};

/// \brief Captures error messages from Clang diagnostics
class ErrorCapturingDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  ErrorCapturingDiagnosticConsumer(std::optional<ProcessingError> &error,
                                   const std::string &currentFilePath)
      : error_(error), currentFilePath_(currentFilePath) {}

  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &info) override {
    llvm::SmallString<256> message;
    info.FormatDiagnostic(message);
    std::string msg = message.str().str();

    std::string file = currentFilePath_;
    unsigned line = 0;
    unsigned column = 0;
    clang::SourceLocation loc = info.getLocation();
    if (loc.isValid()) {
      const clang::SourceManager &sm = info.getSourceManager();
      clang::FullSourceLoc fullLoc(loc, sm);
      if (fullLoc.isValid()) {
        const llvm::StringRef filename =
            sm.getFilename(fullLoc.getSpellingLoc());
        if (!filename.empty()) {
          file = filename.str();
        }
        line = fullLoc.getSpellingLineNumber();
        column = fullLoc.getSpellingColumnNumber();
      }
    }

    llvm::errs() << acav::logfmt::formatDiagnosticLine(level, file, line,
                                                       column, msg)
                 << "\n";

    // Capture first error message
    if (level >= clang::DiagnosticsEngine::Error && !error_.has_value()) {
      error_ = ProcessingError{currentFilePath_, message.str().str()};
    }
  }

private:
  std::optional<ProcessingError> &error_;
  const std::string &currentFilePath_;
};

/// \brief Emits structured diagnostics for driver-level warnings/errors.
class StructuredDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &info) override {
    llvm::SmallString<256> message;
    info.FormatDiagnostic(message);

    std::string file;
    unsigned line = 0;
    unsigned column = 0;
    clang::SourceLocation loc = info.getLocation();
    if (loc.isValid()) {
      const clang::SourceManager &sm = info.getSourceManager();
      clang::FullSourceLoc fullLoc(loc, sm);
      if (fullLoc.isValid()) {
        const llvm::StringRef filename =
            sm.getFilename(fullLoc.getSpellingLoc());
        if (!filename.empty()) {
          file = filename.str();
        }
        line = fullLoc.getSpellingLineNumber();
        column = fullLoc.getSpellingColumnNumber();
      }
    }

    llvm::errs() << acav::logfmt::formatDiagnosticLine(
                        level, file, line, column, message.str().str())
                 << "\n";
  }
};

/// FrontendAction to set up PPCallbacks for each translation unit.
class DependencyFrontendAction : public clang::PreprocessOnlyAction {
public:
  DependencyFrontendAction(std::vector<FileDependencies> &allResults,
                           std::vector<ProcessingError> &errors)
      : allResults_(allResults), errors_(errors) {}

  bool BeginSourceFileAction(clang::CompilerInstance &ci) override {
    currentDeps_ = FileDependencies{};
    currentError_ = std::nullopt;

    currentDeps_.path = getMainFilePath(ci);

    ci.getPreprocessor().addPPCallbacks(std::make_unique<DependencyPPCallbacks>(
        ci.getSourceManager(), currentDeps_));

    // To capture error messages while processing this file
    ci.getDiagnostics().setClient(
        new ErrorCapturingDiagnosticConsumer(currentError_, currentDeps_.path),
        /*ShouldOwnClient=*/true);

    return true;
  }

  void EndSourceFileAction() override {
    // Add to shared results (even if partial)
    allResults_.push_back(std::move(currentDeps_));

    // Save error if occurred
    if (currentError_.has_value()) {
      if (currentError_->filePath_.empty()) {
        currentError_->filePath_ = currentDeps_.path;
      }

      errors_.push_back(std::move(currentError_.value()));
    }
  }

private:
  // Get the main file path from CompilerInstance
  static std::string getMainFilePath(const clang::CompilerInstance &ci) {
    const clang::SourceManager &sm = ci.getSourceManager();
    clang::FileID mainFileID = sm.getMainFileID();
    auto fileEntry = sm.getFileEntryRefForID(mainFileID);
    if (!fileEntry)
      return "";
    // Use tryGetRealPathName() to get canonical absolute path
    // This handles files outside the project directory correctly
    std::string realPath = fileEntry->getFileEntry().tryGetRealPathName().str();
    if (!realPath.empty())
      return realPath;
    // Fall back to getName() if real path is not available
    return std::string(fileEntry->getName());
  }

  std::vector<FileDependencies> &allResults_;
  std::vector<ProcessingError> &errors_;
  FileDependencies currentDeps_;
  std::optional<ProcessingError> currentError_;
};

/// Factory to create FrontendActions for each translation unit.
class DependencyFrontendFactory : public clang::tooling::FrontendActionFactory {
public:
  DependencyFrontendFactory(std::vector<FileDependencies> &allResults,
                            std::vector<ProcessingError> &errors)
      : allResults_(allResults), errors_(errors) {}

  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<DependencyFrontendAction>(allResults_, errors_);
  }

private:
  std::vector<FileDependencies> &allResults_;
  std::vector<ProcessingError> &errors_;
};

/// \brief Build JSON output with error section
nlohmann::json buildJsonOutput(const std::vector<FileDependencies> &results,
                               const std::vector<ProcessingError> &errors,
                               size_t totalFiles) {
  size_t totalHeaderCount = 0;
  nlohmann::json filesJson = nlohmann::json::array();

  // Build file entries
  std::unordered_set<std::string> filesWithErrors;
  for (const auto &err : errors) {
    filesWithErrors.insert(err.filePath_);
  }

  for (const auto &fileDeps : results) {
    nlohmann::json headersJson = nlohmann::json::array();
    for (const auto &header : fileDeps.headers) {
      headersJson.push_back({{"path", header.path_},
                             {"direct", header.direct_},
                             {"inclusionKind", header.inclusionKind_}});
    }

    nlohmann::json fileEntry = {{"path", fileDeps.path},
                                {"headerCount", fileDeps.headers.size()},
                                {"headers", headersJson}};

    // Mark partial data if file had errors
    bool hasError = filesWithErrors.count(fileDeps.path) > 0;
    if (hasError) {
      fileEntry["partialData"] = true;
      // Find and include error message
      auto it = std::find_if(errors.begin(), errors.end(),
                             [&](const ProcessingError &e) {
                               return e.filePath_ == fileDeps.path;
                             });
      if (it != errors.end()) {
        fileEntry["error"] = it->errorMessage_;
      }
    }

    filesJson.push_back(fileEntry);
    totalHeaderCount += fileDeps.headers.size();
  }

  // Build error section
  nlohmann::json errorsJson = nlohmann::json::array();
  for (const auto &err : errors) {
    errorsJson.push_back(
        {{"path", err.filePath_}, {"message", err.errorMessage_}});
  }

  // Build final output
  nlohmann::json outputJson;
  outputJson["statistics"] = {{"totalCount", totalFiles},
                              {"successCount", results.size()},
                              {"failureCount", errors.size()},
                              {"totalHeaderCount", totalHeaderCount}};
  outputJson["files"] = filesJson;

  // Only include errors section if errors exist
  if (!errors.empty()) {
    outputJson["errors"] = errorsJson;
  }

  return outputJson;
}

int main(int argc, char *argv[]) {
  cl::HideUnrelatedOptions(queryDependenciesCategory);
  if (!cl::ParseCommandLineOptions(argc, argv, "Query Dependencies Tool\n")) {
    emitStructuredMessage("error", "Error parsing command line options.");
    return 1;
  }

  // Load compilation database
  std::string errorMessage;
  std::unique_ptr<ct::CompilationDatabase> compilationDatabase =
      ct::JSONCompilationDatabase::loadFromFile(
          compilationDatabasePath, errorMessage,
          ct::JSONCommandLineSyntax::AutoDetect);
  if (!compilationDatabase) {
    emitStructuredMessage("error", "Error loading compilation database: " +
                                       errorMessage);
    return 1;
  }

  // Let Clang tooling recover driver path/mode information before expanding
  // response files. The driver simulation then owns implicit include discovery.
  compilationDatabase = ct::inferToolLocation(std::move(compilationDatabase));
  compilationDatabase =
      ct::inferTargetAndDriverMode(std::move(compilationDatabase));

  // Expand response files (@file arguments) in compilation commands
  // This is necessary for C++20 modules where CMake uses .modmap files
  compilationDatabase = ct::expandResponseFiles(std::move(compilationDatabase),
                                                llvm::vfs::getRealFileSystem());

  std::vector<std::string> allSourceFiles = compilationDatabase->getAllFiles();
  if (allSourceFiles.empty()) {
    emitStructuredMessage("error", "No source files in compilation database.");
    return 1;
  }

  // Filter source files if --source arguments provided
  std::vector<std::string> filesToProcess;
  if (!sourceFiles.empty()) {
    // User specified specific files, use only those
    // Create a set for O(1) lookup
    std::unordered_set<std::string> requestedFiles(sourceFiles.begin(),
                                                   sourceFiles.end());

    // Filter to only files that exist in compilation database
    for (const auto &file : allSourceFiles) {
      if (requestedFiles.count(file) > 0) {
        filesToProcess.push_back(file);
      }
    }

    // Warn if some requested files not found
    if (filesToProcess.size() < sourceFiles.size()) {
      emitStructuredMessage(
          "warning",
          "Warning: " +
              std::to_string(sourceFiles.size() - filesToProcess.size()) +
              " requested files not found in compilation database");
    }
  } else {
    // No --source arguments, use all files (backward compatible)
    filesToProcess = std::move(allSourceFiles);
  }

  if (filesToProcess.empty()) {
    emitStructuredMessage("error", "No source files to process.");
    return 1;
  }

  // Shared results and errors across all translation units
  std::vector<FileDependencies> allResults;
  std::vector<ProcessingError> errors;

  // Run tool on filtered files
  ct::ClangTool tool(*compilationDatabase, filesToProcess);
  StructuredDiagnosticConsumer structuredDiagnostics;
  tool.setDiagnosticConsumer(&structuredDiagnostics);

  // Get Clang resource directory at runtime
  // This is necessary for libclang to find built-in headers (stdarg.h, etc.)
  std::string clangResourceDir = acav::getClangResourceDir(ClangResourceDir);
  if (clangResourceDir.empty()) {
    emitStructuredMessage("error",
                          "Error: Failed to get Clang resource directory.");
    return 1;
  }

  tool.appendArgumentsAdjuster(
      [clangResourceDir](const ct::CommandLineArguments &args,
                         llvm::StringRef filename) {
        std::string toolchainDiagnostic;
        ct::CommandLineArguments adjusted =
            acav::buildToolchainAdjustedCommandLine(args, clangResourceDir,
                                                    toolchainDiagnostic);
        if (!toolchainDiagnostic.empty()) {
          emitStructuredMessage("warning", "[toolchain] " + filename.str() +
                                               ": " + toolchainDiagnostic);
        }
        return adjusted;
      });

  DependencyFrontendFactory factory(allResults, errors);

  // Run tool - ignore return value, we handle errors ourselves
  tool.run(&factory);

  // Log progress to stderr
  emitStructuredMessage(
      "info", "Processed " + std::to_string(filesToProcess.size()) +
                  " files: " + std::to_string(allResults.size()) +
                  " succeeded, " + std::to_string(errors.size()) + " failed");

  // Sort results by source file path for consistent output
  std::sort(allResults.begin(), allResults.end(),
            [](const FileDependencies &a, const FileDependencies &b) {
              return a.path < b.path;
            });

  // Build JSON with error section
  nlohmann::json outputJson =
      buildJsonOutput(allResults, errors, filesToProcess.size());

  // Write to output file
  std::ofstream ofs(outputPath);
  if (!ofs) {
    emitStructuredMessage("error", "Error opening output file: " + outputPath);
    return 1;
  }
  ofs << outputJson.dump(2) << "\n";

  // Always return 0 - errors are in JSON and stderr
  return 0;
}
