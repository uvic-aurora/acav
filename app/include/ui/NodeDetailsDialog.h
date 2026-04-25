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

/// \file NodeDetailsDialog.h
/// \brief Non-modal dialog displaying AST node properties in a tree view.
#pragma once

#include "core/AstNode.h"
#include <QDialog>
#include <QTreeWidget>

namespace acav {

/// \brief Non-modal dialog displaying AST node properties in a tree view
///
/// Shows all JSON properties of an AST node in a hierarchical view.
/// Each instance owns a deep copy of the node data, so it survives TU changes.
/// Multiple instances can be open simultaneously.
class NodeDetailsDialog : public QDialog {
  Q_OBJECT

public:
  /// \brief Create a node details dialog
  /// \param properties Deep copy of the node properties JSON
  /// \param windowTitle Title for the dialog window
  /// \param parent Optional parent widget
  explicit NodeDetailsDialog(AcavJson properties, const QString &windowTitle,
                             QWidget *parent = nullptr);
  ~NodeDetailsDialog() override = default;

private:
  void setupUI();
  void populateTree(QTreeWidgetItem *parent, const QString &key,
                    const AcavJson &value);
  static QString valueToDisplayString(const AcavJson &value,
                                      const QString &key = QString());

  AcavJson properties_;
  QTreeWidget *treeWidget_;
};

} // namespace acav
