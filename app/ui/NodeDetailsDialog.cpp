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

/// \file NodeDetailsDialog.cpp
/// \brief Implementation of the Node Details dialog.
#include "ui/NodeDetailsDialog.h"

#include <QHeaderView>
#include <QVBoxLayout>

namespace acav {

NodeDetailsDialog::NodeDetailsDialog(AcavJson properties,
                                     const QString &windowTitle,
                                     QWidget *parent)
    : QDialog(parent), properties_(std::move(properties)) {
  setWindowTitle(windowTitle);
  setupUI();
}

void NodeDetailsDialog::setupUI() {
  setMinimumSize(400, 300);
  resize(600, 500);

  auto *layout = new QVBoxLayout(this);

  treeWidget_ = new QTreeWidget(this);
  treeWidget_->setHeaderLabels({tr("Property"), tr("Value")});
  treeWidget_->setRootIsDecorated(true);
  treeWidget_->setAnimated(false);
  treeWidget_->setUniformRowHeights(true);
  treeWidget_->setAlternatingRowColors(true);
  treeWidget_->setIndentation(20);
  treeWidget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  treeWidget_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  treeWidget_->header()->setStretchLastSection(true);
  treeWidget_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

  layout->addWidget(treeWidget_);

  // Populate tree with data
  populateTree(nullptr, QString(), properties_);
  treeWidget_->expandToDepth(1);
}

void NodeDetailsDialog::populateTree(QTreeWidgetItem *parent, const QString &key,
                                     const AcavJson &value) {
  QTreeWidgetItem *item = nullptr;

  if (value.is_object()) {
    QString label = key.isEmpty() ? tr("(root)") : key;
    item = new QTreeWidgetItem({label, tr("(object)")});

    for (auto it = value.begin(); it != value.end(); ++it) {
      QString childKey = QString::fromStdString(it.key().str());
      populateTree(item, childKey, it.value());
    }
  } else if (value.is_array()) {
    item = new QTreeWidgetItem({key, tr("[%1 items]").arg(value.size())});

    int index = 0;
    for (const auto &elem : value) {
      QString indexKey = QStringLiteral("[%1]").arg(index++);
      populateTree(item, indexKey, elem);
    }
  } else {
    QString displayValue = valueToDisplayString(value, key);
    item = new QTreeWidgetItem({key, displayValue});

    if (value.is_boolean()) {
      item->setForeground(1, QColor(0, 100, 0));
    } else if (value.is_number()) {
      item->setForeground(1, QColor(0, 0, 150));
    } else if (value.is_null()) {
      item->setForeground(1, QColor(128, 128, 128));
    }
  }

  if (parent) {
    parent->addChild(item);
  } else {
    treeWidget_->addTopLevelItem(item);
  }
}

QString NodeDetailsDialog::valueToDisplayString(const AcavJson &value,
                                                 const QString &key) {
  if (value.is_boolean()) {
    return value.get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
  }
  if (value.is_number_integer()) {
    int64_t num = value.get<int64_t>();
    // Format "nodePtr" fields as hex
    if (key == QStringLiteral("nodePtr")) {
      return QStringLiteral("0x%1").arg(static_cast<quint64>(num), 0, 16);
    }
    return QString::number(num);
  }
  if (value.is_number_unsigned()) {
    uint64_t num = value.get<uint64_t>();
    // Format "nodePtr" fields as hex
    if (key == QStringLiteral("nodePtr")) {
      return QStringLiteral("0x%1").arg(num, 0, 16);
    }
    return QString::number(num);
  }
  if (value.is_number_float()) {
    return QString::number(value.get<double>());
  }
  if (value.is_string()) {
    return QString::fromStdString(value.get<InternedString>().str());
  }
  if (value.is_null()) {
    return QStringLiteral("null");
  }
  return QStringLiteral("(unknown)");
}

} // namespace acav
