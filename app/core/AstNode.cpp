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

#include "core/AstNode.h"
#include "core/MemoryProfiler.h"

#ifdef ACAV_ENABLE_STRING_STATS
#include <iomanip>
#include <iostream>
#endif

namespace acav {

AstNode::AstNode(AcavJson properties, const SourceRange &sourceRange)
    : properties_(std::move(properties)), sourceRange_(sourceRange) {}

AstViewNode::AstViewNode(AstNode *node) : node_(node) {
  if (node_) {
    node_->hold();
  }
}

AstViewNode::~AstViewNode() {
  // Release reference to data node
  if (node_) {
    node_->release();
  }

  // Clear children vector but don't delete
  // AstContext owns and will destroy them
  children_.clear();
}

void AstViewNode::addChild(AstViewNode *child) {
  if (child) {
    child->setParent(this);
    children_.push_back(child);
  }
}

// AstContext implementation

AstContext::~AstContext() {
  MemoryProfiler::checkpoint("AstContext dtor: START");
  
  // Delete AstViewNodes first (they hold references to AstNodes)
  MemoryProfiler::checkpoint("AstContext dtor: Before deleting ViewNodes");
  for (AstViewNode *viewNode : allAstViewNodes_) {
    delete viewNode;
  }
  allAstViewNodes_.clear();
  allAstViewNodes_.shrink_to_fit();  // Release vector capacity!
  MemoryProfiler::checkpoint("AstContext dtor: After deleting ViewNodes");

  // Then delete AstNodes
  MemoryProfiler::checkpoint("AstContext dtor: Before deleting AstNodes");
  for (AstNode *node : allAstNodes_) {
    delete node;
  }
  allAstNodes_.clear();
  allAstNodes_.shrink_to_fit();  // Release vector capacity!
  MemoryProfiler::checkpoint("AstContext dtor: After deleting AstNodes");

  // Clear deduplication map (pointers are already deleted)
  typeDeduplicationMap_.clear();
  // Note: unordered_map also keeps capacity, but typically smaller overhead
  
  MemoryProfiler::checkpoint("AstContext dtor: END");
}

AstNode *AstContext::createAstNode(AcavJson properties,
                                   const SourceRange &range) {
  AstNode *node = new AstNode(std::move(properties), range);
  allAstNodes_.push_back(node);
  return node;
}

AstViewNode *AstContext::createAstViewNode(AstNode *node) {
  AstViewNode *viewNode = new AstViewNode(node);
  allAstViewNodes_.push_back(viewNode);
  return viewNode;
}

AstNode *AstContext::getOrCreateTypeNode(const void *typePtr,
                                          AcavJson properties,
                                          const SourceRange &range) {
#ifdef ACAV_ENABLE_STRING_STATS
  ++typeLookupCount_;
#endif

  // Check if node for this type already exists
  auto it = typeDeduplicationMap_.find(typePtr);
  if (it != typeDeduplicationMap_.end()) {
#ifdef ACAV_ENABLE_STRING_STATS
    ++typeCacheHits_;
#endif
    return it->second; // Return existing deduplicated node
  }

  // Create new node
  AstNode *node = createAstNode(std::move(properties), range);

  // Store in deduplication map
  typeDeduplicationMap_[typePtr] = node;

  return node;
}

AstNode *AstContext::findTypeNode(const void *typePtr) const {
  auto it = typeDeduplicationMap_.find(typePtr);
  if (it != typeDeduplicationMap_.end()) {
    return it->second;
  }
  return nullptr;
}

void AstContext::indexNode(AstViewNode *node) {
  if (node) {
    locationIndex_.addNode(node);
  }
}

void AstContext::finalizeLocationIndex() {
  locationIndex_.finalize();
}

#ifdef ACAV_ENABLE_STRING_STATS

TypeDeduplicationStats AstContext::getTypeDeduplicationStats() const {
  TypeDeduplicationStats stats{};

  stats.totalTypeLookups = typeLookupCount_;
  stats.cacheHits = typeCacheHits_;
  stats.uniqueTypeNodes = typeDeduplicationMap_.size();

  // Count total references to type nodes (sum of refCounts)
  std::size_t totalRefs = 0;
  std::size_t totalNodeBytes = 0;
  for (const auto &[ptr, node] : typeDeduplicationMap_) {
    totalRefs += node->getUseCount();
    // Estimate node size: AstNode object + JSON properties overhead
    totalNodeBytes += sizeof(AstNode) + node->getProperties().dump().size();
  }
  stats.totalTypeReferences = totalRefs;

  // Average bytes per type node
  std::size_t avgNodeBytes = stats.uniqueTypeNodes > 0
      ? totalNodeBytes / stats.uniqueTypeNodes
      : 256; // Default estimate

  // The REAL savings: multiple AstViewNodes share the same AstNode
  // Without sharing: each of the 420017 references would need its own AstNode
  // With sharing: we only have 28805 AstNodes
  stats.estimatedSavedNodes = (totalRefs > stats.uniqueTypeNodes)
      ? (totalRefs - stats.uniqueTypeNodes)
      : 0;

  // Memory saved = nodes avoided * average node size
  stats.estimatedSavedBytes = stats.estimatedSavedNodes * avgNodeBytes;

  // Deduplication ratio: how many ViewNodes share each AstNode on average
  stats.deduplicationRatio = stats.uniqueTypeNodes > 0
      ? static_cast<double>(stats.totalTypeReferences) / stats.uniqueTypeNodes
      : 0.0;

  // Savings percentage: what fraction of memory was saved
  std::size_t wouldHaveUsed = totalRefs * avgNodeBytes;  // Without sharing
  std::size_t actuallyUsed = totalNodeBytes;              // With sharing
  stats.savingsPercent = wouldHaveUsed > 0
      ? 100.0 * static_cast<double>(wouldHaveUsed - actuallyUsed) / wouldHaveUsed
      : 0.0;

  return stats;
}

void AstContext::printTypeDeduplicationStats(const char *label) const {
  auto stats = getTypeDeduplicationStats();

  std::cerr << "\n";
  std::cerr << "========== Type Node Sharing Statistics";
  if (label) {
    std::cerr << " [" << label << "]";
  }
  std::cerr << " ==========\n";
  std::cerr << std::fixed << std::setprecision(2);
  std::cerr << "  Unique type AstNodes:      " << stats.uniqueTypeNodes << "\n";
  std::cerr << "  AstViewNode references:    " << stats.totalTypeReferences << "\n";
  std::cerr << "  Nodes avoided by sharing:  " << stats.estimatedSavedNodes << "\n";
  std::cerr << "  Estimated memory saved:    " << stats.estimatedSavedBytes / 1024.0 << " KB\n";
  std::cerr << "  Sharing ratio:             " << stats.deduplicationRatio << "x\n";
  std::cerr << "  Savings percentage:        " << stats.savingsPercent << "%\n";
  std::cerr << "=======================================================\n\n";
}

#endif // ACAV_ENABLE_STRING_STATS

} // namespace acav
