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

/// \file ClangUtils.cpp
/// \brief Implementation of Clang utilities and AST operations

#include "common/ClangUtils.h"
#include "common/DiagnosticLogFormat.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <cstdlib>
#if LLVM_VERSION_MAJOR >= 22
#include <clang/Driver/CreateASTUnitFromArgs.h>
#endif
#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/PCHContainerOperations.h>
#include <clang/Lex/HeaderSearchOptions.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/ScopeExit.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <optional>
#include <utility>

namespace ct = clang::tooling;

namespace acav {

namespace {

void emitStructuredMessage(const std::string &level,
                           const std::string &message) {
  llvm::errs() << acav::logfmt::formatDiagnosticLine(level, "", 0, 0, message)
               << "\n";
}

/// \brief Get the directory containing the current executable
/// \return Path to executable directory, or empty string on failure
std::string getExecutableDir() {
  // Use LLVM's getMainExecutable with a dummy function address
  // This works on all platforms (Linux, macOS, Windows)
  std::string execPath = llvm::sys::fs::getMainExecutable(
      "acav", reinterpret_cast<void *>(&getExecutableDir));

  if (execPath.empty()) {
    return "";
  }

  llvm::SmallString<256> execDir(execPath);
  llvm::sys::path::remove_filename(execDir);
  return std::string(execDir);
}

/// \brief Find bundled clang resource directory relative to executable
/// Checks for ../lib/clang/<LLVM_VERSION_MAJOR>/ relative to executable
/// \return Path to bundled resource dir if found and valid, empty otherwise
std::optional<std::string> findBundledResourceDir() {
  std::string execDir = getExecutableDir();
  if (execDir.empty()) {
    return std::nullopt;
  }

  // Build path: <exec_dir>/../lib/clang/<version>/
  llvm::SmallString<256> resourcePath(execDir);
  llvm::sys::path::append(resourcePath, "..", "lib", "clang",
                          std::to_string(LLVM_VERSION_MAJOR));

  // Normalize the path (resolve .. components)
  llvm::SmallString<256> normalizedPath;
  if (std::error_code ec =
          llvm::sys::fs::real_path(resourcePath, normalizedPath)) {
    // Path doesn't exist or can't be resolved
    return std::nullopt;
  }

  return std::string(normalizedPath);
}

bool isValidClangResourceDir(llvm::StringRef resourceDir) {
  if (resourceDir.empty() || !llvm::sys::fs::exists(resourceDir)) {
    return false;
  }

  llvm::SmallString<256> includeDir(resourceDir);
  llvm::sys::path::append(includeDir, "include");
  if (!llvm::sys::fs::exists(includeDir)) {
    return false;
  }

  llvm::SmallString<256> stddefPath(includeDir);
  llvm::sys::path::append(stddefPath, "stddef.h");
  return llvm::sys::fs::exists(stddefPath);
}

std::optional<std::string> readFileToString(const std::string &path) {
  auto bufferOrErr = llvm::MemoryBuffer::getFile(path);
  if (!bufferOrErr) {
    return std::nullopt;
  }
  return (*bufferOrErr)->getBuffer().str();
}

std::optional<std::string>
runProgramCaptureStdout(llvm::StringRef program,
                        llvm::ArrayRef<llvm::StringRef> args) {
  llvm::SmallString<256> stdoutPath;
  if (llvm::sys::fs::createTemporaryFile("acav", "stdout", stdoutPath)) {
    return std::nullopt;
  }

  std::string errMsg;
  bool executionFailed = false;
  std::array<std::optional<llvm::StringRef>, 3> redirects = {
      std::nullopt, llvm::StringRef(stdoutPath),
      llvm::StringRef("") // portable /dev/null for stderr
  };

  int rc = llvm::sys::ExecuteAndWait(program, args, std::nullopt, redirects, 0,
                                     0, &errMsg, &executionFailed);
#if LLVM_VERSION_MAJOR >= 22
  llvm::scope_exit cleanup([&]() { (void)llvm::sys::fs::remove(stdoutPath); });
#else
  auto cleanup =
      llvm::make_scope_exit([&]() { (void)llvm::sys::fs::remove(stdoutPath); });
#endif

  if (executionFailed || rc != 0) {
    return std::nullopt;
  }

  auto output = readFileToString(stdoutPath.str().str());
  if (!output) {
    return std::nullopt;
  }
  return *output;
}

std::optional<std::string> runClangWithArg(llvm::StringRef programPath,
                                           llvm::StringRef arg) {
  llvm::StringRef progName = llvm::sys::path::filename(programPath);
  std::array<llvm::StringRef, 2> args = {progName, arg};
  return runProgramCaptureStdout(programPath, args);
}

int parseClangMajorVersion(const std::string &versionOutput) {
  // Typical outputs include:
  //   "clang version 21.1.8 ..."
  //   "Apple clang version 15.0.0 ..."
  // We parse the first integer following the token "version".
  constexpr llvm::StringLiteral versionToken("version");
  std::size_t pos = versionOutput.find(versionToken.data());
  if (pos == std::string::npos) {
    return -1;
  }
  pos += versionToken.size();
  while (pos < versionOutput.size() &&
         (versionOutput[pos] == ' ' || versionOutput[pos] == '\t')) {
    ++pos;
  }
  std::size_t start = pos;
  while (pos < versionOutput.size() && std::isdigit(versionOutput[pos])) {
    ++pos;
  }
  if (start == pos) {
    return -1;
  }
  return std::atoi(versionOutput.substr(start, pos - start).c_str());
}

std::optional<std::string>
getClangResourceDirFromProgram(llvm::StringRef programPath) {
  auto output = runClangWithArg(programPath, "-print-resource-dir");
  if (!output) {
    return std::nullopt;
  }
  std::string resourceDir =
      llvm::StringRef(*output).split('\n').first.trim().str();
  if (!isValidClangResourceDir(resourceDir)) {
    return std::nullopt;
  }
  return resourceDir;
}

std::optional<int> getClangProgramMajorVersion(llvm::StringRef programPath) {
  auto output = runClangWithArg(programPath, "--version");
  if (!output) {
    return std::nullopt;
  }
  int major = parseClangMajorVersion(*output);
  if (major <= 0) {
    return std::nullopt;
  }
  return major;
}

std::vector<std::string>
stripResourceDirArgs(const std::vector<std::string> &commandLine) {
  std::vector<std::string> stripped;
  stripped.reserve(commandLine.size());

  for (std::size_t i = 0; i < commandLine.size(); ++i) {
    const std::string &arg = commandLine[i];
    if (arg == "-resource-dir") {
      if (i + 1 < commandLine.size()) {
        ++i;
      }
      continue;
    }
    if (llvm::StringRef(arg).starts_with("-resource-dir=")) {
      continue;
    }
    stripped.push_back(arg);
  }

  return stripped;
}

class CallbackDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  explicit CallbackDiagnosticConsumer(DiagnosticCallback callback)
      : callback_(std::move(callback)) {}

  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &info) override {
    if (!callback_) {
      return;
    }

    llvm::SmallString<256> message;
    info.FormatDiagnostic(message);

    DiagnosticMessage diag;
    diag.level = level;
    diag.message = message.str().str();

    clang::SourceLocation loc = info.getLocation();
    if (loc.isValid()) {
      const clang::SourceManager &sm = info.getSourceManager();
      clang::FullSourceLoc fullLoc(loc, sm);
      if (fullLoc.isValid()) {
        const llvm::StringRef filename =
            sm.getFilename(fullLoc.getSpellingLoc());
        if (!filename.empty()) {
          diag.file = filename.str();
        }
        diag.line = fullLoc.getSpellingLineNumber();
        diag.column = fullLoc.getSpellingColumnNumber();
      }
    }

    callback_(diag);
  }

private:
  DiagnosticCallback callback_;
};

// Create diagnostic consumer with callback or fallback to structured logging
clang::DiagnosticConsumer *
createDiagnosticConsumer(const DiagnosticCallback &callback) {
  if (callback) {
    return new CallbackDiagnosticConsumer(callback);
  }
  auto structuredFallback = [](const DiagnosticMessage &diag) {
    llvm::errs() << acav::logfmt::formatDiagnosticLine(diag.level, diag.file,
                                                       diag.line, diag.column,
                                                       diag.message)
                 << "\n";
  };
  return new CallbackDiagnosticConsumer(structuredFallback);
}

} // namespace

// Forward declaration for internal helper function
static std::map<std::string, std::string>
extractModuleFileMappings(const std::string &compilationDb,
                          const std::string &sourcePath);

std::string getClangResourceDir(const std::string &overrideResourceDir) {
  // 1. Check explicit override first
  if (!overrideResourceDir.empty()) {
    if (!isValidClangResourceDir(overrideResourceDir)) {
      emitStructuredMessage("error", "[clang] Invalid override resource dir: " +
                                         overrideResourceDir);
      return "";
    }
    emitStructuredMessage("info", "[clang] Using override resource dir: " +
                                      overrideResourceDir);
    return overrideResourceDir;
  }

  // 2. Check for bundled resource directory (../lib/clang/<version>/)
  // This is the preferred method for release builds where dependencies
  // are bundled with the executable
  if (auto bundledDir = findBundledResourceDir()) {
    if (isValidClangResourceDir(*bundledDir)) {
      emitStructuredMessage("info", "[clang] Using bundled resource dir: " +
                                        *bundledDir);
      return *bundledDir;
    }
    emitStructuredMessage("debug",
                          "[clang] Bundled resource dir found but invalid: " +
                              *bundledDir);
  }

  // 3. Fallback: query clang++ -print-resource-dir
  // This method requires a compatible clang++ to be installed on the system
  constexpr int requiredMajor = LLVM_VERSION_MAJOR;

  auto tryProgram =
      [&](llvm::StringRef programPath) -> std::optional<std::string> {
    auto major = getClangProgramMajorVersion(programPath);
    if (!major || *major != requiredMajor) {
      emitStructuredMessage(
          "warning",
          "[clang] Skipping clang binary (major mismatch): " +
              programPath.str() + " (found " +
              (major ? std::to_string(*major) : std::string("unknown")) +
              ", need " + std::to_string(requiredMajor) + ")");
      return std::nullopt;
    }
    return getClangResourceDirFromProgram(programPath);
  };

  const std::vector<std::string> candidateNames = {
      "clang++-" + std::to_string(requiredMajor),
      "clang-" + std::to_string(requiredMajor) + "++",
      "clang++",
  };

  for (const std::string &name : candidateNames) {
    auto programOrErr = llvm::sys::findProgramByName(name);
    if (!programOrErr) {
      continue;
    }
    if (auto dir = tryProgram(*programOrErr)) {
      emitStructuredMessage("info",
                            "[clang] Resource dir (via clang++): " + *dir);
      return *dir;
    }
  }

  emitStructuredMessage(
      "error", "[clang] Failed to locate clang resource dir. Checked:\n"
               "  1. Bundled: ../lib/clang/" +
                   std::to_string(requiredMajor) +
                   "/ (not found or invalid)\n"
                   "  2. System clang++ -print-resource-dir (not found or "
                   "version mismatch)");
  return "";
}

std::vector<std::string>
buildToolchainAdjustedCommandLine(const std::vector<std::string> &commandLine,
                                  const std::string &clangResourceDir,
                                  std::string &diagnostic) {
  diagnostic.clear();
  std::vector<std::string> adjusted = stripResourceDirArgs(commandLine);

#ifdef __APPLE__
  bool hasSysroot = false;
  bool useMacOSSDK = true;
  for (std::size_t i = 0; i < adjusted.size(); ++i) {
    const std::string &arg = adjusted[i];
    const llvm::StringRef argRef(arg);

    hasSysroot |= argRef.starts_with("-isysroot") || arg == "--sysroot" ||
                  argRef.starts_with("--sysroot=");

    llvm::StringRef target;
    if ((arg == "-target" || arg == "--target") && i + 1 < adjusted.size()) {
      target = adjusted[i + 1];
    } else if (argRef.starts_with("--target=")) {
      target = argRef.drop_front(llvm::StringRef("--target=").size());
    } else if (argRef.starts_with("-target=")) {
      target = argRef.drop_front(llvm::StringRef("-target=").size());
    }

    if (!target.empty()) {
      const std::string lowerTarget = target.lower();
      const llvm::StringRef lowerTargetRef(lowerTarget);
      useMacOSSDK = lowerTargetRef.contains("-apple-macos") ||
                    lowerTargetRef.contains("-apple-darwin");
    }
  }

  if (!hasSysroot && useMacOSSDK) {
    auto xcrunOrErr = llvm::sys::findProgramByName("xcrun");
    const std::string xcrun = xcrunOrErr ? *xcrunOrErr : "/usr/bin/xcrun";
    if (!llvm::sys::fs::exists(xcrun)) {
      diagnostic = "unable to find xcrun to discover the active macOS SDK";
    } else {
      std::array<llvm::StringRef, 4> args = {"xcrun", "--sdk", "macosx",
                                             "--show-sdk-path"};
      auto output = runProgramCaptureStdout(xcrun, args);
      if (!output) {
        diagnostic = "xcrun failed while discovering the active macOS SDK";
      } else {
        std::string sdkPath =
            llvm::StringRef(*output).split('\n').first.trim().str();
        if (sdkPath.empty() || !llvm::sys::fs::is_directory(sdkPath)) {
          diagnostic = "xcrun returned an invalid macOS SDK path: " + sdkPath;
        } else {
          adjusted.push_back("-isysroot");
          adjusted.push_back(sdkPath);
        }
      }
    }
  }
#endif

  if (!clangResourceDir.empty()) {
    adjusted.push_back("-resource-dir");
    adjusted.push_back(clangResourceDir);
  }

  return adjusted;
}

std::unique_ptr<clang::ASTUnit>
createAstFromCDB(const std::string &compilationDatabase,
                 const std::string &sourcePath, std::string &errorMessage,
                 const DiagnosticCallback &diagnosticCallback,
                 const std::string &clangResourceDirOverride) {
  // Load compilation database
  std::string loadError;
  std::unique_ptr<ct::CompilationDatabase> compdb =
      ct::JSONCompilationDatabase::loadFromFile(
          compilationDatabase, loadError,
          clang::tooling::JSONCommandLineSyntax::AutoDetect);

  if (!compdb) {
    errorMessage = "Failed to load compilation database: " + loadError;
    return nullptr;
  }

  // Let Clang tooling recover driver path/mode information before expanding
  // response files. The driver simulation then owns implicit include discovery.
  compdb = ct::inferToolLocation(std::move(compdb));
  compdb = ct::inferTargetAndDriverMode(std::move(compdb));

  // Expand response file
  compdb = ct::expandResponseFiles(std::move(compdb),
                                   llvm::vfs::getRealFileSystem());

  // Get compile commands for the source file
  std::vector<clang::tooling::CompileCommand> commands =
      compdb->getCompileCommands(sourcePath);

  if (commands.empty()) {
    errorMessage = "No compile command found for source file: " + sourcePath;
    return nullptr;
  }

  // Get the resource dir
  std::string resourceDir = getClangResourceDir(clangResourceDirOverride);
  if (resourceDir.empty()) {
    errorMessage = "Get clang resource dir failed";
    return nullptr;
  }

  // Use the first command
  const clang::tooling::CompileCommand &cmd = commands[0];

  // Setup VFS with working directory from compilation database
  // This is critical for C++20 modules: -fmodule-file=name=path uses paths
  // relative to the working directory, which must match the compilation
  // database's "directory" field
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> vfs =
      llvm::vfs::getRealFileSystem();
  if (std::error_code ec = vfs->setCurrentWorkingDirectory(cmd.Directory)) {
    errorMessage = "Failed to set working directory to '" + cmd.Directory +
                   "': " + ec.message();
    return nullptr;
  }

  // Setup diagnostics engine to capture compilation errors
#if LLVM_VERSION_MAJOR >= 21
  auto diagOpts = std::make_shared<clang::DiagnosticOptions>();
  clang::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diags(
      new clang::DiagnosticsEngine(
          clang::IntrusiveRefCntPtr<clang::DiagnosticIDs>(
              new clang::DiagnosticIDs()),
          *diagOpts, createDiagnosticConsumer(diagnosticCallback)));
#else
  clang::IntrusiveRefCntPtr<clang::DiagnosticOptions> diagOpts(
      new clang::DiagnosticOptions());
  clang::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diags(
      new clang::DiagnosticsEngine(
          clang::IntrusiveRefCntPtr<clang::DiagnosticIDs>(
              new clang::DiagnosticIDs()),
          diagOpts, createDiagnosticConsumer(diagnosticCallback)));
#endif

  std::string toolchainDiagnostic;
  std::vector<std::string> adjustedCommandLine =
      buildToolchainAdjustedCommandLine(cmd.CommandLine, resourceDir,
                                        toolchainDiagnostic);
  if (!toolchainDiagnostic.empty()) {
    emitStructuredMessage("warning", "[toolchain] " + toolchainDiagnostic);
  }

  // Prepare command line arguments for Clang
  std::vector<const char *> args;
  for (const auto &arg : adjustedCommandLine) {
    args.push_back(arg.c_str());
  }
  if (std::find(adjustedCommandLine.begin(), adjustedCommandLine.end(),
                "-fparse-all-comments") == adjustedCommandLine.end()) {
    args.push_back("-fparse-all-comments");
  }

  // Log the fully expanded command for debugging (includes response expansion).
  std::string expandedCommand = "[make-ast] Expanded command:";
  for (const auto *arg : args) {
    expandedCommand += " ";
    expandedCommand += arg;
  }
  emitStructuredMessage("debug", expandedCommand);

  // Create PCH container operations
  auto pchOps = std::make_shared<clang::PCHContainerOperations>();

  // Build AST from command line arguments
  // Pass VFS with working directory set to ensure module file paths resolve
  // correctly
#if LLVM_VERSION_MAJOR >= 22
  std::unique_ptr<clang::ASTUnit> astUnit = clang::CreateASTUnitFromCommandLine(
      args.data(), args.data() + args.size(), pchOps, diagOpts, diags,
      resourceDir,
      /*StorePreamblesInMemory=*/false,
      /*PreambleStoragePath=*/llvm::StringRef(),
      /*OnlyLocalDecls=*/false, clang::CaptureDiagsKind::None,
      /*RemappedFiles=*/{},
      /*RemappedFilesKeepOriginalName=*/true,
      /*PrecompilePreambleAfterNParses=*/0, clang::TU_Complete,
      /*CacheCodeCompletionResults=*/false,
      /*IncludeBriefCommentsInCodeCompletion=*/false,
      /*AllowPCHWithCompilerErrors=*/false,
      clang::SkipFunctionBodiesScope::None,
      /*SingleFileParse=*/false,
      /*UserFilesAreVolatile=*/false,
      /*ForSerialization=*/true, // Important: we may call Save()
      /*RetainExcludedConditionalBlocks=*/false,
      /*ModuleFormat=*/std::nullopt,
      /*ErrAST=*/nullptr,
      /*VFS=*/vfs);
#elif LLVM_VERSION_MAJOR >= 21
  // LLVM 21: ASTUnit::LoadFromCommandLine with diagOpts as param 4
  std::unique_ptr<clang::ASTUnit> astUnit = clang::ASTUnit::LoadFromCommandLine(
      args.data(), args.data() + args.size(), pchOps, diagOpts, diags,
      resourceDir,
      /*StorePreamblesInMemory=*/false,
      /*PreambleStoragePath=*/llvm::StringRef(),
      /*OnlyLocalDecls=*/false, clang::CaptureDiagsKind::None,
      /*RemappedFiles=*/{},
      /*RemappedFilesKeepOriginalName=*/true,
      /*PrecompilePreambleAfterNParses=*/0, clang::TU_Complete,
      /*CacheCodeCompletionResults=*/false,
      /*IncludeBriefCommentsInCodeCompletion=*/false,
      /*AllowPCHWithCompilerErrors=*/false,
      clang::SkipFunctionBodiesScope::None,
      /*SingleFileParse=*/false,
      /*UserFilesAreVolatile=*/false,
      /*ForSerialization=*/true, // Important: we may call Save()
      /*RetainExcludedConditionalBlocks=*/false,
      /*ModuleFormat=*/std::nullopt,
      /*ErrAST=*/nullptr,
      /*VFS=*/vfs);
#else
  // LLVM 20: ASTUnit::LoadFromCommandLine without separate diagOpts
  std::unique_ptr<clang::ASTUnit> astUnit = clang::ASTUnit::LoadFromCommandLine(
      args.data(), args.data() + args.size(), pchOps, diags, resourceDir,
      /*StorePreamblesInMemory=*/false,
      /*PreambleStoragePath=*/llvm::StringRef(),
      /*OnlyLocalDecls=*/false, clang::CaptureDiagsKind::None,
      /*RemappedFiles=*/{},
      /*RemappedFilesKeepOriginalName=*/true,
      /*PrecompilePreambleAfterNParses=*/0, clang::TU_Complete,
      /*CacheCodeCompletionResults=*/false,
      /*IncludeBriefCommentsInCodeCompletion=*/false,
      /*AllowPCHWithCompilerErrors=*/false,
      clang::SkipFunctionBodiesScope::None,
      /*SingleFileParse=*/false,
      /*UserFilesAreVolatile=*/false,
      /*ForSerialization=*/true, // Important: we may call Save()
      /*RetainExcludedConditionalBlocks=*/false,
      /*ModuleFormat=*/std::nullopt,
      /*ErrAST=*/nullptr,
      /*VFS=*/vfs);
#endif

  if (!astUnit) {
    errorMessage = "Failed to create AST for source file: " + sourcePath;
    return nullptr;
  }

  return std::move(astUnit);
}

bool saveAst(clang::ASTUnit &astUnit, const std::string &outputPath,
             std::string &errorMessage) {
  // Note: Save() returns true on error, false on success
  if (astUnit.Save(outputPath)) {
    errorMessage = "Failed to save AST to: " + outputPath;
    return false;
  }
  return true;
}

std::unique_ptr<clang::ASTUnit>
loadAstFromFile(const std::string &astFilePath, std::string &errorMessage,
                const std::string &compilationDbPath,
                const std::string &sourcePath,
                const DiagnosticCallback &diagnosticCallback) {
  // Debug output
  emitStructuredMessage("debug", "[loadAstFromFile] AST file: " + astFilePath);
  emitStructuredMessage(
      "debug", "[loadAstFromFile] Compilation DB: " +
                   (compilationDbPath.empty() ? "(none)" : compilationDbPath));

  // Extract module file mappings from compilation database (for C++20 modules)
  std::map<std::string, std::string> moduleFileMappings;
  std::string workingDir;

  if (!compilationDbPath.empty() && !sourcePath.empty()) {
    moduleFileMappings =
        extractModuleFileMappings(compilationDbPath, sourcePath);

    // Get working directory from compilation database path
    llvm::SmallString<256> compDbDir(compilationDbPath);
    llvm::sys::path::remove_filename(compDbDir);
    workingDir = std::string(compDbDir);
  }

  emitStructuredMessage("debug",
                        "[loadAstFromFile] Working dir: " +
                            (workingDir.empty() ? "(empty)" : workingDir));
  emitStructuredMessage("debug", "[loadAstFromFile] Module mappings: " +
                                     std::to_string(moduleFileMappings.size()) +
                                     " entries");

  // Setup diagnostics - these don't need to match Save() configuration
#if LLVM_VERSION_MAJOR >= 21
  auto diagOpts = std::make_shared<clang::DiagnosticOptions>();
  clang::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diags(
      new clang::DiagnosticsEngine(
          clang::IntrusiveRefCntPtr<clang::DiagnosticIDs>(
              new clang::DiagnosticIDs()),
          *diagOpts, createDiagnosticConsumer(diagnosticCallback)));
#else
  clang::IntrusiveRefCntPtr<clang::DiagnosticOptions> diagOpts(
      new clang::DiagnosticOptions());
  clang::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diags(
      new clang::DiagnosticsEngine(
          clang::IntrusiveRefCntPtr<clang::DiagnosticIDs>(
              new clang::DiagnosticIDs()),
          diagOpts, createDiagnosticConsumer(diagnosticCallback)));
#endif

  // Create PCH container reader
  auto pchContainerOps = std::make_shared<clang::PCHContainerOperations>();

  // Setup VFS with working directory if provided
  // This is needed for C++20 modules where the AST may reference .pcm files
  // with relative paths
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> vfs =
      llvm::vfs::getRealFileSystem();
  if (!workingDir.empty()) {
    if (std::error_code ec = vfs->setCurrentWorkingDirectory(workingDir)) {
      // Log but don't fail - module mappings are already absolute
      emitStructuredMessage("warning", "[loadAstFromFile] Warning: Failed to "
                                       "set VFS working directory to '" +
                                           workingDir + "': " + ec.message());
    }
  }

  // Setup FileSystemOptions with working directory
  clang::FileSystemOptions fsOpts;
  if (!workingDir.empty()) {
    fsOpts.WorkingDir = workingDir;
  }

  // Setup HeaderSearchOptions for C++20 module resolution
  // Use explicit module file mappings extracted from compilation database
  // Note: extractModuleFileMappings already resolves relative paths to absolute
  clang::HeaderSearchOptions hsOpts;
  for (const auto &[moduleName, pcmPath] : moduleFileMappings) {
    hsOpts.PrebuiltModuleFiles[moduleName] = pcmPath;
    emitStructuredMessage("debug", "[loadAstFromFile] Added module mapping: " +
                                       moduleName + " -> " + pcmPath);
  }

  // Also add working directory as a search path for backward compatibility
  if (!workingDir.empty()) {
    hsOpts.PrebuiltModulePaths.push_back(workingDir);
  }

  // Load AST from file
  // Note: Most configuration is stored in the AST file itself
  // AllowASTWithCompilerErrors=true to handle cases where module files
  // can't be found (the AST was already successfully built, we just need
  // to load the serialized data)
#if LLVM_VERSION_MAJOR >= 22
  // LLVM 22: VFS is 4th param (before DiagOpts)
  std::unique_ptr<clang::ASTUnit> astUnit = clang::ASTUnit::LoadFromASTFile(
      astFilePath, pchContainerOps->getRawReader(),
      clang::ASTUnit::LoadEverything, /*VFS=*/vfs, diagOpts, diags, fsOpts,
      hsOpts,
      /*LangOpts=*/nullptr,
      /*OnlyLocalDecls=*/false, clang::CaptureDiagsKind::None,
      /*AllowASTWithCompilerErrors=*/true,
      /*UserFilesAreVolatile=*/false);
#elif LLVM_VERSION_MAJOR >= 21
  // LLVM 21: VFS is last param, diagOpts passed separately
  std::unique_ptr<clang::ASTUnit> astUnit = clang::ASTUnit::LoadFromASTFile(
      astFilePath, pchContainerOps->getRawReader(),
      clang::ASTUnit::LoadEverything, diagOpts, diags, fsOpts, hsOpts,
      /*LangOpts=*/nullptr,
      /*OnlyLocalDecls=*/false, clang::CaptureDiagsKind::None,
      /*AllowASTWithCompilerErrors=*/true,
      /*UserFilesAreVolatile=*/false,
      /*VFS=*/vfs);
#else
  // LLVM 20: No separate diagOpts, shared_ptr<HeaderSearchOptions>
  auto hsOptsPtr = std::make_shared<clang::HeaderSearchOptions>(hsOpts);
  std::unique_ptr<clang::ASTUnit> astUnit = clang::ASTUnit::LoadFromASTFile(
      astFilePath, pchContainerOps->getRawReader(),
      clang::ASTUnit::LoadEverything, diags, fsOpts, hsOptsPtr,
      /*LangOpts=*/nullptr,
      /*OnlyLocalDecls=*/false, clang::CaptureDiagsKind::None,
      /*AllowASTWithCompilerErrors=*/true,
      /*UserFilesAreVolatile=*/false,
      /*VFS=*/vfs);
#endif

  if (!astUnit) {
    errorMessage = "Failed to load AST from file: " + astFilePath;
    return nullptr;
  }

  return std::move(astUnit);
}

std::vector<std::string>
getSourceFilesFromCompilationDatabase(const std::string &compDbPath,
                                      std::string &errorMessage) {

  // Load compilation database
  std::string loadError;
  std::unique_ptr<ct::CompilationDatabase> compilationDatabase =
      ct::JSONCompilationDatabase::loadFromFile(
          compDbPath, loadError, ct::JSONCommandLineSyntax::AutoDetect);

  if (!compilationDatabase) {
    errorMessage = "Failed to load compilation database: " + loadError;
    return {};
  }

  // Extract all source files
  std::vector<std::string> sourceFiles = compilationDatabase->getAllFiles();

  if (sourceFiles.empty()) {
    errorMessage = "No source files found in compilation database";
    return {};
  }

  return sourceFiles;
}

// Internal helper: Extract -fmodule-file=name=path mappings from compile
// command
static std::map<std::string, std::string>
extractModuleFileMappings(const std::string &compilationDb,
                          const std::string &sourcePath) {
  std::map<std::string, std::string> mappings;

  // Load compilation database
  std::string loadError;
  std::unique_ptr<ct::CompilationDatabase> compdb =
      ct::JSONCompilationDatabase::loadFromFile(
          compilationDb, loadError, ct::JSONCommandLineSyntax::AutoDetect);

  if (!compdb) {
    emitStructuredMessage(
        "error",
        "[extractModuleFileMappings] Failed to load compilation database: " +
            loadError);
    return mappings;
  }

  // Expand response files (this expands @modmap files)
  compdb = ct::expandResponseFiles(std::move(compdb),
                                   llvm::vfs::getRealFileSystem());

  // Get compile commands for the source file
  std::vector<ct::CompileCommand> commands =
      compdb->getCompileCommands(sourcePath);

  if (commands.empty()) {
    emitStructuredMessage(
        "warning",
        "[extractModuleFileMappings] No compile command found for: " +
            sourcePath);
    return mappings;
  }

  const ct::CompileCommand &cmd = commands.front();
  std::string workingDir = cmd.Directory;

  // Parse command line for -fmodule-file=name=path flags
  for (const std::string &arg : cmd.CommandLine) {
    // Check for -fmodule-file=name=path format
    if (arg.rfind("-fmodule-file=", 0) == 0) {
      // Extract the name=path part
      std::string nameAndPath = arg.substr(14); // Skip "-fmodule-file="

      // Find the first '=' which separates module name from path
      size_t eqPos = nameAndPath.find('=');
      if (eqPos != std::string::npos) {
        std::string moduleName = nameAndPath.substr(0, eqPos);
        std::string pcmPath = nameAndPath.substr(eqPos + 1);

        // Resolve relative paths against working directory
        if (!pcmPath.empty() && pcmPath[0] != '/') {
          pcmPath = workingDir + "/" + pcmPath;
        }

        mappings[moduleName] = pcmPath;
        emitStructuredMessage("debug",
                              "[extractModuleFileMappings] Found mapping: " +
                                  moduleName + " -> " + pcmPath);
      }
    }
  }

  return mappings;
}

} // namespace acav
