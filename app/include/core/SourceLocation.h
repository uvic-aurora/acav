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

/// \file SourceLocation.h
/// \brief Source code location representation.
#pragma once

#include "common/FileManager.h"

// Forward declarations for Clang types
namespace clang {
class SourceLocation;
class SourceManager;
class SourceRange;
} // namespace clang

namespace acav {

/// \brief Represents a specific position in source code
///
/// Uses FileID (compact integer) + line + column coordinates.
/// FileID 0 = invalid (for compiler-generated code).
///
/// Memory layout: 12 bytes (4 + 4 + 4)
class SourceLocation {
public:
  SourceLocation() = delete;
  SourceLocation(FileID fileId, unsigned line, unsigned column);

  FileID fileID() const { return fileId_; }
  unsigned line() const { return line_; }
  unsigned column() const { return column_; }
  bool isValid() const { return fileId_ != FileManager::InvalidFileID; }

  /// \brief Create SourceLocation from Clang's SourceLocation
  /// \param loc Clang source location
  /// \param sm Clang source manager
  /// \param fileMgr FileManager for path registration
  /// \return ACAV SourceLocation (may be invalid if loc is invalid)
  static SourceLocation fromClang(const clang::SourceLocation &loc,
                                   const clang::SourceManager &sm,
                                   FileManager &fileMgr);

  /// \brief Reset internal caches (per extraction run).
  static void resetCache();

private:
  FileID fileId_ = 0;  // File identifier (0 = invalid)
  unsigned line_;      // 1-based line number
  unsigned column_;    // 1-based column number
};

/// \brief Represents a span of source code (begin to end)
///
/// Memory layout: 24 bytes (2 × SourceLocation)
class SourceRange {
public:
  SourceRange() = delete;
  SourceRange(SourceLocation begin, SourceLocation end);

  SourceLocation begin() const { return begin_; }
  SourceLocation end() const { return end_; }

  /// \brief Create SourceRange from Clang's SourceRange
  /// \param range Clang source range
  /// \param sm Clang source manager
  /// \param fileMgr FileManager for path registration
  /// \return ACAV SourceRange
  static SourceRange fromClang(const clang::SourceRange &range,
                                const clang::SourceManager &sm,
                                FileManager &fileMgr);

  /// \brief Reset internal caches (per extraction run).
  static void resetCache();

private:
  SourceLocation begin_;
  SourceLocation end_;
};

} // namespace acav
