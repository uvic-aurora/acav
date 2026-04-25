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

/// \file AstNode.h
/// \brief AST data structures and memory management.
#pragma once

#include "common/InternedString.h"
#include "core/SourceLocation.h"
#include "core/SourceLocationIndex.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>

namespace acav {

#ifdef ACAV_ENABLE_STRING_STATS
/// \brief Statistics about type node deduplication.
struct TypeDeduplicationStats {
  std::size_t totalTypeLookups;      ///< Total calls to getOrCreateTypeNode
  std::size_t cacheHits;             ///< Times an existing node was reused
  std::size_t uniqueTypeNodes;       ///< Number of unique type nodes created
  std::size_t totalTypeReferences;   ///< Total AstViewNodes referencing type nodes
  std::size_t estimatedSavedNodes;   ///< Nodes avoided by deduplication
  std::size_t estimatedSavedBytes;   ///< Approximate memory saved
  double deduplicationRatio;         ///< Average references per unique type
  double savingsPercent;             ///< Percentage of memory saved
};
#endif

/// Custom JSON type using InternedString for automatic string deduplication.
using AcavJson =
    nlohmann::basic_json<std::map, std::vector, InternedString, bool, int64_t,
                         uint64_t, double, std::allocator,
                         nlohmann::adl_serializer, std::vector<uint8_t>>;

// Forward declarations
class AstNode;
class AstViewNode;

/// \brief Memory manager for AST nodes in a translation unit
///
/// Owns and tracks all AstNode and AstViewNode objects for one TU.
/// Provides factory methods for node creation.
/// Handles cleanup to avoid stack overflow on deep trees.
/// One AstContext per translation unit, destroyed when TU is closed.
class AstContext {
public:
  AstContext() = default;
  ~AstContext();

  AstContext(const AstContext &) = delete;
  AstContext &operator=(const AstContext &) = delete;
  AstContext(AstContext &&) = delete;
  AstContext &operator=(AstContext &&) = delete;

  /// \brief Create a new AST data node.
  /// \param properties Node metadata (kind, name, type, etc.).
  /// \param range Source location range.
  /// \return Owned pointer (context manages lifetime).
  AstNode *createAstNode(AcavJson properties, const SourceRange &range);

  /// \brief Create a new AST view node wrapping data.
  /// \param node Data node to wrap.
  /// \return Owned pointer (context manages lifetime).
  AstViewNode *createAstViewNode(AstNode *node);

  /// \brief Get or create deduplicated Type node
  ///
  /// Uses canonical Type pointer as identity key. Multiple QualTypes
  /// referring to the same canonical type will map to the same AstNode.
  ///
  /// Example:
  ///   const int* p1;  // QualType 1
  ///   int const* p2;  // QualType 2
  ///   // Both have same canonical type → same AstNode
  ///
  /// \param typePtr Canonical Type pointer (must not be nullptr)
  /// \param properties Node properties (kind, type name, etc.)
  /// \param range Source range for this type
  /// \return Deduplicated AstNode* (never nullptr)
  ///
  /// Thread-safety: Not thread-safe. Must be called from same thread as
  /// AstContext.
  AstNode *getOrCreateTypeNode(const void *typePtr, AcavJson properties,
                                const SourceRange &range);

  /// \brief Find existing deduplicated Type node (or nullptr).
  AstNode *findTypeNode(const void *typePtr) const;

  /// \brief Add node to location index
  /// \param node AST view node to index
  /// Called during AST construction to build source-to-AST mapping
  void indexNode(AstViewNode *node);

  /// \brief Finalize location index (call after all nodes indexed)
  void finalizeLocationIndex();

  /// \brief Get location index for source-to-AST queries
  const SourceLocationIndex &getLocationIndex() const { return locationIndex_; }

  /// \brief Get total number of data nodes.
  std::size_t getAstNodeCount() const { return allAstNodes_.size(); }
  /// \brief Get total number of view nodes.
  std::size_t getAstViewNodeCount() const { return allAstViewNodes_.size(); }

#ifdef ACAV_ENABLE_STRING_STATS
  /// \brief Get type deduplication statistics.
  TypeDeduplicationStats getTypeDeduplicationStats() const;
  /// \brief Print type deduplication statistics to stderr.
  void printTypeDeduplicationStats(const char *label = nullptr) const;
#endif

private:
  std::vector<AstNode *> allAstNodes_;
  std::vector<AstViewNode *> allAstViewNodes_;

  /// \brief Deduplication map for Type nodes
  /// Key: Canonical Type pointer (pointer identity from Clang)
  /// Value: Deduplicated AstNode instance
  std::unordered_map<const void *, AstNode *> typeDeduplicationMap_;

  /// \brief Location index for source-to-AST mapping
  SourceLocationIndex locationIndex_;

#ifdef ACAV_ENABLE_STRING_STATS
  /// \brief Stats counters for type deduplication
  mutable std::size_t typeLookupCount_ = 0;
  mutable std::size_t typeCacheHits_ = 0;
#endif
};

/// \brief Pure data container for AST node properties
///
/// Stores node metadata (kind, name, type, etc.) in JSON format.
/// Separated from tree structure to enable deduplication.
/// Use nlohmann/json with InternedString to deduplication.
///
/// Common properties:
///   - "kind": Node kind (e.g., "FunctionDecl", "VarDecl")
///   - "name": Node name (if applicable)
///   - "type": Type string (if applicable)
class AstNode {
public:
  /// \brief Get node properties (kind, name, type, etc.).
  const AcavJson &getProperties() const { return properties_; }
  /// \brief Get mutable properties.
  AcavJson &getProperties() { return properties_; }
  /// \brief Get source location range.
  const SourceRange &getSourceRange() const { return sourceRange_; }

  /// \brief Get number of view nodes referencing this data.
  std::size_t getUseCount() const { return refCount_; }
  /// \brief Increment reference count.
  void hold() { ++refCount_; }
  /// \brief Decrement reference count.
  void release() {
    if (refCount_ > 0)
      --refCount_;
  }

private:
  // Constructor is private - only AstContext can create nodes
  AstNode(AcavJson properties, const SourceRange &sourceRange);
  friend class AstContext;

  AcavJson properties_;    // Node metadata (kind, name, type, etc.)
  SourceRange sourceRange_;  // Source location
  std::size_t refCount_ = 0; // Number of AstViewNodes using this
};

/// \brief Represents node in AST tree hierarchy
///
/// Manages parent-child relationships. Points to shared AstNode data.
/// Memory managed by AstContext (no recursive deletion).
class AstViewNode {
public:
  ~AstViewNode();

  AstViewNode(const AstViewNode &) = delete;
  AstViewNode &operator=(const AstViewNode &) = delete;
  AstViewNode(AstViewNode &&) = delete;
  AstViewNode &operator=(AstViewNode &&) = delete;

  /// \brief Get underlying data node.
  AstNode *getNode() const { return node_; }
  /// \brief Get parent in tree hierarchy.
  AstViewNode *getParent() const { return parent_; }
  /// \brief Get children in tree hierarchy.
  const std::vector<AstViewNode *> &getChildren() const { return children_; }

  /// \brief Set parent node.
  void setParent(AstViewNode *parent) { parent_ = parent; }
  /// \brief Add child node.
  void addChild(AstViewNode *child);

  /// \brief Get node properties (delegates to data node).
  const AcavJson &getProperties() const { return node_->getProperties(); }
  /// \brief Get source range (delegates to data node).
  const SourceRange &getSourceRange() const { return node_->getSourceRange(); }

private:
  // Constructor is private - only AstContext can create nodes
  explicit AstViewNode(AstNode *node);
  friend class AstContext;

  AstNode *node_;                       // Pointer to data (may be shared)
  AstViewNode *parent_ = nullptr;       // Parent in tree
  std::vector<AstViewNode *> children_; // Children in tree (owned)
};

} // namespace acav
