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

/// \file QueryDependenciesRunner.h
/// \brief Qt runner for query-dependencies tool.
#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

#include "core/LogEntry.h"

namespace acav {

/// \brief Runs the query-dependencies tool and parses its output
///
/// This class provides a Qt-based interface to run the query-dependencies
/// command-line tool using QProcess. It executes the tool, waits for completion,
/// and parses the JSON output.
///
/// Usage:
///   QueryDependenciesRunner runner;
///   connect(&runner, &QueryDependenciesRunner::dependenciesReady,
///           this, &MyClass::onDependenciesReady);
///   connect(&runner, &QueryDependenciesRunner::error,
///           this, &MyClass::onError);
///   runner.run("/path/to/compile_commands.json");
///
class QueryDependenciesRunner : public QObject {
  Q_OBJECT

public:
  explicit QueryDependenciesRunner(QObject *parent = nullptr);
  ~QueryDependenciesRunner() override;

  /// \brief Run query-dependencies tool with the specified compilation database
  /// \param compilationDatabasePath Path to compile_commands.json
  /// \param outputFilePath Path where dependencies.json should be written
  /// \param queryDependenciesBinary Path to query-dependencies executable
  ///                                (defaults to "query-dependencies" in PATH)
  void run(const QString &compilationDatabasePath,
           const QString &outputFilePath,
           const QString &queryDependenciesBinary = QString());

  /// \brief Check if query is currently running
  bool isRunning() const;

  /// \brief Set the Clang resource directory
  /// \param dir Path to clang resource dir (lib/clang/<ver>)
  void setClangResourceDir(const QString &dir) { clangResourceDir_ = dir; }

signals:
  /// \brief Emitted when query-dependencies completes successfully
  /// \param dependencies Parsed JSON object from query-dependencies output
  void dependenciesReady(const QJsonObject &dependencies);

  /// \brief Emitted when query-dependencies completes with some errors
  /// \param dependencies Parsed JSON object (includes error section)
  /// \param errorMessages List of formatted error messages
  void dependenciesReadyWithErrors(const QJsonObject &dependencies,
                                   const QStringList &errorMessages);

  /// \brief Emitted when an error occurs
  /// \param errorMessage Description of the error
  void error(const QString &errorMessage);

  /// \brief Emitted with progress updates (e.g., "Running query-dependencies...")
  /// \param message Progress message
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
  QString compilationDatabasePath_;
  QString outputFilePath_;
  QString clangResourceDir_;
  QElapsedTimer elapsed_;
  QString pendingStdout_;
  QString pendingStderr_;
};

} // namespace acav
