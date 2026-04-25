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

/// \file AstExtractorRunner.h
/// \brief Qt runner for AST extraction pipeline.
#pragma once

#include "common/FileManager.h"
#include "core/AstNode.h"
#include "core/LogEntry.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace clang {
class ASTUnit;
} // namespace clang

namespace acav {

class AstContext; // Forward declaration
struct DiagnosticMessage; // Forward declaration

/// \brief Statistics collected during AST extraction
struct AstExtractionStats {
  std::size_t totalCount = 0;              // Total nodes visited
  std::size_t declCount = 0;               // Declaration nodes
  std::size_t stmtCount = 0;               // Statement nodes
  std::size_t typeCount = 0;               // Type nodes (deduplicated)
  std::size_t typeLocCount = 0;            // TypeLoc nodes
  std::size_t attrCount = 0;               // Attribute nodes
  std::size_t conceptRefCount = 0;         // ConceptReference nodes
  std::size_t cxxBaseSpecCount = 0;        // CXXBaseSpecifier nodes
  std::size_t ctorInitCount = 0;           // CXXCtorInitializer nodes
  std::size_t lambdaCaptureCount = 0;      // LambdaCapture nodes
  std::size_t nestedNameSpecCount = 0;     // NestedNameSpecifier nodes
  std::size_t nestedNameSpecLocCount = 0;  // NestedNameSpecifierLoc nodes
  std::size_t tempArgCount = 0;            // TemplateArgument nodes
  std::size_t tempArgLocCount = 0;         // TemplateArgumentLoc nodes
  std::size_t tempNameCount = 0;           // TemplateName nodes
  std::size_t commentCount = 0;            // Comment nodes
};

/// \brief Qt runner that loads AST from cache and builds ACAV tree.
///
/// This class handles the complete AST extraction pipeline:
/// 1. Registers input files with FileManager
/// 2. Loads Clang ASTUnit from serialized .ast file
/// 3. Builds ACAV AST tree by traversing Clang AST
/// 4. Emits Qt signals for lifecycle events and progress
///
/// Design: This runner executes synchronously (blocking). For async execution,
/// move it to a QThread and use Qt's signal/slot mechanism.
///
/// Testing: Inject a custom AstLoader via constructor to mock AST loading.
class AstExtractorRunner : public QObject {
  Q_OBJECT

public:
  /// \brief Function type for loading AST from file.
  ///
  /// \param astFilePath Path to .ast file
  /// \param errorOut Output parameter for error message
  /// \param compilationDbPath Path to compile_commands.json for module resolution
  /// \param sourcePath Source file path for module mapping extraction
  /// \return Loaded ASTUnit or nullptr on failure
  using AstLoader = std::function<std::unique_ptr<clang::ASTUnit>(
      const std::string &astFilePath, std::string &errorOut,
      const std::string &compilationDbPath, const std::string &sourcePath)>;

  /// \brief Construct runner with default AST loader.
  explicit AstExtractorRunner(AstContext *context, FileManager &fileManager,
                              QObject *parent = nullptr);

  /// \brief Construct runner with custom AST loader (for testing).
  explicit AstExtractorRunner(AstContext *context, FileManager &fileManager,
                              AstLoader loader, QObject *parent = nullptr);

  ~AstExtractorRunner() override;

  // Non-copyable, non-movable
  AstExtractorRunner(const AstExtractorRunner &) = delete;
  AstExtractorRunner &operator=(const AstExtractorRunner &) = delete;
  AstExtractorRunner(AstExtractorRunner &&) = delete;
  AstExtractorRunner &operator=(AstExtractorRunner &&) = delete;

  /// \brief Run extraction pipeline and emit lifecycle signals.
  ///
  /// This method is synchronous and will block until extraction completes.
  /// For async execution, move this runner to a QThread.
  ///
  /// \param astFilePath Path to serialized .ast file
  /// \param tuFilePaths Paths to register with FileManager
  /// \param compilationDbPath Path to compilation database (used to determine
  ///        working directory for loading AST with C++20 module support)
  ///
  /// Signals emitted: started -> progress* -> (finished | error)
  void run(const QString &astFilePath, const QStringList &tuFilePaths,
           const QString &compilationDbPath = QString());

  /// \brief Enable or disable comment extraction in AST
  void setCommentExtractionEnabled(bool enabled);

  bool commentExtractionEnabled() const { return commentExtractionEnabled_.load(); }

signals:
  void started(const QString &astFilePath);
  void progress(const QString &message);
  void statsUpdated(const AstExtractionStats &stats);
  void finished(AstViewNode *root);
  void error(const QString &message);
  void logMessage(const LogEntry &entry);

private:
  void registerInputFiles(const QStringList &tuFilePaths);
  AstViewNode *buildTreeFromASTUnit(clang::ASTUnit &astUnit);
  void buildLocationIndex(AstViewNode *node);

  AstContext *context_;      // Not owned
  FileManager &fileManager_; // Not owned
  AstLoader astLoader_;
  std::function<void(const DiagnosticMessage &)> diagnosticCallback_;
  std::chrono::steady_clock::time_point start_;
  std::atomic<bool> commentExtractionEnabled_{false};
};

} // namespace acav
