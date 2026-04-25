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

/// \file ParallelProcessRunner.h
/// \brief Generic parallel process runner for executing multiple instances of a program
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <vector>

#include "core/LogEntry.h"

namespace acav {

/// \brief Generic parallel process runner for executing multiple instances of a program
///
/// This class provides a reusable framework for running multiple processes in parallel,
/// managing their lifecycle, and aggregating results. It's designed to be extended
/// for specific use cases (e.g., query-dependencies, make-ast, custom analyzers).
///
/// Key features:
/// - Automatic chunking of input data
/// - Process lifecycle management
/// - Error aggregation
/// - Progress tracking
/// - Graceful degradation on partial failures
///
/// Usage pattern:
///   1. Subclass ParallelProcessRunner
///   2. Override virtual methods to customize behavior:
///      - prepareProcessArguments() - Build command-line arguments per chunk
///      - mergeResults() - Combine outputs from all chunks
///      - onAllCompleted() - Final processing after all chunks finish
///   3. Call run() with input data
///
class ParallelProcessRunner : public QObject {
  Q_OBJECT

public:
  explicit ParallelProcessRunner(QObject *parent = nullptr);
  ~ParallelProcessRunner() override;

  /// \brief Check if any process is currently running
  bool isRunning() const;

  /// \brief Set number of parallel processes (0 = auto-detect from CPU cores)
  void setParallelCount(int count);

  /// \brief Get current parallel count setting
  int getParallelCount() const { return parallelCount_; }

  /// \brief Cancel all running processes
  void cancel();

signals:
  /// \brief Emitted when an error occurs that prevents execution
  void error(const QString &errorMessage);

  /// \brief Emitted with progress updates (e.g., "Completed 3/8 chunks")
  void progress(const QString &message);

  /// \brief Emitted when a single chunk completes successfully
  /// \param chunkIndex Index of the completed chunk
  /// \param totalChunks Total number of chunks
  void chunkCompleted(int chunkIndex, int totalChunks);

  /// \brief Emitted when a single chunk fails
  /// \param chunkIndex Index of the failed chunk
  /// \param errorMessage Error description
  void chunkFailed(int chunkIndex, const QString &errorMessage);

  /// \brief Emitted when a chunk process produces log output
  void logMessage(const LogEntry &entry);

protected:
  /// \brief Divide input data into chunks for parallel processing
  /// \param inputData Generic input data (e.g., list of files, list of tasks)
  /// \param chunkCount Number of chunks to create
  /// \return Vector of chunks, each chunk is a list of input items
  ///
  /// Default implementation: Round-robin distribution
  /// Override for custom chunking strategies (e.g., size-based, complexity-based)
  virtual std::vector<QStringList> chunkInputData(
      const QStringList &inputData,
      int chunkCount) const;

  /// \brief Prepare command-line arguments for a specific chunk
  /// \param chunkIndex Index of the chunk being processed
  /// \param chunkData Data items in this chunk
  /// \param tempOutputPath Temporary file path for this chunk's output
  /// \return QStringList of command-line arguments
  ///
  /// Must be implemented by subclasses to define program-specific arguments
  virtual QStringList prepareProcessArguments(
      int chunkIndex,
      const QStringList &chunkData,
      const QString &tempOutputPath) = 0;

  /// \brief Merge outputs from all chunks into final result
  /// \param tempOutputPaths Paths to temporary output files (one per chunk)
  /// \param finalOutputPath Path where merged result should be written
  /// \param errorMessage Output parameter for error details
  /// \return true on success, false on failure
  ///
  /// Must be implemented by subclasses to define result merging logic
  virtual bool mergeResults(
      const QStringList &tempOutputPaths,
      const QString &finalOutputPath,
      QString &errorMessage) = 0;

  /// \brief Called when all processes have completed (success or failure)
  /// \param successCount Number of chunks that completed successfully
  /// \param failureCount Number of chunks that failed
  /// \param totalCount Total number of chunks
  ///
  /// Override to perform final processing, emit custom signals, etc.
  /// Default implementation: Calls mergeResults() if any chunks succeeded
  virtual void onAllCompleted(int successCount, int failureCount, int totalCount);

  /// \brief Get path to temporary output file for a chunk
  /// \param chunkIndex Index of the chunk
  /// \return Full path to temporary file
  QString getTempOutputPath(int chunkIndex) const;

  /// \brief Start parallel execution
  /// \param programPath Path to executable to run
  /// \param inputData Input data to be chunked and processed
  /// \param finalOutputPath Where to write final merged output
  void runParallel(
      const QString &programPath,
      const QStringList &inputData,
      const QString &finalOutputPath);

  // Protected members accessible to subclasses
  QString finalOutputPath_;       ///< Final output file path
  QStringList errorMessages_;     ///< Aggregated error messages
  QTemporaryDir tempDir_;         ///< Temporary directory for chunk outputs
  QElapsedTimer elapsed_;

private slots:
  void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void onProcessError(QProcess::ProcessError error);
  void onProcessStdOut();
  void onProcessStdErr();

private:
  QString processSource(QProcess *process) const;
  void checkAllProcessesCompleted();

  // Configuration
  QString programPath_;
  int parallelCount_;  // Number of parallel processes (0 = auto-detect)

  // Process management
  std::vector<QProcess*> processes_;
  QStringList tempOutputPaths_;
  int completedProcessCount_;
  int failedProcessCount_;
  QHash<QProcess*, QString> pendingStdout_;
  QHash<QProcess*, QString> pendingStderr_;
};

} // namespace acav
