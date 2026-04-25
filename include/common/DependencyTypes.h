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

/// \file DependencyTypes.h
/// \brief Common data structures for dependency information shared across
/// tools.
#pragma once

#include <string>
#include <vector>

namespace acav {

/// \brief Represents information about a single included header file
struct HeaderInfo {
  std::string path_;          ///< Full path to the header file
  bool direct_;               ///< True if directly included at least once,
                              ///< even if also indirectly included
  std::string inclusionKind_; ///< Type of inclusion (C_User, C_System,
                              ///< C_ExternCSystem, etc.)
};

/// \brief Structure to store dependency information for a single source file
/// This structure captures all header files included by a source file.
///  each entry has:
///       - path: path of source file
///       - headers: list of HeaderInfo
///           - path: path of header
///           - direct (bool): directly included (as opposed to indirect)
///           - inclusionKind (string): type of inclusion
struct FileDependencies {
  std::string path;                ///< Path to the source file
  std::vector<HeaderInfo> headers; ///< List of all included headers
};

/// \brief Represents an error that occurred during dependency analysis
struct ProcessingError {
  std::string filePath_;     ///< Source file that failed to process
  std::string errorMessage_; ///< Error description from Clang
};

} // namespace acav
