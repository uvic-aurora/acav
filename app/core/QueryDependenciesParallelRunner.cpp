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

/// \file QueryDependenciesParallelRunner.cpp
/// \brief Implementation of query-dependencies-specific parallel runner
#include "core/QueryDependenciesParallelRunner.h"
#include "common/ClangUtils.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace acav {

QueryDependenciesParallelRunner::QueryDependenciesParallelRunner(QObject *parent)
    : ParallelProcessRunner(parent) {}

void QueryDependenciesParallelRunner::run(
    const QString &compilationDatabasePath,
    const QString &outputFilePath,
    const QString &queryDependenciesBinary) {

  compilationDatabasePath_ = compilationDatabasePath;

  // Determine binary path
  queryDependenciesBinary_ = queryDependenciesBinary.isEmpty()
      ? QCoreApplication::applicationDirPath() + "/query-dependencies"
      : queryDependenciesBinary;

  // Extract source files from compilation database
  std::string errorMsg;
  std::vector<std::string> sourceFilesVec =
      acav::getSourceFilesFromCompilationDatabase(
          compilationDatabasePath.toStdString(), errorMsg);

  if (sourceFilesVec.empty()) {
    emit error(QString("Failed to load source files: %1")
                   .arg(QString::fromStdString(errorMsg)));
    return;
  }

  // Convert to QStringList
  QStringList sourceFiles;
  for (const auto& file : sourceFilesVec) {
    sourceFiles.append(QString::fromStdString(file));
  }

  // Run parallel execution (calls base class)
  elapsed_.restart();
  runParallel(queryDependenciesBinary_, sourceFiles, outputFilePath);
}

QStringList QueryDependenciesParallelRunner::prepareProcessArguments(
    int chunkIndex,
    const QStringList &chunkData,
    const QString &tempOutputPath) {

  Q_UNUSED(chunkIndex);  // Not used in current implementation

  QStringList arguments;
  arguments << "--compilation-database" << compilationDatabasePath_;
  arguments << "--output" << tempOutputPath;
  if (!clangResourceDir_.isEmpty()) {
    arguments << "--clang-resource-dir" << clangResourceDir_;
  }

  // Add source files for this chunk
  for (const QString& sourceFile : chunkData) {
    arguments << "--source" << sourceFile;
  }

  return arguments;
}

bool QueryDependenciesParallelRunner::mergeResults(
    const QStringList &tempOutputPaths,
    const QString &finalOutputPath,
    QString &errorMessage) {

  // Aggregate data
  QJsonArray mergedFiles;
  QJsonArray mergedErrors;
  int totalCount = 0;
  int successCount = 0;
  int failureCount = 0;
  int totalHeaderCount = 0;

  // Load and merge each chunk
  for (const QString& tempPath : tempOutputPaths) {
    QFile file(tempPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qDebug() << "Warning: Could not open chunk file:" << tempPath;
      continue;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
      qDebug() << "Warning: Could not parse chunk file:" << tempPath
               << "Error:" << parseError.errorString();
      continue;
    }

    QJsonObject chunkObj = doc.object();

    // Merge statistics
    if (chunkObj.contains("statistics")) {
      QJsonObject stats = chunkObj["statistics"].toObject();
      totalCount += stats["totalCount"].toInt();
      successCount += stats["successCount"].toInt();
      failureCount += stats["failureCount"].toInt();
      totalHeaderCount += stats["totalHeaderCount"].toInt();
    }

    // Merge files array
    if (chunkObj.contains("files")) {
      QJsonArray filesArray = chunkObj["files"].toArray();
      for (const QJsonValue& val : filesArray) {
        mergedFiles.append(val);
      }
    }

    // Merge errors array
    if (chunkObj.contains("errors")) {
      QJsonArray errorsArray = chunkObj["errors"].toArray();
      for (const QJsonValue& val : errorsArray) {
        mergedErrors.append(val);
      }
    }
  }

  // Build final JSON
  QJsonObject finalObj;
  finalObj["statistics"] = QJsonObject{
      {"totalCount", totalCount},
      {"successCount", successCount},
      {"failureCount", failureCount},
      {"totalHeaderCount", totalHeaderCount}
  };
  finalObj["files"] = mergedFiles;

  if (!mergedErrors.isEmpty()) {
    finalObj["errors"] = mergedErrors;
  }

  // Write to final output file
  QFile outputFile(finalOutputPath);
  if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    errorMessage = QString("Cannot open output file: %1")
                       .arg(outputFile.errorString());
    return false;
  }

  QJsonDocument finalDoc(finalObj);
  outputFile.write(finalDoc.toJson(QJsonDocument::Indented));
  outputFile.close();

  return true;
}

void QueryDependenciesParallelRunner::onAllCompleted(
    int successCount, int failureCount, int totalCount) {

  // Call base class implementation (does merging)
  ParallelProcessRunner::onAllCompleted(successCount, failureCount, totalCount);

  // If merge failed, base class already emitted error
  // Check if final output exists
  QFile outputFile(finalOutputPath_);
  if (!outputFile.exists()) {
    return;  // Error already emitted
  }

  // Read and parse final merged JSON
  if (!outputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    emit error(QString("Failed to read merged output: %1")
                   .arg(outputFile.errorString()));
    return;
  }

  QByteArray jsonData = outputFile.readAll();
  outputFile.close();

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    emit error(QString("Failed to parse merged JSON: %1")
                   .arg(parseError.errorString()));
    return;
  }

  QJsonObject jsonObj = doc.object();

  // Emit domain-specific signals
  if (!errorMessages_.empty() ||
      (jsonObj.contains("errors") && !jsonObj["errors"].toArray().isEmpty())) {
    emit dependenciesReadyWithErrors(jsonObj, errorMessages_);
  } else {
    const double seconds = elapsed_.isValid()
                               ? static_cast<double>(elapsed_.elapsed()) / 1000.0
                               : 0.0;
    emit progress(
        QString("query-dependencies (parallel): %1s")
            .arg(QString::number(seconds, 'f', 2)));
    emit dependenciesReady(jsonObj);
  }
}

} // namespace acav
