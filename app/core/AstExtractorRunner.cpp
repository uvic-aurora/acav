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

#include "core/AstExtractorRunner.h"
#include "common/ClangUtils.h"
#include "common/InternedString.h"
#include "core/AcavAstBuilder.h"
#include "core/MemoryProfiler.h"
#include "core/SourceLocation.h"
#include <QDateTime>
#include <QFileInfo>
#include <QString>
#include <chrono>
// Qt defines 'emit' as a no-op macro which conflicts with Sema.h in LLVM 22+
// (ASTUnit.h → ASTWriter.h → Sema.h has a method named 'emit')
#undef emit
#include <clang/Frontend/ASTUnit.h>
#define emit

namespace acav {

AstExtractorRunner::AstExtractorRunner(AstContext *context,
                                       FileManager &fileManager,
                                       QObject *parent)
    : QObject(parent), context_(context), fileManager_(fileManager) {
  diagnosticCallback_ = [this](const DiagnosticMessage &diag) {
    LogEntry entry;
    switch (diag.level) {
    case clang::DiagnosticsEngine::Warning:
      entry.level = LogLevel::Warning;
      break;
    case clang::DiagnosticsEngine::Error:
    case clang::DiagnosticsEngine::Fatal:
      entry.level = LogLevel::Error;
      break;
    case clang::DiagnosticsEngine::Remark:
    case clang::DiagnosticsEngine::Note:
    case clang::DiagnosticsEngine::Ignored:
    default:
      entry.level = LogLevel::Debug;
      break;
    }

    QString message = QString::fromStdString(diag.message);
    if (!diag.file.empty()) {
      QString location = QString::fromStdString(diag.file);
      if (diag.line > 0) {
        location += QString(":%1").arg(diag.line);
      }
      if (diag.column > 0) {
        location += QString(":%1").arg(diag.column);
      }
      message = QString("%1: %2").arg(location, message);
    }

    entry.source = "acav-clang";
    entry.message = message;
    entry.timestamp = QDateTime::currentDateTime();
    emit logMessage(entry);
  };

  // Use the default loader: loadAstFromFile handles module path extraction
  astLoader_ = [this](const std::string &path, std::string &errorMsg,
                      const std::string &compilationDbPath,
                      const std::string &sourcePath)
      -> std::unique_ptr<clang::ASTUnit> {
    return acav::loadAstFromFile(path, errorMsg, compilationDbPath,
                                   sourcePath, diagnosticCallback_);
  };
}

AstExtractorRunner::AstExtractorRunner(AstContext *context,
                                       FileManager &fileManager,
                                       AstLoader loader, QObject *parent)
    : QObject(parent), context_(context), fileManager_(fileManager),
      astLoader_(std::move(loader)) {}

AstExtractorRunner::~AstExtractorRunner() = default;

void AstExtractorRunner::setCommentExtractionEnabled(bool enabled) {
  commentExtractionEnabled_.store(enabled);
}

void AstExtractorRunner::run(const QString &astFilePath,
                             const QStringList &tuFilePaths,
                             const QString &compilationDbPath) {
  // Ensure a clean state for each extraction run
  SourceLocation::resetCache();

  start_ = std::chrono::steady_clock::now();
  emit started(astFilePath);

  emit progress(QStringLiteral("Pre-registering files..."));
  registerInputFiles(tuFilePaths);

  emit progress(QStringLiteral("Loading AST from cache..."));
  auto loadStart = std::chrono::steady_clock::now();

  // Get first source file for module mapping extraction
  std::string sourcePath;
  if (!tuFilePaths.isEmpty()) {
    sourcePath = tuFilePaths.first().toStdString();
  }

  // loadAstFromFile handles module path extraction internally
  std::string stdError;
  std::unique_ptr<clang::ASTUnit> astUnit =
      astLoader_(astFilePath.toStdString(), stdError,
                 compilationDbPath.toStdString(), sourcePath);

  if (!astUnit) {
    QString errorMsg = stdError.empty() ? QStringLiteral("Failed to load AST")
                                        : QString::fromStdString(stdError);
    emit error(errorMsg);
    return;
  }

  auto loadEnd = std::chrono::steady_clock::now();
  emit progress(
      QString("Loaded AST: %1s")
          .arg(std::chrono::duration<double>(loadEnd - loadStart).count(), 0,
               'f', 2));
  
  // Checkpoint: Clang AST loaded into memory
  MemoryProfiler::checkpoint("After loading Clang ASTUnit");

  emit progress(QStringLiteral("Building ACAV AST ..."));
  auto buildStart = std::chrono::steady_clock::now();

  AstViewNode *root = buildTreeFromASTUnit(*astUnit);
  if (!root) {
    emit error(QStringLiteral("Failed to build AST"));
    return;
  }

  auto buildEnd = std::chrono::steady_clock::now();
  emit progress(
      QString("Built tree: %1s")
          .arg(std::chrono::duration<double>(buildEnd - buildStart).count(), 0,
               'f', 2));
  
  // Checkpoint: ACAV AST built, Clang AST still in memory
  MemoryProfiler::checkpoint("After building ACAV AST (Clang still loaded)");

#ifdef ACAV_ENABLE_STRING_STATS
  InternedString::printStats("After AST extraction");
  context_->printTypeDeduplicationStats("After AST extraction");
#endif

  // EXPLICITLY destroy Clang AST before returning
  // This ensures cleanup happens on worker thread before we emit finished
  astUnit.reset();  // Force Clang AST destruction NOW!
  
  // Checkpoint: Clang AST explicitly destroyed
  MemoryProfiler::checkpoint("After destroying Clang ASTUnit");

  auto totalEnd = std::chrono::steady_clock::now();
  emit progress(
      QString("Total: %1s")
          .arg(std::chrono::duration<double>(totalEnd - start_).count(), 0, 'f',
               2));

  emit finished(root);
}

void AstExtractorRunner::registerInputFiles(const QStringList &tuFilePaths) {
  for (const QString &filePath : tuFilePaths) {
    fileManager_.registerFile(filePath.toStdString());
  }
}

AstViewNode *AstExtractorRunner::buildTreeFromASTUnit(clang::ASTUnit &astUnit) {
  clang::ASTContext &ctx = astUnit.getASTContext();
  clang::TranslationUnitDecl *tuDecl = ctx.getTranslationUnitDecl();
  if (!tuDecl) {
    return nullptr;
  }

  AstExtractionStats stats;
  AstViewNode *root = AcavAstBuilder::buildFromASTUnit(
      astUnit, context_, fileManager_, stats,
      commentExtractionEnabled_.load());

  emit statsUpdated(stats);

  if (root) {
    emit progress(QStringLiteral("Building location index..."));
    auto indexStart = std::chrono::steady_clock::now();
    buildLocationIndex(root);
    context_->finalizeLocationIndex();
    auto indexEnd = std::chrono::steady_clock::now();
    emit progress(
        QString("Index built: %1s")
            .arg(std::chrono::duration<double>(indexEnd - indexStart).count(),
                 0, 'f', 2));
  }

  return root;
}

void AstExtractorRunner::buildLocationIndex(AstViewNode *node) {
  if (!node) {
    return;
  }

  context_->indexNode(node);
  for (AstViewNode *child : node->getChildren()) {
    buildLocationIndex(child);
  }
}

} // namespace acav
