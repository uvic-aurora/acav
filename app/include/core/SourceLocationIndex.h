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

/// \file SourceLocationIndex.h
/// \brief Source-to-AST node lookup index.
#pragma once

#include "common/FileManager.h"
#include <map>
#include <vector>

namespace acav {

// Forward declarations
class AstViewNode;

/// \brief Represents an interval in source code with associated AST node
struct Interval {
  unsigned beginLine;
  unsigned beginColumn;
  unsigned endLine;
  unsigned endColumn;
  AstViewNode *node; // Not owned - AstContext owns all nodes

  /// \brief Check if interval contains a point
  /// \param line Line number (1-based)
  /// \param column Column number (1-based)
  /// \return true if point is within interval (inclusive)
  bool contains(unsigned line, unsigned column) const;

  /// \brief Compare intervals by start position for sorting
  bool operator<(const Interval &other) const;
};

/// \brief Sorted array for interval queries
///
/// Stores source ranges in a sorted vector for efficient point queries.
/// Uses binary search for lookup.
///
/// Complexity:
/// - Insert: O(1) amortized (vector push_back)
/// - Finalize: O(n log n) (one-time sort)
/// - Query: O(log n + k) where k = number of matches
class IntervalTree {
public:
  IntervalTree() = default;
  ~IntervalTree() = default;

  // Non-copyable
  IntervalTree(const IntervalTree &) = delete;
  IntervalTree &operator=(const IntervalTree &) = delete;

  /// \brief Add interval to collection (not sorted yet)
  /// \param interval The source range and associated node
  void insert(Interval interval);

  /// \brief Sort intervals by start position (call once after all inserts)
  void finalize();

  /// \brief Find all intervals containing a point
  /// \param line Source line number (1-based)
  /// \param column Source column number (1-based)
  /// \return Vector of nodes sorted by depth (deepest first)
  std::vector<AstViewNode *> query(unsigned line, unsigned column) const;

  /// \brief Find first interval fully contained within a range
  /// \param beginLine Range start line (1-based)
  /// \param beginColumn Range start column (1-based)
  /// \param endLine Range end line (1-based)
  /// \param endColumn Range end column (1-based)
  /// \return First node whose interval is inside the range, or nullptr
  AstViewNode *queryFirstContained(unsigned beginLine, unsigned beginColumn,
                                   unsigned endLine, unsigned endColumn) const;

  /// \brief Get total number of intervals
  std::size_t size() const { return intervals_.size(); }

private:
  std::vector<Interval> intervals_;
  bool finalized_ = false;

  unsigned getDepth(AstViewNode *node) const;
};

/// \brief Index for source-to-AST node lookup
///
/// Maintains one IntervalTree per FileID. Built during AST extraction.
/// Owned by AstContext, destroyed when TU is closed.
class SourceLocationIndex {
public:
  SourceLocationIndex() = default;
  ~SourceLocationIndex() = default;

  // Non-copyable (manages interval trees)
  SourceLocationIndex(const SourceLocationIndex &) = delete;
  SourceLocationIndex &operator=(const SourceLocationIndex &) = delete;

  /// \brief Add node to index
  /// \param node AST node with valid source range
  /// \pre node->getSourceRange().begin().fileID() != 0 (valid)
  void addNode(AstViewNode *node);

  /// \brief Finalize all interval trees (call after all nodes added)
  void finalize();

  /// \brief Query nodes at specific source position
  /// \param fileId File identifier
  /// \param line Line number (1-based)
  /// \param column Column number (1-based)
  /// \return Vector of matching nodes, sorted deepest-first
  std::vector<AstViewNode *> getNodesAt(FileID fileId, unsigned line,
                                         unsigned column) const;

  /// \brief Query first node fully contained within a range
  /// \param fileId File identifier
  /// \param beginLine Range start line (1-based)
  /// \param beginColumn Range start column (1-based)
  /// \param endLine Range end line (1-based)
  /// \param endColumn Range end column (1-based)
  /// \return First node contained in the range, or nullptr
  AstViewNode *getFirstNodeContainedInRange(FileID fileId, unsigned beginLine,
                                            unsigned beginColumn,
                                            unsigned endLine,
                                            unsigned endColumn) const;

  /// \brief Get number of files in index
  std::size_t getFileCount() const { return trees_.size(); }

  /// \brief Get total number of intervals across all files
  std::size_t getTotalIntervals() const;

  /// \brief Check whether the current TU AST contains any indexed nodes for a file.
  bool hasFile(FileID fileId) const;

private:
  std::map<FileID, IntervalTree> trees_; // One tree per file
};

} // namespace acav
