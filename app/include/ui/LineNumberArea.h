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

#pragma once

#include <QWidget>

namespace acav {

class SourceCodeView;  // Forward declaration

/// \brief Widget for displaying line numbers beside SourceCodeView
///
/// Companion widget that paints line numbers in the left margin.
/// Managed by SourceCodeView, synchronized with scrolling and text changes.
/// Based on Qt's Code Editor example pattern.
///
class LineNumberArea : public QWidget {
  Q_OBJECT

public:
  /// \brief Construct line number area for given editor
  /// \param editor Parent SourceCodeView (must not be null)
  explicit LineNumberArea(SourceCodeView *editor);

  /// \brief Returns size hint based on current line number width
  /// \return Preferred size for layout system
  QSize sizeHint() const override;

protected:
  /// \brief Paints line numbers for visible text blocks
  /// \param event Paint event with update rectangle
  void paintEvent(QPaintEvent *event) override;

private:
  SourceCodeView *codeEditor_;  // Non-owning pointer to parent editor
};

} // namespace acav
