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

/// \file MakeAstRunner.h
/// \brief Qt runner for make-ast tool execution.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>

#include "core/LogEntry.h"

namespace acav {

/// \brief Runs the make-ast tool to generate AST cache files
///
/// This class provides a Qt-based interface to run the make-ast
/// command-line tool using QProcess. It executes the tool, waits for
/// completion, and emits signals for success or error.
///
/// Usage:
///   MakeAstRunner runner;
///   connect(&runner, &MakeAstRunner::astReady,
///           this, &MyClass::onAstReady);
///   connect(&runner, &MakeAstRunner::error,
///           this, &MyClass::onError);
///   runner.run(compDb, sourceFile, outputPath);
///
class MakeAstRunner : public QObject {
  Q_OBJECT

public:
  explicit MakeAstRunner(QObject *parent = nullptr);
  ~MakeAstRunner() override;

  /// \brief Run make-ast tool for specified source file
  /// \param compilationDatabasePath Path to compile_commands.json
  /// \param sourceFilePath Path to source file to parse
  /// \param outputFilePath Path where .ast file should be written
  /// \param makeAstBinary Path to make-ast executable (defaults to app dir)
  void run(const QString &compilationDatabasePath,
           const QString &sourceFilePath,
           const QString &outputFilePath,
           const QString &makeAstBinary = QString());

  /// \brief Check if tool is currently running
  bool isRunning() const;

  /// \brief Attempts to terminate the process gracefully
  /// Sends SIGTERM on Unix/macOS, WM_CLOSE on Windows
  void terminate();

  /// \brief Kills the process immediately
  /// Sends SIGKILL on Unix/macOS, forceful termination on Windows
  void kill();

  /// \brief Set the Clang resource directory
  /// \param dir Path to clang resource dir (lib/clang/<ver>)
  void setClangResourceDir(const QString &dir) { clangResourceDir_ = dir; }

  /// \brief Waits for the process to finish
  /// \param msecs Maximum time to wait in milliseconds (default: 30000ms)
  /// \return true if process finished, false if timeout occurred
  bool waitForFinished(int msecs = 30000);

signals:
  /// \brief Emitted when make-ast completes successfully
  /// \param astFilePath Path to generated .ast file
  void astReady(const QString &astFilePath);

  /// \brief Emitted when an error occurs
  /// \param errorMessage Description of the error
  void error(const QString &errorMessage);

  /// \brief Emitted with progress updates
  /// \param message Progress message (e.g., "Generating AST for file.cpp...")
  void progress(const QString &message);

  /// \brief Emitted when the tool produces log output
  void logMessage(const LogEntry &entry);

private slots:
  void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void onProcessError(QProcess::ProcessError error);
  void onProcessStdOut();
  void onProcessStdErr();

private:
  QProcess *process_;
  QString sourceFilePath_;
  QString outputFilePath_;
  QString clangResourceDir_;
  QElapsedTimer elapsed_;
  QString pendingStdout_;
  QString pendingStderr_;
};

} // namespace acav
