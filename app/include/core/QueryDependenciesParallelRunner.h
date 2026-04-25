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

/// \file QueryDependenciesParallelRunner.h
/// \brief Parallel runner specifically for query-dependencies tool
#pragma once

#include "core/ParallelProcessRunner.h"
#include <QElapsedTimer>
#include <QJsonObject>

namespace acav {

/// \brief Parallel runner specifically for query-dependencies tool
///
/// This class extends ParallelProcessRunner to handle query-dependencies-specific logic:
/// - Building command-line arguments with --source options
/// - Merging JSON dependency outputs
/// - Emitting domain-specific signals (dependenciesReady)
///
/// Usage:
///   QueryDependenciesParallelRunner runner;
///   connect(&runner, &QueryDependenciesParallelRunner::dependenciesReady,
///           this, &MyClass::onDependenciesReady);
///   runner.run("/path/to/compile_commands.json", "/path/to/output.json");
///
class QueryDependenciesParallelRunner : public ParallelProcessRunner {
  Q_OBJECT

public:
  explicit QueryDependenciesParallelRunner(QObject *parent = nullptr);
  ~QueryDependenciesParallelRunner() override = default;

  /// \brief Run query-dependencies in parallel
  /// \param compilationDatabasePath Path to compile_commands.json
  /// \param outputFilePath Path where final dependencies.json should be written
  /// \param queryDependenciesBinary Path to query-dependencies executable (optional)
  void run(const QString &compilationDatabasePath,
           const QString &outputFilePath,
           const QString &queryDependenciesBinary = QString());

  /// \brief Set the Clang resource directory
  /// \param dir Path to clang resource dir (lib/clang/<ver>)
  void setClangResourceDir(const QString &dir) { clangResourceDir_ = dir; }

signals:
  /// \brief Emitted when all processes complete successfully
  void dependenciesReady(const QJsonObject &dependencies);

  /// \brief Emitted when processes complete with some errors
  void dependenciesReadyWithErrors(const QJsonObject &dependencies,
                                   const QStringList &errorMessages);

protected:
  /// \brief Build command-line arguments for query-dependencies
  QStringList prepareProcessArguments(
      int chunkIndex,
      const QStringList &chunkData,
      const QString &tempOutputPath) override;

  /// \brief Merge JSON dependency files
  bool mergeResults(
      const QStringList &tempOutputPaths,
      const QString &finalOutputPath,
      QString &errorMessage) override;

  /// \brief Handle completion and emit domain-specific signals
  void onAllCompleted(int successCount, int failureCount, int totalCount) override;

private:
  QString compilationDatabasePath_;
  QString queryDependenciesBinary_;
  QString clangResourceDir_;
  QElapsedTimer elapsed_;
};

} // namespace acav
