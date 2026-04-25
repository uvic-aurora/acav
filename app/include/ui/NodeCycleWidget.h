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

/// \file NodeCycleWidget.h
/// \brief Popup widget for cycling through overlapping AST nodes.
#pragma once

#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <vector>

namespace acav {

class AstViewNode; // Forward declaration

/// \brief Popup widget for cycling through multiple matching nodes
///
/// Displayed when multiple AST nodes overlap at a source position.
/// Shows list of matches with node kinds and allows cycling.
class NodeCycleWidget : public QWidget {
  Q_OBJECT

public:
  explicit NodeCycleWidget(QWidget *parent = nullptr);

  /// \brief Show widget with list of matches
  /// \param matches Nodes at clicked position (deepest first)
  /// \param clickPos Global screen position where user clicked
  void showMatches(const std::vector<AstViewNode *> &matches,
                   const QPoint &clickPos);

signals:
  /// \brief Emitted when user navigates to a node
  void nodeSelected(AstViewNode *node);

  /// \brief Emitted when widget is closed
  void closed();

private slots:
  void onNext();
  void onPrevious();
  void onClose();

private:
  void updateDisplay();

  std::vector<AstViewNode *> matches_;
  std::size_t currentIndex_ = 0;

  QLabel *infoLabel_;
  QPushButton *prevButton_;
  QPushButton *nextButton_;
  QPushButton *closeButton_;
};

} // namespace acav
