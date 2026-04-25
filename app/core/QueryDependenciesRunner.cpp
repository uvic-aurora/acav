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

#include "core/QueryDependenciesRunner.h"
#include "core/ProcessOutputUtils.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace acav {

QueryDependenciesRunner::QueryDependenciesRunner(QObject *parent)
    : QObject(parent), process_(new QProcess(this)) {
  connect(process_, &QProcess::finished, this,
          &QueryDependenciesRunner::onProcessFinished);
  connect(process_, &QProcess::errorOccurred, this,
          &QueryDependenciesRunner::onProcessError);
  connect(process_, &QProcess::readyReadStandardOutput, this,
          &QueryDependenciesRunner::onProcessStdOut);
  connect(process_, &QProcess::readyReadStandardError, this,
          &QueryDependenciesRunner::onProcessStdErr);
}

QueryDependenciesRunner::~QueryDependenciesRunner() {
  if (process_->state() != QProcess::NotRunning) {
    process_->kill();
    process_->waitForFinished();
  }
}

void QueryDependenciesRunner::run(const QString &compilationDatabasePath,
                                  const QString &outputFilePath,
                                  const QString &queryDependenciesBinary) {
  if (isRunning()) {
    emit error("Query-dependencies is already running");
    return;
  }

  compilationDatabasePath_ = compilationDatabasePath;
  outputFilePath_ = outputFilePath;

  // Determine the binary path
  QString binaryPath = queryDependenciesBinary;
  if (binaryPath.isEmpty()) {
    // Try to find query-dependencies in the same directory as the app
    QString appDir = QCoreApplication::applicationDirPath();
    binaryPath = appDir + "/query-dependencies";
  }

  emit progress("Running query-dependencies...");
  elapsed_.restart();

  // Build command arguments - output to file instead of stdout
  QStringList arguments;
  arguments << "--compilation-database" << compilationDatabasePath;
  arguments << "--output" << outputFilePath;
  if (!clangResourceDir_.isEmpty()) {
    arguments << "--clang-resource-dir" << clangResourceDir_;
  }

  qDebug() << "Running:" << binaryPath << arguments.join(" ");

  process_->start(binaryPath, arguments);
}

bool QueryDependenciesRunner::isRunning() const {
  return process_->state() != QProcess::NotRunning;
}

void QueryDependenciesRunner::onProcessFinished(
    int exitCode, QProcess::ExitStatus exitStatus) {
  drainProcessOutput(process_, pendingStdout_, pendingStderr_,
                     "query-dependencies",
                     [this](const LogEntry &entry) { emit logMessage(entry); });

  if (exitStatus != QProcess::NormalExit) {
    emit error("Query-dependencies process crashed");
    return;
  }

  if (exitCode != 0) {
    emit error(QString("Query-dependencies failed with exit code %1")
                   .arg(exitCode));
    return;
  }

  // Read JSON from output file
  QFile outputFile(outputFilePath_);
  if (!outputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    emit error(QString("Failed to open dependencies file: %1")
                   .arg(outputFile.errorString()));
    return;
  }

  QByteArray jsonData = outputFile.readAll();
  outputFile.close();

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    emit error(QString("Failed to parse query-dependencies JSON output: %1")
                   .arg(parseError.errorString()));
    return;
  }

  if (!doc.isObject()) {
    emit error("Query-dependencies output is not a JSON object");
    return;
  }

  QJsonObject jsonObj = doc.object();

  // Check for errors section
  if (jsonObj.contains("errors") && jsonObj["errors"].isArray()) {
    QJsonArray errorsArray = jsonObj["errors"].toArray();
    QStringList errorMessages;

    for (const QJsonValue &errorVal : errorsArray) {
      if (errorVal.isObject()) {
        QJsonObject errorObj = errorVal.toObject();
        QString path = errorObj["path"].toString();
        QString message = errorObj["message"].toString();
        errorMessages.append(QString("%1: %2").arg(path, message));
      }
    }

    emit progress(QString("Dependencies loaded with %1 errors")
                      .arg(errorMessages.size()));
    emit dependenciesReadyWithErrors(jsonObj, errorMessages);
  } else {
    // No errors, normal success
    const double seconds = elapsed_.isValid()
                               ? static_cast<double>(elapsed_.elapsed()) / 1000.0
                               : 0.0;
    emit progress(
        QString("query-dependencies: %1s")
            .arg(QString::number(seconds, 'f', 2)));
    emit dependenciesReady(jsonObj);
  }
}

void QueryDependenciesRunner::onProcessError(QProcess::ProcessError error) {
  drainProcessOutput(process_, pendingStdout_, pendingStderr_,
                     "query-dependencies",
                     [this](const LogEntry &entry) { emit logMessage(entry); });

  QString systemError = process_->errorString();
  QString errorMessage;
  switch (error) {
  case QProcess::FailedToStart:
    errorMessage = QString("Failed to start query-dependencies: %1. Make sure it's in the "
                   "same directory as the app or in PATH.").arg(systemError);
    break;
  case QProcess::Crashed:
    errorMessage = QString("Query-dependencies crashed: %1").arg(systemError);
    break;
  case QProcess::Timedout:
    errorMessage = QString("Query-dependencies timed out: %1").arg(systemError);
    break;
  case QProcess::ReadError:
    errorMessage = QString("Error reading from query-dependencies: %1").arg(systemError);
    break;
  case QProcess::WriteError:
    errorMessage = QString("Error writing to query-dependencies: %1").arg(systemError);
    break;
  default:
    errorMessage = QString("Unknown error running query-dependencies: %1").arg(systemError);
    break;
  }

  emit this->error(errorMessage);
  emitErrorLog("query-dependencies", errorMessage,
               [this](const LogEntry &entry) { emit logMessage(entry); });
}

void QueryDependenciesRunner::onProcessStdOut() {
  emitParsedOutput(pendingStdout_,
                   QString::fromUtf8(process_->readAllStandardOutput()),
                   "query-dependencies", false,
                   [this](const LogEntry &entry) { emit logMessage(entry); });
}

void QueryDependenciesRunner::onProcessStdErr() {
  emitParsedOutput(pendingStderr_,
                   QString::fromUtf8(process_->readAllStandardError()),
                   "query-dependencies", true,
                   [this](const LogEntry &entry) { emit logMessage(entry); });
}

} // namespace acav
