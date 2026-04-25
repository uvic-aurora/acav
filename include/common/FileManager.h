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

/// \file FileManager.h
/// \brief Centralized file registry with path-to-FileID mapping.
#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace acav {

/// Type-safe identifier for registered files. 0 is reserved for invalid.
using FileID = std::size_t;

/// \brief Centralized file registry providing path-to-FileID mapping.
///
/// Implements memory optimization similar to LLVM's StringRef pattern:
/// AST nodes store FileID references instead of full paths.
/// Thread-safe with path normalization and reference counting.
class FileManager {
public:
  static constexpr FileID InvalidFileID = 0; ///< Reserved invalid FileID.

  FileManager() = default;
  ~FileManager() = default;

  FileManager(const FileManager &) = delete;
  FileManager &operator=(const FileManager &) = delete;
  FileManager(FileManager &&) = delete;
  FileManager &operator=(FileManager &&) = delete;

  /// \brief Register a file and return its FileID.
  /// \param filePath Path to the file (will be normalized).
  /// \return FileID (1-based), or existing ID if already registered.
  FileID registerFile(std::string_view filePath);

  /// \brief Look up FileID for a path without registering.
  /// \param filePath Path to look up (will be normalized).
  /// \return FileID if found, std::nullopt otherwise.
  std::optional<FileID> tryGetFileId(std::string_view filePath) const;

  /// \brief Get the canonical path for a FileID.
  /// \param id FileID to look up.
  /// \return Path string_view, or empty if invalid. Valid until destruction.
  std::string_view getFilePath(FileID id) const;

  /// \brief Get reference count for a file.
  /// \param id FileID to query.
  /// \return Number of times registerFile() was called for this ID.
  std::size_t getRefCount(FileID id) const;

  /// \brief Get total number of registered files.
  std::size_t getRegisteredFileCount() const;

  /// \brief Check if a FileID is valid.
  bool isValidFileId(FileID id) const;

private:
  // Internal file record
  struct FileRecord {
    std::string path;        // Normalized canonical path
    std::size_t refCount = 0; // Number of times registered
  };

  // Normalize file path to canonical form
  // Handles: relative paths, symbolic links, ".." and "." components
  // Returns: Canonical absolute path as string
  std::string normalizePath(std::string_view filePath) const;

  mutable std::mutex mutex_;                      // Thread-safety lock
  std::vector<FileRecord> files_;                 // Indexed by FileID - 1
  std::unordered_map<std::string, FileID> pathToId_; // Fast lookup cache
};

} // namespace acav
