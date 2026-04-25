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

#include "core/MakeAstRunner.h"
#include "core/ProcessOutputUtils.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>

namespace acav {

MakeAstRunner::MakeAstRunner(QObject *parent)
    : QObject(parent), process_(new QProcess(this)) {
  connect(process_, &QProcess::finished, this,
          &MakeAstRunner::onProcessFinished);
  connect(process_, &QProcess::errorOccurred, this,
          &MakeAstRunner::onProcessError);
  connect(process_, &QProcess::readyReadStandardOutput, this,
          &MakeAstRunner::onProcessStdOut);
  connect(process_, &QProcess::readyReadStandardError, this,
          &MakeAstRunner::onProcessStdErr);
}

MakeAstRunner::~MakeAstRunner() {
  if (process_->state() != QProcess::NotRunning) {
    process_->kill();
    process_->waitForFinished();
  }
}

void MakeAstRunner::run(const QString &compilationDatabasePath,
                        const QString &sourceFilePath,
                        const QString &outputFilePath,
                        const QString &makeAstBinary) {
  if (isRunning()) {
    emit error("make-ast is already running");
    return;
  }

  sourceFilePath_ = sourceFilePath;
  outputFilePath_ = outputFilePath;

  // Determine the binary path
  QString binaryPath = makeAstBinary;
  if (binaryPath.isEmpty()) {
    // Try to find make-ast in the same directory as the app
    QString appDir = QCoreApplication::applicationDirPath();
    binaryPath = appDir + "/make-ast";
  }

  QFileInfo sourceInfo(sourceFilePath);
  emit progress("Generating AST for " + sourceInfo.fileName() + "...");
  elapsed_.restart();

  // Build command arguments
  QStringList arguments;
  arguments << "--compilation-database" << compilationDatabasePath;
  arguments << "--source" << sourceFilePath;
  arguments << "--output" << outputFilePath;
  if (!clangResourceDir_.isEmpty()) {
    arguments << "--clang-resource-dir" << clangResourceDir_;
  }

  qDebug() << "Running:" << binaryPath << arguments.join(" ");

  // Start the process
  process_->start(binaryPath, arguments);
}

bool MakeAstRunner::isRunning() const {
  return process_->state() != QProcess::NotRunning;
}

void MakeAstRunner::terminate() {
  if (process_) {
    process_->terminate();
  }
}

void MakeAstRunner::kill() {
  if (process_) {
    process_->kill();
  }
}

bool MakeAstRunner::waitForFinished(int msecs) {
  if (process_) {
    return process_->waitForFinished(msecs);
  }
  return true; // No process to wait for
}

void MakeAstRunner::onProcessFinished(int exitCode,
                                      QProcess::ExitStatus exitStatus) {
  drainProcessOutput(process_, pendingStdout_, pendingStderr_, "make-ast",
                     [this](const LogEntry &entry) { emit logMessage(entry); });

  if (exitStatus != QProcess::NormalExit) {
    emit error("make-ast process crashed");
    return;
  }

  if (exitCode != 0) {
    emit error(QString("make-ast failed with exit code %1").arg(exitCode));
    return;
  }

  // Success
  QFileInfo sourceInfo(sourceFilePath_);
  const double seconds = elapsed_.isValid()
                             ? static_cast<double>(elapsed_.elapsed()) / 1000.0
                             : 0.0;
  emit progress(QString("make-ast: %1s (%2)")
                    .arg(QString::number(seconds, 'f', 2))
                    .arg(sourceInfo.fileName()));
  emit astReady(outputFilePath_);
}

void MakeAstRunner::onProcessError(QProcess::ProcessError error) {
  drainProcessOutput(process_, pendingStdout_, pendingStderr_, "make-ast",
                     [this](const LogEntry &entry) { emit logMessage(entry); });

  QString systemError = process_->errorString();
  QString errorMessage;
  switch (error) {
  case QProcess::FailedToStart:
    errorMessage = QString("Failed to start make-ast: %1. Make sure it's in the "
                   "same directory as the app or in PATH.").arg(systemError);
    break;
  case QProcess::Crashed:
    errorMessage = QString("make-ast crashed: %1").arg(systemError);
    break;
  case QProcess::Timedout:
    errorMessage = QString("make-ast timed out: %1").arg(systemError);
    break;
  case QProcess::ReadError:
    errorMessage = QString("Error reading from make-ast: %1").arg(systemError);
    break;
  case QProcess::WriteError:
    errorMessage = QString("Error writing to make-ast: %1").arg(systemError);
    break;
  default:
    errorMessage = QString("Unknown error running make-ast: %1").arg(systemError);
    break;
  }

  emit this->error(errorMessage);
  emitErrorLog("make-ast", errorMessage,
               [this](const LogEntry &entry) { emit logMessage(entry); });
}

void MakeAstRunner::onProcessStdOut() {
  emitParsedOutput(pendingStdout_,
                   QString::fromUtf8(process_->readAllStandardOutput()),
                   "make-ast", false,
                   [this](const LogEntry &entry) { emit logMessage(entry); });
}

void MakeAstRunner::onProcessStdErr() {
  emitParsedOutput(pendingStderr_,
                   QString::fromUtf8(process_->readAllStandardError()),
                   "make-ast", true,
                   [this](const LogEntry &entry) { emit logMessage(entry); });
}

} // namespace acav
