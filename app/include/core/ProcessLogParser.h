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

#pragma once

#include "core/LogEntry.h"
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace acav {

struct ParsedLogLine {
  LogLevel level = LogLevel::Info;
  QString message;
  bool parsed = false;
};

inline LogLevel levelFromString(const QString &level) {
  const QString lower = level.toLower();
  if (lower == "info") {
    return LogLevel::Info;
  }
  if (lower == "fatal" || lower == "error") {
    return LogLevel::Error;
  }
  if (lower == "warning" || lower == "warn") {
    return LogLevel::Warning;
  }
  if (lower == "debug" || lower == "note" || lower == "remark") {
    return LogLevel::Debug;
  }
  return LogLevel::Info;
}

inline ParsedLogLine parseDiagnosticLine(const QString &line) {
  ParsedLogLine parsed;
  if (!line.startsWith("@diag\t")) {
    return parsed;
  }

  QString payload = line.mid(6);
  const QStringList parts = payload.split('\t');
  if (parts.size() < 2) {
    return parsed;
  }

  parsed.level = levelFromString(parts[0]);
  if (parts.size() >= 5) {
    const QString file = parts[1];
    const QString lineNo = parts[2];
    const QString colNo = parts[3];
    const QString message = parts.mid(4).join("\t");
    QString location = file;
    if (!lineNo.isEmpty()) {
      location += ":" + lineNo;
    }
    if (!colNo.isEmpty()) {
      location += ":" + colNo;
    }
    parsed.message = location.isEmpty() ? message
                                        : QString("%1: %2").arg(location, message);
  } else {
    parsed.message = parts.mid(1).join("\t");
  }

  parsed.parsed = true;
  return parsed;
}

inline bool isWordChar(QChar ch) {
  return ch.isLetterOrNumber() || ch == '_';
}

inline bool containsSeverityToken(const QString &lower,
                                  const QString &token) {
  int index = 0;
  while ((index = lower.indexOf(token, index)) != -1) {
    const int before = index - 1;
    const int after = index + token.size();
    const bool beforeOk =
        before < 0 || !isWordChar(lower.at(before));
    if (!beforeOk) {
      index = after;
      continue;
    }

    if (after >= lower.size()) {
      return true;
    }

    const QChar afterChar = lower.at(after);
    if (afterChar.isSpace() || afterChar == ':' || afterChar == ']' ||
        afterChar == ')' || afterChar == ',' || afterChar == ';') {
      return true;
    }

    if (afterChar == '.' && after + 1 >= lower.size()) {
      return true;
    }

    index = after;
  }

  return false;
}

inline LogLevel guessLevelFromText(const QString &line, bool isStdErr) {
  const QString lower = line.toLower();
  if (containsSeverityToken(lower, "fatal") ||
      containsSeverityToken(lower, "error")) {
    return LogLevel::Error;
  }
  if (containsSeverityToken(lower, "warning") ||
      containsSeverityToken(lower, "warn")) {
    return LogLevel::Warning;
  }
  if (containsSeverityToken(lower, "note") ||
      containsSeverityToken(lower, "remark") ||
      containsSeverityToken(lower, "debug")) {
    return LogLevel::Debug;
  }
  return isStdErr ? LogLevel::Warning : LogLevel::Info;
}

inline QVector<LogEntry> parseProcessOutput(QString &buffer,
                                            const QString &chunk,
                                            const QString &source,
                                            bool isStdErr) {
  QVector<LogEntry> entries;
  if (chunk.isEmpty()) {
    return entries;
  }

  constexpr int kMaxBufferSize = 64 * 1024;
  buffer += chunk;
  if (buffer.size() > kMaxBufferSize) {
    buffer = buffer.right(kMaxBufferSize);
  }
  const QStringList lines = buffer.split('\n');
  buffer = lines.isEmpty() ? QString() : lines.last();

  if (lines.size() <= 1) {
    return entries;
  }

  const QDateTime now = QDateTime::currentDateTime();
  entries.reserve(lines.size() - 1);
  for (int i = 0; i < lines.size() - 1; ++i) {
    const QString line = lines.at(i).trimmed();
    if (line.isEmpty()) {
      continue;
    }

    ParsedLogLine parsed = parseDiagnosticLine(line);
    LogEntry entry;
    entry.level =
        parsed.parsed ? parsed.level : guessLevelFromText(line, isStdErr);
    entry.source = source;
    entry.message = parsed.parsed ? parsed.message : line;
    entry.timestamp = now;
    entries.append(entry);
  }

  return entries;
}

} // namespace acav
