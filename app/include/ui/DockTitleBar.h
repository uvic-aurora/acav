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

/// \file DockTitleBar.h
/// \brief Custom dock widget title bar with fast focus indication.
#pragma once

#include <QFrame>
#include <QLabel>
#include <QResizeEvent>

namespace acav {

/// \brief Custom title bar for QDockWidget with fast focus indication.
///
/// Uses QPalette for color changes instead of setStyleSheet(),
/// avoiding expensive style recalculation on focus changes.
class DockTitleBar : public QFrame {
public:
  explicit DockTitleBar(const QString &title, QWidget *parent = nullptr);

  /// Set the focused state. Uses QPalette for fast updates.
  void setFocused(bool focused);

  /// Get the current focused state.
  bool isFocused() const { return focused_; }

  /// Set subtitle text (e.g., file path) displayed below the title.
  /// Long paths are elided in the middle; full path shown in tooltip.
  void setSubtitle(const QString &subtitle);

  /// Get the current subtitle text (full, non-elided).
  QString subtitle() const { return fullSubtitle_; }

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void updateAppearance();
  void updateElidedSubtitle();

  QLabel *titleLabel_ = nullptr;
  QLabel *subtitleLabel_ = nullptr;
  QString fullSubtitle_;
  bool focused_ = false;
};

} // namespace acav
