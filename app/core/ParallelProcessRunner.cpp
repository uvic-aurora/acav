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

/// \file ParallelProcessRunner.cpp
/// \brief Implementation of generic parallel process runner
#include "core/ParallelProcessRunner.h"
#include "core/ProcessOutputUtils.h"
#include <QDebug>
#include <QFileInfo>
#include <QThread>

namespace acav {

ParallelProcessRunner::ParallelProcessRunner(QObject *parent)
    : QObject(parent), parallelCount_(0), completedProcessCount_(0),
      failedProcessCount_(0) {}

ParallelProcessRunner::~ParallelProcessRunner() {
  cancel();
  // Clean up process objects
  for (auto* process : processes_) {
    delete process;
  }
  pendingStdout_.clear();
  pendingStderr_.clear();
}

void ParallelProcessRunner::setParallelCount(int count) {
  parallelCount_ = count;
}

bool ParallelProcessRunner::isRunning() const {
  for (const auto* process : processes_) {
    if (process && process->state() != QProcess::NotRunning) {
      return true;
    }
  }
  return false;
}

void ParallelProcessRunner::cancel() {
  for (auto* process : processes_) {
    if (process && process->state() != QProcess::NotRunning) {
      process->kill();
      process->waitForFinished(1000);
    }
  }
}

std::vector<QStringList> ParallelProcessRunner::chunkInputData(
    const QStringList &inputData, int chunkCount) const {

  std::vector<QStringList> chunks(chunkCount);

  // Round-robin distribution for even load balancing
  for (int i = 0; i < inputData.size(); ++i) {
    chunks[i % chunkCount].append(inputData[i]);
  }

  return chunks;
}

void ParallelProcessRunner::runParallel(
    const QString &programPath,
    const QStringList &inputData,
    const QString &finalOutputPath) {

  if (isRunning()) {
    emit error("Processes are already running");
    return;
  }

  if (inputData.isEmpty()) {
    emit error("No input data to process");
    return;
  }

  programPath_ = programPath;
  finalOutputPath_ = finalOutputPath;

  // Determine chunk count
  int chunkCount = parallelCount_;
  if (chunkCount <= 0) {
    chunkCount = QThread::idealThreadCount();
    if (chunkCount <= 0) {
      chunkCount = 4;  // Fallback default
    }
  }

  // Don't create more chunks than input items
  chunkCount = std::min(chunkCount, static_cast<int>(inputData.size()));

  emit progress(QString("Dividing %1 items into %2 chunks for parallel processing")
                    .arg(inputData.size()).arg(chunkCount));

  // Divide input data into chunks
  auto chunks = chunkInputData(inputData, chunkCount);

  // Create temporary directory
  if (!tempDir_.isValid()) {
    emit error("Failed to create temporary directory for chunk outputs");
    return;
  }

  // Clean up previous run
  for (auto* process : processes_) {
    delete process;
  }
  processes_.clear();
  tempOutputPaths_.clear();
  errorMessages_.clear();
  completedProcessCount_ = 0;
  failedProcessCount_ = 0;
  pendingStdout_.clear();
  pendingStderr_.clear();
  elapsed_.restart();

  // Launch processes for each chunk
  for (int i = 0; i < static_cast<int>(chunks.size()); ++i) {
    const QStringList& chunk = chunks[i];
    QString tempOutputPath = getTempOutputPath(i);
    tempOutputPaths_.append(tempOutputPath);

    // Let subclass prepare arguments
    QStringList arguments = prepareProcessArguments(i, chunk, tempOutputPath);

    // Create and configure process
    QProcess* process = new QProcess(this);
    process->setProgram(programPath_);
    process->setArguments(arguments);
    process->setProperty("chunkIndex", i);

    connect(process, &QProcess::finished, this,
            &ParallelProcessRunner::onProcessFinished);
    connect(process, &QProcess::errorOccurred, this,
            &ParallelProcessRunner::onProcessError);
    connect(process, &QProcess::readyReadStandardOutput, this,
            &ParallelProcessRunner::onProcessStdOut);
    connect(process, &QProcess::readyReadStandardError, this,
            &ParallelProcessRunner::onProcessStdErr);

    processes_.push_back(process);

    qDebug() << "Starting chunk" << i << "with" << chunk.size() << "items";
    process->start();
  }

  emit progress(QString("Started %1 parallel processes").arg(chunks.size()));
}

QString ParallelProcessRunner::getTempOutputPath(int chunkIndex) const {
  return tempDir_.filePath(QString("output_%1.tmp").arg(chunkIndex));
}

void ParallelProcessRunner::onProcessFinished(
    int exitCode, QProcess::ExitStatus exitStatus) {

  QProcess* process = qobject_cast<QProcess*>(sender());
  if (!process) return;

  QString stderr_output;
  drainProcessOutput(process, pendingStdout_, pendingStderr_,
                     processSource(process),
                     [this](const LogEntry &entry) { emit logMessage(entry); },
                     nullptr, &stderr_output);

  int chunkIndex = process->property("chunkIndex").toInt();
  int totalChunks = static_cast<int>(processes_.size());

  if (exitStatus != QProcess::NormalExit || exitCode != 0) {
    failedProcessCount_++;
    QString errorMsg = QString("Chunk %1 failed with exit code %2:\n%3")
                           .arg(chunkIndex).arg(exitCode).arg(stderr_output);
    errorMessages_.append(errorMsg);
    qDebug() << errorMsg;
    emit chunkFailed(chunkIndex, errorMsg);
  } else {
    completedProcessCount_++;
    qDebug() << "Chunk" << chunkIndex << "completed successfully";
    emit chunkCompleted(chunkIndex, totalChunks);
  }

  checkAllProcessesCompleted();

  pendingStdout_.remove(process);
  pendingStderr_.remove(process);
}

void ParallelProcessRunner::onProcessError(QProcess::ProcessError processError) {
  QProcess* process = qobject_cast<QProcess*>(sender());
  if (!process) return;

  int chunkIndex = process->property("chunkIndex").toInt();
  failedProcessCount_++;

  drainProcessOutput(process, pendingStdout_, pendingStderr_,
                     processSource(process),
                     [this](const LogEntry &entry) { emit logMessage(entry); });

  QString errorMessage;
  QString systemError = process->errorString();
  switch (processError) {
    case QProcess::FailedToStart:
      errorMessage = QString("Chunk %1: Failed to start process: %2")
                         .arg(chunkIndex).arg(systemError);
      break;
    case QProcess::Crashed:
      errorMessage = QString("Chunk %1: Process crashed: %2")
                         .arg(chunkIndex).arg(systemError);
      break;
    default:
      errorMessage = QString("Chunk %1: Process error %2: %3")
                         .arg(chunkIndex).arg(processError).arg(systemError);
      break;
  }

  errorMessages_.append(errorMessage);
  qDebug() << errorMessage;
  emit chunkFailed(chunkIndex, errorMessage);

  emitErrorLog(processSource(process), errorMessage,
               [this](const LogEntry &entry) { emit logMessage(entry); });

  checkAllProcessesCompleted();
}

void ParallelProcessRunner::onProcessStdOut() {
  QProcess* process = qobject_cast<QProcess*>(sender());
  if (!process) return;
  emitParsedOutput(pendingStdout_, process,
                   QString::fromUtf8(process->readAllStandardOutput()),
                   processSource(process), false,
                   [this](const LogEntry &entry) { emit logMessage(entry); });
}

void ParallelProcessRunner::onProcessStdErr() {
  QProcess* process = qobject_cast<QProcess*>(sender());
  if (!process) return;
  emitParsedOutput(pendingStderr_, process,
                   QString::fromUtf8(process->readAllStandardError()),
                   processSource(process), true,
                   [this](const LogEntry &entry) { emit logMessage(entry); });
}

QString ParallelProcessRunner::processSource(QProcess *process) const {
  const int chunkIndex = process ? process->property("chunkIndex").toInt() : 0;
  QString base = QFileInfo(programPath_).baseName();
  if (base.isEmpty()) {
    base = "process";
  }
  if (processes_.size() > 1) {
    return QString("%1[%2]").arg(base).arg(chunkIndex);
  }
  return base;
}

void ParallelProcessRunner::checkAllProcessesCompleted() {
  int totalProcesses = static_cast<int>(processes_.size());
  int finishedProcesses = completedProcessCount_ + failedProcessCount_;

  if (finishedProcesses < totalProcesses) {
    emit progress(QString("Completed %1/%2 chunks")
                      .arg(finishedProcesses).arg(totalProcesses));
    return;
  }

  // All processes finished
  onAllCompleted(completedProcessCount_, failedProcessCount_, totalProcesses);
}

void ParallelProcessRunner::onAllCompleted(
    int successCount, int failureCount, int totalCount) {

  if (failureCount == totalCount) {
    emit error(QString("All %1 chunks failed").arg(totalCount));
    return;
  }

  if (successCount == 0) {
    emit error("No chunks completed successfully");
    return;
  }

  emit progress("All chunks completed, merging results...");

  // Merge results
  QString mergeError;
  bool mergeSuccess = mergeResults(tempOutputPaths_, finalOutputPath_, mergeError);

  if (!mergeSuccess) {
    emit error(QString("Failed to merge chunk outputs: %1").arg(mergeError));
    return;
  }

  const double seconds = elapsed_.isValid()
                             ? static_cast<double>(elapsed_.elapsed()) / 1000.0
                             : 0.0;
  emit progress(
      QString("Results merged successfully in %1s")
          .arg(QString::number(seconds, 'f', 2)));
}

} // namespace acav
