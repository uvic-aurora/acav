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

/// \file AstModel.h
/// \brief Qt model for AST tree view display.
#pragma once

#include "core/AstNode.h"
#include <QAbstractItemModel>
#include <QString>
#include <vector>

namespace acav {

/// \brief Qt model for displaying AST hierarchy
///
/// Implements QAbstractItemModel to display AstViewNode tree
/// in QTreeView. Shows node kind + name in display role.
///
/// Supports millions of nodes via Qt's lazy loading.
///
class AstModel : public QAbstractItemModel {
  Q_OBJECT

public:
  explicit AstModel(QObject *parent = nullptr);
  ~AstModel() override;

  /// \brief Message shown when the model has no AST loaded.
  void setEmptyMessage(const QString &message);

  /// \brief Set the root node of AST
  /// \param root Root AstViewNode (does NOT take ownership - owned by
  /// AstContext)
  void setRootNode(AstViewNode *root);

  /// \brief Set total node count for display
  void setTotalNodeCount(std::size_t count) { totalNodeCount_ = count; }

  /// \brief Clear model and delete AST
  void clear();

  /// \brief Number of nodes currently visible
  int visibleNodeCount() const;

  // QAbstractItemModel interface
  QModelIndex index(int row, int column,
                    const QModelIndex &parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &child) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

  // Custom roles
  enum CustomRoles {
    NodePtrRole = Qt::UserRole + 1 // Returns AstViewNode*
  };

  /// \brief Programmatically select node and get its model index
  /// \param node Node to select
  /// \return QModelIndex for the node (invalid if not found)
  QModelIndex selectNode(AstViewNode *node);

  /// \brief Update selection from a QModelIndex (e.g., when user clicks directly)
  /// \param index The model index that was selected
  void updateSelectionFromIndex(const QModelIndex &index);

  /// \brief Get currently selected node
  AstViewNode *selectedNode() const { return selectedNode_; }

  /// \brief Check if model has any nodes loaded
  bool hasNodes() const { return root_ != nullptr; }

signals:
  /// \brief Emitted when node is selected
  void nodeSelected(AstViewNode *node);

private:
  AstViewNode *getNodeFromIndex(const QModelIndex &index) const;

  /// \brief Find model index for given node by traversing tree
  QModelIndex findNodeIndex(AstViewNode *node) const;

  /// \brief Get visible children for the given parent
  const std::vector<AstViewNode *> &
  visibleChildrenFor(AstViewNode *parent) const;

  /// \brief Compute row of a visible node in its parent's child list
  int visibleRow(AstViewNode *node) const;

  AstViewNode *root_ = nullptr; // Not owned - AstContext owns all nodes
  AstViewNode *selectedNode_ = nullptr;
  std::size_t totalNodeCount_ = 0;
  QString emptyMessage_;
};

} // namespace acav
