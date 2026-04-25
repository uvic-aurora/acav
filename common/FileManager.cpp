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

#include "common/FileManager.h"
#include <filesystem>
#include <stdexcept>

namespace acav {

FileID FileManager::registerFile(std::string_view filePath) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Normalize the path first
  std::string normalizedPath = normalizePath(filePath);

  // Check if already registered
  auto it = pathToId_.find(normalizedPath);
  if (it != pathToId_.end()) {
    // File already registered, increment ref count and return existing ID
    FileID id = it->second;
    files_[id - 1].refCount++;
    return id;
  }

  // New file - create record
  FileRecord record;
  record.path = std::move(normalizedPath);
  record.refCount = 1;

  // Assign FileID (starting from 1, since 0 is reserved for invalid)
  FileID newId = files_.size() + 1;

  // Store record and update mapping
  files_.push_back(std::move(record));
  pathToId_[files_.back().path] = newId;

  return newId;
}

std::optional<FileID> FileManager::tryGetFileId(std::string_view filePath) const {
  std::lock_guard<std::mutex> lock(mutex_);

  // Normalize path for lookup
  std::string normalizedPath = normalizePath(filePath);

  auto it = pathToId_.find(normalizedPath);
  if (it != pathToId_.end()) {
    return it->second;
  }

  return std::nullopt;
}

std::string_view FileManager::getFilePath(FileID id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!isValidFileId(id)) {
    static const std::string empty;
    return empty;
  }

  return files_[id - 1].path;
}

std::size_t FileManager::getRefCount(FileID id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!isValidFileId(id)) {
    return 0;
  }

  return files_[id - 1].refCount;
}

std::size_t FileManager::getRegisteredFileCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return files_.size();
}

bool FileManager::isValidFileId(FileID id) const {
  // Note: mutex should already be held by caller
  return id != InvalidFileID && id <= files_.size();
}

std::string FileManager::normalizePath(std::string_view filePath) const {
  try {
    // Convert to filesystem path
    std::filesystem::path fsPath(filePath);

    // Get canonical path (resolves symlinks, relative paths, etc.)
    // If file doesn't exist, use absolute path instead
    std::filesystem::path canonicalPath;

    if (std::filesystem::exists(fsPath)) {
      canonicalPath = std::filesystem::canonical(fsPath);
    } else {
      // File doesn't exist yet, just make it absolute
      canonicalPath = std::filesystem::absolute(fsPath);
      // Normalize by removing . and .. components
      canonicalPath = canonicalPath.lexically_normal();
    }

    return canonicalPath.string();
  } catch (const std::filesystem::filesystem_error &e) {
    // If filesystem operations fail, fall back to making the path absolute
    try {
      std::filesystem::path fsPath(filePath);
      return std::filesystem::absolute(fsPath).lexically_normal().string();
    } catch (...) {
      // Last resort: return as-is
      return std::string(filePath);
    }
  }
}

} // namespace acav
