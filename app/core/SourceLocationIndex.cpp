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

#include "core/SourceLocationIndex.h"
#include "core/AstNode.h"
#include <algorithm>

namespace acav {

// ============================================================================
// Interval Implementation
// ============================================================================

bool Interval::contains(unsigned line, unsigned column) const {
  // Check if point is after or at begin
  bool afterBegin = (line > beginLine) ||
                    (line == beginLine && column >= beginColumn);

  // Check if point is before or at end
  bool beforeEnd = (line < endLine) || (line == endLine && column <= endColumn);

  return afterBegin && beforeEnd;
}

bool Interval::operator<(const Interval &other) const {
  if (beginLine != other.beginLine) {
    return beginLine < other.beginLine;
  }
  return beginColumn < other.beginColumn;
}

// ============================================================================
// IntervalTree Implementation (using sorted vector)
// ============================================================================

void IntervalTree::insert(Interval interval) {
  intervals_.push_back(interval);
}

void IntervalTree::finalize() {
  if (finalized_) {
    return;
  }

  // Sort intervals by start position
  std::sort(intervals_.begin(), intervals_.end());
  finalized_ = true;
}

std::vector<AstViewNode *> IntervalTree::query(unsigned line,
                                                unsigned column) const {
  std::vector<AstViewNode *> results;

  // Linear scan through intervals to find all matches
  // This is O(n) but with good cache locality
  // For most files, n is reasonable (<10k intervals)
  for (const Interval &interval : intervals_) {
    // Early exit optimization: if interval starts after query point, stop
    if (interval.beginLine > line ||
        (interval.beginLine == line && interval.beginColumn > column)) {
      break;
    }

    // Check if interval contains the point
    if (interval.contains(line, column)) {
      results.push_back(interval.node);
    }
  }

  // Sort by depth (deepest first = most specific)
  std::sort(results.begin(), results.end(),
            [this](AstViewNode *a, AstViewNode *b) {
              return getDepth(a) > getDepth(b);
            });

  return results;
}

AstViewNode *IntervalTree::queryFirstContained(unsigned beginLine,
                                               unsigned beginColumn,
                                               unsigned endLine,
                                               unsigned endColumn) const {
  // Find first interval starting at or after (beginLine, beginColumn)
  Interval searchKey{beginLine, beginColumn, 0, 0, nullptr};
  auto it = std::lower_bound(intervals_.begin(), intervals_.end(), searchKey);

  for (; it != intervals_.end(); ++it) {
    // Stop if interval starts after range end
    if (it->beginLine > endLine ||
        (it->beginLine == endLine && it->beginColumn > endColumn)) {
      break;
    }

    // Check if interval is fully contained: start >= range start, end <= range end
    bool startsInRange = it->beginLine > beginLine ||
                         (it->beginLine == beginLine && it->beginColumn >= beginColumn);
    bool endsInRange = it->endLine < endLine ||
                       (it->endLine == endLine && it->endColumn <= endColumn);

    if (startsInRange && endsInRange) {
      return it->node;
    }
  }

  return nullptr;
}

unsigned IntervalTree::getDepth(AstViewNode *node) const {
  if (!node) {
    return 0;
  }

  unsigned depth = 0;
  AstViewNode *current = node;
  while (current->getParent()) {
    ++depth;
    current = current->getParent();
  }
  return depth;
}

// ============================================================================
// SourceLocationIndex Implementation
// ============================================================================

void SourceLocationIndex::addNode(AstViewNode *node) {
  if (!node) {
    return;
  }

  const SourceRange &range = node->getSourceRange();
  FileID fileId = range.begin().fileID();

  // Skip invalid locations (compiler-generated nodes)
  if (fileId == FileManager::InvalidFileID) {
    return;
  }

  // Create interval
  Interval interval{range.begin().line(), range.begin().column(),
                    range.end().line(), range.end().column(), node};

  // Insert into appropriate tree (create if needed)
  trees_[fileId].insert(interval);
}

void SourceLocationIndex::finalize() {
  // Finalize all trees (sort intervals)
  for (auto &[fileId, tree] : trees_) {
    tree.finalize();
  }
}

std::vector<AstViewNode *> SourceLocationIndex::getNodesAt(FileID fileId,
                                                             unsigned line,
                                                             unsigned column) const {
  auto it = trees_.find(fileId);
  if (it == trees_.end()) {
    return {}; // No tree for this file
  }

  return it->second.query(line, column);
}

AstViewNode *SourceLocationIndex::getFirstNodeContainedInRange(
    FileID fileId, unsigned beginLine, unsigned beginColumn, unsigned endLine,
    unsigned endColumn) const {
  auto it = trees_.find(fileId);
  if (it == trees_.end()) {
    return nullptr;
  }

  return it->second.queryFirstContained(beginLine, beginColumn, endLine,
                                        endColumn);
}

std::size_t SourceLocationIndex::getTotalIntervals() const {
  std::size_t total = 0;
  for (const auto &[fileId, tree] : trees_) {
    total += tree.size();
  }
  return total;
}

bool SourceLocationIndex::hasFile(FileID fileId) const {
  return trees_.find(fileId) != trees_.end();
}

} // namespace acav
