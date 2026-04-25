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

/// \file DockTitleBar.cpp
/// \brief Implementation of custom dock title bar.
#include "ui/DockTitleBar.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPalette>

namespace acav {

DockTitleBar::DockTitleBar(const QString &title, QWidget *parent)
    : QFrame(parent) {
  setAutoFillBackground(true);
  setFrameShape(QFrame::NoFrame);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(6, 2, 6, 2);
  layout->setSpacing(8);

  titleLabel_ = new QLabel(title, this);
  QFont font = titleLabel_->font();
  font.setBold(true);
  titleLabel_->setFont(font);

  subtitleLabel_ = new QLabel(this);
  subtitleLabel_->setVisible(false);
  subtitleLabel_->setTextFormat(Qt::PlainText);

  layout->addWidget(titleLabel_);
  layout->addWidget(subtitleLabel_, 1); // Allow subtitle to take available space
  layout->addStretch();

  updateAppearance();
}

void DockTitleBar::setFocused(bool focused) {
  if (focused_ == focused) {
    return;
  }
  focused_ = focused;
  updateAppearance();
}

void DockTitleBar::setSubtitle(const QString &subtitle) {
  if (!subtitleLabel_) {
    return;
  }
  fullSubtitle_ = subtitle;
  subtitleLabel_->setToolTip(subtitle);
  subtitleLabel_->setVisible(!subtitle.isEmpty());
  updateElidedSubtitle();
}

void DockTitleBar::resizeEvent(QResizeEvent *event) {
  QFrame::resizeEvent(event);
  updateElidedSubtitle();
}

void DockTitleBar::updateElidedSubtitle() {
  if (!subtitleLabel_ || fullSubtitle_.isEmpty()) {
    return;
  }

  // Calculate available width for subtitle
  // Reserve space for title, spacing, and margins
  int titleWidth = titleLabel_ ? titleLabel_->sizeHint().width() : 0;
  int margins = 6 + 6 + 8; // left + right margin + spacing
  int availableWidth = width() - titleWidth - margins - 20; // extra padding

  if (availableWidth < 50) {
    availableWidth = 50; // Minimum width
  }

  QFontMetrics fm(subtitleLabel_->font());
  QString elided = fm.elidedText(fullSubtitle_, Qt::ElideMiddle, availableWidth);
  subtitleLabel_->setText(elided);
}

void DockTitleBar::updateAppearance() {
  QPalette pal = palette();

  if (focused_) {
    // Focused: blue accent bar
    pal.setColor(QPalette::Window, QColor(0, 120, 212)); // #0078d4
    pal.setColor(QPalette::WindowText, Qt::white);
  } else {
    // Unfocused: default background
    pal.setColor(QPalette::Window, palette().color(QPalette::Mid));
    pal.setColor(QPalette::WindowText, palette().color(QPalette::WindowText));
  }

  setPalette(pal);
  titleLabel_->setPalette(pal);
  if (subtitleLabel_) {
    subtitleLabel_->setPalette(pal);
  }
}

} // namespace acav
