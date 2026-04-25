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

#include "ui/NodeCycleWidget.h"
#include "core/AstNode.h"
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace acav {

NodeCycleWidget::NodeCycleWidget(QWidget *parent) : QWidget(parent) {
  // Make this a popup window
  setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_DeleteOnClose, false);

  // Create UI elements
  infoLabel_ = new QLabel(this);
  prevButton_ = new QPushButton("Previous", this);
  nextButton_ = new QPushButton("Next", this);
  closeButton_ = new QPushButton("Close", this);

  // Layout
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->addWidget(infoLabel_);

  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->addWidget(prevButton_);
  buttonLayout->addWidget(nextButton_);
  buttonLayout->addWidget(closeButton_);
  mainLayout->addLayout(buttonLayout);

  setLayout(mainLayout);

  // Connect signals
  connect(prevButton_, &QPushButton::clicked, this, &NodeCycleWidget::onPrevious);
  connect(nextButton_, &QPushButton::clicked, this, &NodeCycleWidget::onNext);
  connect(closeButton_, &QPushButton::clicked, this, &NodeCycleWidget::onClose);

  // Style — handled by NodeCycleWidget selector in style.qss
}

void NodeCycleWidget::showMatches(const std::vector<AstViewNode *> &matches,
                                   const QPoint &clickPos) {
  if (matches.empty()) {
    return;
  }

  matches_ = matches;
  currentIndex_ = 0;

  updateDisplay();

  // Position near click
  move(clickPos + QPoint(10, 10));
  show();
  raise();
  activateWindow();

  // Emit first node
  emit nodeSelected(matches_[currentIndex_]);
}

void NodeCycleWidget::onNext() {
  if (matches_.empty()) {
    return;
  }

  currentIndex_ = (currentIndex_ + 1) % matches_.size();
  updateDisplay();
  emit nodeSelected(matches_[currentIndex_]);
}

void NodeCycleWidget::onPrevious() {
  if (matches_.empty()) {
    return;
  }

  if (currentIndex_ == 0) {
    currentIndex_ = matches_.size() - 1;
  } else {
    --currentIndex_;
  }

  updateDisplay();
  emit nodeSelected(matches_[currentIndex_]);
}

void NodeCycleWidget::onClose() {
  hide();
  emit closed();
}

void NodeCycleWidget::updateDisplay() {
  if (matches_.empty()) {
    return;
  }

  AstViewNode *node = matches_[currentIndex_];
  const AcavJson &props = node->getProperties();

  QString kind = "<unknown>";
  if (props.contains("kind") && props.at("kind").is_string()) {
    kind = QString::fromStdString(
        props.at("kind").get<InternedString>().str());
  }
  QString name;
  if (props.contains("name")) {
    name = QString::fromStdString(props["name"].get<InternedString>().str());
  }

  QString info = QString("Match %1 of %2: %3")
                     .arg(currentIndex_ + 1)
                     .arg(matches_.size())
                     .arg(kind);

  if (!name.isEmpty()) {
    info += QString(" (%1)").arg(name);
  }

  infoLabel_->setText(info);

  // Enable/disable buttons
  prevButton_->setEnabled(matches_.size() > 1);
  nextButton_->setEnabled(matches_.size() > 1);
}

} // namespace acav
