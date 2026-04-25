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

/// \file ProcessOutputUtils.h
/// \brief Helpers for draining process output into log entries.
#pragma once

#include "core/LogEntry.h"
#include "core/ProcessLogParser.h"
#include <QDateTime>
#include <QHash>
#include <QProcess>
#include <functional>

namespace acav {

using LogEmitter = std::function<void(const LogEntry &)>;

inline void emitParsedOutput(QString &buffer, const QString &chunk,
                             const QString &source, bool isStdErr,
                             const LogEmitter &emitLog) {
  const QVector<LogEntry> entries =
      parseProcessOutput(buffer, chunk, source, isStdErr);
  for (const LogEntry &entry : entries) {
    emitLog(entry);
  }
}

inline void emitParsedOutput(QHash<QProcess *, QString> &buffers,
                             QProcess *process, const QString &chunk,
                             const QString &source, bool isStdErr,
                             const LogEmitter &emitLog) {
  if (!process) {
    return;
  }
  QString &buffer = buffers[process];
  emitParsedOutput(buffer, chunk, source, isStdErr, emitLog);
}

inline void drainProcessOutput(QProcess *process, QString &stdoutBuffer,
                               QString &stderrBuffer, const QString &source,
                               const LogEmitter &emitLog) {
  if (!process) {
    return;
  }

  const QString stdoutChunk =
      QString::fromUtf8(process->readAllStandardOutput());
  const QString stderrChunk =
      QString::fromUtf8(process->readAllStandardError());

  emitParsedOutput(stdoutBuffer, stdoutChunk, source, false, emitLog);
  emitParsedOutput(stderrBuffer, stderrChunk, source, true, emitLog);
  if (!stdoutBuffer.isEmpty()) {
    emitParsedOutput(stdoutBuffer, "\n", source, false, emitLog);
  }
  if (!stderrBuffer.isEmpty()) {
    emitParsedOutput(stderrBuffer, "\n", source, true, emitLog);
  }
}

inline void drainProcessOutput(QProcess *process, QString &stdoutBuffer,
                               QString &stderrBuffer, const QString &source,
                               const LogEmitter &emitLog,
                               QString *stdoutCapture,
                               QString *stderrCapture) {
  if (!process) {
    return;
  }

  const QString stdoutChunk =
      QString::fromUtf8(process->readAllStandardOutput());
  const QString stderrChunk =
      QString::fromUtf8(process->readAllStandardError());
  if (stdoutCapture) {
    stdoutCapture->append(stdoutChunk);
  }
  if (stderrCapture) {
    stderrCapture->append(stderrChunk);
  }

  emitParsedOutput(stdoutBuffer, stdoutChunk, source, false, emitLog);
  emitParsedOutput(stderrBuffer, stderrChunk, source, true, emitLog);
  if (!stdoutBuffer.isEmpty()) {
    emitParsedOutput(stdoutBuffer, "\n", source, false, emitLog);
  }
  if (!stderrBuffer.isEmpty()) {
    emitParsedOutput(stderrBuffer, "\n", source, true, emitLog);
  }
}

inline void drainProcessOutput(QProcess *process,
                               QHash<QProcess *, QString> &stdoutBuffers,
                               QHash<QProcess *, QString> &stderrBuffers,
                               const QString &source,
                               const LogEmitter &emitLog) {
  if (!process) {
    return;
  }
  QString &stdoutBuffer = stdoutBuffers[process];
  QString &stderrBuffer = stderrBuffers[process];
  drainProcessOutput(process, stdoutBuffer, stderrBuffer, source, emitLog);
}

inline void drainProcessOutput(QProcess *process,
                               QHash<QProcess *, QString> &stdoutBuffers,
                               QHash<QProcess *, QString> &stderrBuffers,
                               const QString &source,
                               const LogEmitter &emitLog,
                               QString *stdoutCapture,
                               QString *stderrCapture) {
  if (!process) {
    return;
  }
  QString &stdoutBuffer = stdoutBuffers[process];
  QString &stderrBuffer = stderrBuffers[process];
  drainProcessOutput(process, stdoutBuffer, stderrBuffer, source, emitLog,
                     stdoutCapture, stderrCapture);
}

inline void emitErrorLog(const QString &source, const QString &message,
                         const LogEmitter &emitLog) {
  LogEntry entry;
  entry.level = LogLevel::Error;
  entry.source = source;
  entry.message = message;
  entry.timestamp = QDateTime::currentDateTime();
  emitLog(entry);
}

} // namespace acav
