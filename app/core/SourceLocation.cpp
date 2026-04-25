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

#include "core/SourceLocation.h"
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <llvm/ADT/DenseMap.h>
#include <optional>
#include <utility>

namespace acav {

namespace {
// Cache for spelling locations to SourceLocation components.
struct CachedLoc {
  FileID fileId = 0;
  unsigned offset = 0; // offset from start of file (spelling)
  unsigned line = 0;
  unsigned column = 0;
};

using LocCache = llvm::DenseMap<uintptr_t, CachedLoc>;
using FileEntryCache = llvm::DenseMap<const clang::FileEntry *, FileID>;
using EndTokenCache = llvm::DenseMap<uintptr_t, clang::SourceLocation>;

// Thread-local caches avoid locking overhead during single-threaded AST walks.
thread_local LocCache locCache;
thread_local FileEntryCache fileEntryCache;
thread_local EndTokenCache endTokenCache;

uintptr_t toKey(const clang::SourceLocation &loc) {
  return static_cast<uintptr_t>(loc.getRawEncoding());
}

clang::SourceLocation resolveFileLoc(const clang::SourceLocation &loc,
                                     const clang::SourceManager &sm) {
  return loc.isInvalid() ? loc : sm.getFileLoc(loc);
}
} // namespace

SourceLocation::SourceLocation(FileID fileId, unsigned line, unsigned column)
    : fileId_(fileId), line_(line), column_(column) {}

SourceLocation SourceLocation::fromClang(const clang::SourceLocation &loc,
                                          const clang::SourceManager &sm,
                                          FileManager &fileMgr) {
  if (loc.isInvalid()) {
    return SourceLocation(FileManager::InvalidFileID, 0, 0);
  }

  // Resolve macro locations to the file location of the expansion.
  clang::SourceLocation fileLoc = resolveFileLoc(loc, sm);
  if (fileLoc.isInvalid()) {
    return SourceLocation(FileManager::InvalidFileID, 0, 0);
  }

  const uintptr_t key = toKey(fileLoc);
  if (auto it = locCache.find(key); it != locCache.end()) {
    return SourceLocation(it->second.fileId, it->second.line, it->second.column);
  }

  // Decompose spelling location once to avoid redundant SourceManager queries.
  auto decomposed = sm.getDecomposedSpellingLoc(fileLoc);
  clang::FileID fid = decomposed.first;
  unsigned offset = decomposed.second;
  if (!fid.isValid()) {
    return SourceLocation(FileManager::InvalidFileID, 0, 0);
  }

  // Get the FileEntry for the spelling location
  auto fileEntry = sm.getFileEntryRefForID(fid);
  if (!fileEntry) {
    return SourceLocation(FileManager::InvalidFileID, 0, 0);
  }

  // FileEntry to FileID cache
  const clang::FileEntry *fe = &fileEntry->getFileEntry();
  auto feIt = fileEntryCache.find(fe);
  FileID fileId;
  if (feIt != fileEntryCache.end()) {
    fileId = feIt->second;
  } else {
    // Register file and get FileID
    const char *filename = fileEntry->getName().data();
    fileId =
        fileMgr.tryGetFileId(filename).value_or(FileManager::InvalidFileID);
    if (fileId == FileManager::InvalidFileID) {
      fileId = fileMgr.registerFile(filename);
    }
    fileEntryCache.try_emplace(fe, fileId);
  }

  // Get line and column (1-based) using decomposed offset to avoid extra work.
  unsigned line = sm.getLineNumber(fid, offset);
  unsigned column = sm.getColumnNumber(fid, offset);

  locCache.try_emplace(key, CachedLoc{fileId, offset, line, column});

  return SourceLocation(fileId, line, column);
}

SourceRange::SourceRange(SourceLocation begin, SourceLocation end)
    : begin_(begin), end_(end) {}

SourceRange SourceRange::fromClang(const clang::SourceRange &range,
                                    const clang::SourceManager &sm,
                                    FileManager &fileMgr) {
  if (range.isInvalid()) {
    SourceLocation invalid(FileManager::InvalidFileID, 0, 0);
    return SourceRange(invalid, invalid);
  }

  // Resolve macro locations to file locations at expansion sites.
  clang::SourceLocation startLoc = resolveFileLoc(range.getBegin(), sm);
  clang::SourceLocation lastTokenLoc = resolveFileLoc(range.getEnd(), sm);

  // Get the actual end location (after the last token)
  clang::SourceLocation endLoc;

  // Validate that lastTokenLoc is safe to use with getLocForEndOfToken
  // The lexer can crash if the location is at or past the end of a file buffer
  bool canUseGetLocForEndOfToken = false;
  if (lastTokenLoc.isValid() && lastTokenLoc.isFileID()) {
    clang::FileID fid = sm.getFileID(lastTokenLoc);
    if (fid.isValid()) {
      auto buffer = sm.getBufferOrNone(fid);
      if (buffer) {
        unsigned offset = sm.getFileOffset(lastTokenLoc);
        // Ensure we're not at or past the end of the buffer
        // Leave some margin for the lexer to safely read ahead
        if (offset < buffer->getBufferSize()) {
          canUseGetLocForEndOfToken = true;
        }
      }
    }
  }

  if (canUseGetLocForEndOfToken) {
    if (auto it = endTokenCache.find(toKey(lastTokenLoc));
        it != endTokenCache.end()) {
      endLoc = it->second;
    } else {
      clang::LangOptions langOpts;
      endLoc = clang::Lexer::getLocForEndOfToken(lastTokenLoc, 0, sm, langOpts);
      endTokenCache.try_emplace(toKey(lastTokenLoc), endLoc);
    }
  }

  // If getLocForEndOfToken fails or was skipped, fall back to lastTokenLoc
  if (endLoc.isInvalid()) {
    endLoc = lastTokenLoc;
  }

  SourceLocation begin = SourceLocation::fromClang(startLoc, sm, fileMgr);
  SourceLocation end = SourceLocation::fromClang(endLoc, sm, fileMgr);

  return SourceRange(begin, end);
}

static void resetAllCaches() {
  locCache.clear();
  fileEntryCache.clear();
  endTokenCache.clear();
}

void SourceLocation::resetCache() { resetAllCaches(); }

} // namespace acav
