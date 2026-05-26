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
#include <utility>

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
// IntervalTree Implementation
// ============================================================================

IntervalTree::SourcePoint IntervalTree::beginPoint(const Interval &interval) {
  return {interval.beginLine, interval.beginColumn};
}

IntervalTree::SourcePoint IntervalTree::endPoint(const Interval &interval) {
  return {interval.endLine, interval.endColumn};
}

bool IntervalTree::pointLess(SourcePoint lhs, SourcePoint rhs) {
  if (lhs.line != rhs.line) {
    return lhs.line < rhs.line;
  }
  return lhs.column < rhs.column;
}

bool IntervalTree::startsAfter(const Interval &interval, SourcePoint point) {
  return pointLess(point, beginPoint(interval));
}

bool IntervalTree::endsBefore(const Interval &interval, SourcePoint point) {
  return pointLess(endPoint(interval), point);
}

bool IntervalTree::intervalEndGreater(const Interval &lhs,
                                      const Interval &rhs) {
  SourcePoint lhsEnd = endPoint(lhs);
  SourcePoint rhsEnd = endPoint(rhs);
  if (lhsEnd.line != rhsEnd.line) {
    return lhsEnd.line > rhsEnd.line;
  }
  if (lhsEnd.column != rhsEnd.column) {
    return lhsEnd.column > rhsEnd.column;
  }
  return pointLess(beginPoint(rhs), beginPoint(lhs));
}

void IntervalTree::insert(Interval interval) {
  intervals_.push_back(interval);
}

void IntervalTree::finalize() {
  if (finalized_) {
    return;
  }

  // Sort intervals by start position
  std::sort(intervals_.begin(), intervals_.end());
  queryRoot_ = buildQueryTree(intervals_);
  finalized_ = true;
}

std::vector<AstViewNode *> IntervalTree::query(unsigned line,
                                                unsigned column) const {
  std::vector<AstViewNode *> results;

  queryPoint(queryRoot_.get(), SourcePoint{line, column}, results);

  // Sort by depth (deepest first = most specific)
  std::sort(results.begin(), results.end(),
            [this](AstViewNode *a, AstViewNode *b) {
              return getDepth(a) > getDepth(b);
            });

  return results;
}

std::unique_ptr<IntervalTree::QueryNode>
IntervalTree::buildQueryTree(std::vector<Interval> intervals) {
  if (intervals.empty()) {
    return nullptr;
  }

  SourcePoint center = beginPoint(intervals[intervals.size() / 2]);
  std::vector<Interval> left;
  std::vector<Interval> right;
  std::vector<Interval> crossing;
  left.reserve(intervals.size() / 2);
  right.reserve(intervals.size() / 2);
  crossing.reserve(intervals.size());

  for (const Interval &interval : intervals) {
    if (endsBefore(interval, center)) {
      left.push_back(interval);
    } else if (startsAfter(interval, center)) {
      right.push_back(interval);
    } else {
      crossing.push_back(interval);
    }
  }

  auto node = std::make_unique<QueryNode>();
  node->center = center;
  node->byBegin = std::move(crossing);
  node->byEndDescending = node->byBegin;
  std::sort(node->byEndDescending.begin(), node->byEndDescending.end(),
            intervalEndGreater);
  node->left = buildQueryTree(std::move(left));
  node->right = buildQueryTree(std::move(right));
  return node;
}

void IntervalTree::queryPoint(const QueryNode *node, SourcePoint point,
                              std::vector<AstViewNode *> &results) {
  if (!node) {
    return;
  }

  if (pointLess(point, node->center)) {
    for (const Interval &interval : node->byBegin) {
      if (startsAfter(interval, point)) {
        break;
      }
      results.push_back(interval.node);
    }
    queryPoint(node->left.get(), point, results);
    return;
  }

  if (pointLess(node->center, point)) {
    for (const Interval &interval : node->byEndDescending) {
      if (endsBefore(interval, point)) {
        break;
      }
      results.push_back(interval.node);
    }
    queryPoint(node->right.get(), point, results);
    return;
  }

  for (const Interval &interval : node->byBegin) {
    results.push_back(interval.node);
  }
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

  bool endsBeforeStart =
      range.end().line() < range.begin().line() ||
      (range.end().line() == range.begin().line() &&
       range.end().column() < range.begin().column());
  if (endsBeforeStart) {
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
