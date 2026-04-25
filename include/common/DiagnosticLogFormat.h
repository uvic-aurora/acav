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

/// \file DiagnosticLogFormat.h
/// \brief Helpers to format diagnostics for log ingestion.
#pragma once

#include "common/ClangUtils.h"
#include <clang/Basic/Diagnostic.h>
#include <string>

namespace acav::logfmt {

inline std::string sanitizeField(std::string value) {
  for (char &ch : value) {
    if (ch == '\n' || ch == '\r' || ch == '\t') {
      ch = ' ';
    }
  }
  return value;
}

inline std::string toLevelString(clang::DiagnosticsEngine::Level level) {
  switch (level) {
  case clang::DiagnosticsEngine::Warning:
    return "warning";
  case clang::DiagnosticsEngine::Error:
    return "error";
  case clang::DiagnosticsEngine::Fatal:
    return "fatal";
  case clang::DiagnosticsEngine::Remark:
    return "remark";
  case clang::DiagnosticsEngine::Note:
    return "note";
  case clang::DiagnosticsEngine::Ignored:
  default:
    return "debug";
  }
}

inline std::string formatDiagnosticLine(const std::string &level,
                                        const std::string &file,
                                        unsigned line,
                                        unsigned column,
                                        const std::string &message) {
  std::string result = "@diag\t";
  result += level;
  result += '\t';
  result += sanitizeField(file);
  result += '\t';
  if (line > 0) {
    result += std::to_string(line);
  }
  result += '\t';
  if (column > 0) {
    result += std::to_string(column);
  }
  result += '\t';
  result += sanitizeField(message);
  return result;
}

inline std::string formatDiagnosticLine(clang::DiagnosticsEngine::Level level,
                                        const std::string &file,
                                        unsigned line,
                                        unsigned column,
                                        const std::string &message) {
  return formatDiagnosticLine(toLevelString(level), file, line, column, message);
}

} // namespace acav::logfmt
