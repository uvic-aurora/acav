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

/// \file DeclContextView.h
/// \brief Declaration context hierarchy display panel.
#pragma once

#include "core/AstNode.h"
#include <QString>
#include <QTreeWidget>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace acav {

/// \brief Panel displaying declaration context hierarchy for selected AST node
///
/// This widget shows semantic and lexical declaration contexts for the
/// currently selected AST node. Context chains are stored on Decl nodes and
/// displayed on demand.
///
class DeclContextView : public QWidget {
  Q_OBJECT

public:
  explicit DeclContextView(QWidget *parent = nullptr);
  ~DeclContextView() override = default;

signals:
  /// \brief Emitted when user clicks a context entry to navigate to that node
  void contextNodeClicked(AstViewNode *node);

public slots:
  /// \brief Update display for selected node
  /// \param node The selected AST node (may be nullptr)
  void setSelectedNode(AstViewNode *node);

  /// \brief Clear the display
  void clear();

  /// \brief Set focus to semantic context tree
  void focusSemanticTree();

  /// \brief Set focus to lexical context tree
  void focusLexicalTree();

  /// \brief Propagate font to all internal widgets (labels + trees)
  void applyFont(const QFont &font);

private slots:
  /// \brief Handle current item change in context tree (mouse or keyboard)
  void onContextItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);

private:
  /// \brief Set up UI components
  void setupUI();

  /// \brief Build and display context hierarchy from node
  /// \param node The selected AST node
  void populateContextHierarchy(AstViewNode *node);

  /// \brief Display "Not applicable" message for non-Decl nodes
  void showNotApplicable();

  /// \brief Populate a tree with a context chain
  void populateContextTree(QTreeWidget *tree, const AcavJson *contextArray,
                           bool highlightLast);

  /// \brief Apply color styling based on decl kind
  void applyKindStyling(QTreeWidgetItem *item, const QString &kind) const;

  // UI components
  QLabel *semanticLabel_;
  QLabel *lexicalLabel_;
  QTreeWidget *semanticTree_;
  QTreeWidget *lexicalTree_;
};

} // namespace acav
