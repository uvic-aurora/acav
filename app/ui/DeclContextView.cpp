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

#include "ui/DeclContextView.h"
#include "common/InternedString.h"
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

namespace acav {

DeclContextView::DeclContextView(QWidget *parent) : QWidget(parent) {
  setObjectName("declContextView");
  setAttribute(Qt::WA_StyledBackground, true);
  setupUI();
}

void DeclContextView::setupUI() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);

  // Semantic context
  semanticLabel_ = new QLabel(tr("Semantic Context"), this);
  layout->addWidget(semanticLabel_);

  semanticTree_ = new QTreeWidget(this);
  semanticTree_->setHeaderHidden(true);
  semanticTree_->setRootIsDecorated(true);
  semanticTree_->setAnimated(false);  // Performance: disable animations
  semanticTree_->setUniformRowHeights(true);  // Performance: uniform row heights
  semanticTree_->setIndentation(16);
  semanticTree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  semanticTree_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  semanticTree_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  semanticTree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  semanticTree_->header()->setStretchLastSection(false);
  semanticTree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  layout->addWidget(semanticTree_, 1);

  // Separator between semantic and lexical contexts
  auto *separator = new QFrame(this);
  separator->setFrameShape(QFrame::HLine);
  separator->setFrameShadow(QFrame::Sunken);
  layout->addWidget(separator);

  // Lexical context
  lexicalLabel_ = new QLabel(tr("Lexical Context"), this);
  layout->addWidget(lexicalLabel_);

  lexicalTree_ = new QTreeWidget(this);
  lexicalTree_->setHeaderHidden(true);
  lexicalTree_->setRootIsDecorated(true);
  lexicalTree_->setAnimated(false);  // Performance: disable animations
  lexicalTree_->setUniformRowHeights(true);  // Performance: uniform row heights
  lexicalTree_->setIndentation(16);
  lexicalTree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  lexicalTree_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  lexicalTree_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  lexicalTree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  lexicalTree_->header()->setStretchLastSection(false);
  lexicalTree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  layout->addWidget(lexicalTree_, 1);

  // Connect signals for navigation (handles both mouse click and keyboard)
  connect(semanticTree_, &QTreeWidget::currentItemChanged, this,
          &DeclContextView::onContextItemChanged);
  connect(lexicalTree_, &QTreeWidget::currentItemChanged, this,
          &DeclContextView::onContextItemChanged);
}

void DeclContextView::setSelectedNode(AstViewNode *node) {
  clear();

  if (!node) {
    return;
  }

  populateContextHierarchy(node);
}

void DeclContextView::clear() {
  semanticTree_->clear();
  lexicalTree_->clear();
}

void DeclContextView::focusSemanticTree() {
  semanticTree_->setFocus();
}

void DeclContextView::focusLexicalTree() {
  lexicalTree_->setFocus();
}

void DeclContextView::applyFont(const QFont &font) {
  setFont(font);
  semanticLabel_->setFont(font);
  lexicalLabel_->setFont(font);
  semanticTree_->setFont(font);
  lexicalTree_->setFont(font);
}

void DeclContextView::populateContextHierarchy(AstViewNode *node) {
  if (!node) {
    return;
  }

  const auto &props = node->getProperties();

  // Check if this is a Decl node (only Decl nodes have declaration contexts)
  bool isDecl = props.contains("isDecl") && props.at("isDecl").is_boolean() &&
                props.at("isDecl").get<bool>();

  if (!isDecl) {
    // Not a Decl node - show "Not applicable."
    showNotApplicable();
    return;
  }

  const AcavJson *semantic = nullptr;
  const AcavJson *lexical = nullptr;

  if (props.contains("semanticDeclContext") &&
      props.at("semanticDeclContext").is_array()) {
    semantic = &props.at("semanticDeclContext");
  }
  if (props.contains("lexicalDeclContext") &&
      props.at("lexicalDeclContext").is_array()) {
    lexical = &props.at("lexicalDeclContext");
  }

  populateContextTree(semanticTree_, semantic, true);
  populateContextTree(lexicalTree_, lexical, true);

  semanticTree_->expandAll();
  lexicalTree_->expandAll();
}

void DeclContextView::showNotApplicable() {
  auto *semanticItem = new QTreeWidgetItem({tr("Not applicable.")});
  semanticItem->setForeground(0, QColor(128, 128, 128));
  semanticTree_->addTopLevelItem(semanticItem);

  auto *lexicalItem = new QTreeWidgetItem({tr("Not applicable.")});
  lexicalItem->setForeground(0, QColor(128, 128, 128));
  lexicalTree_->addTopLevelItem(lexicalItem);
}

void DeclContextView::populateContextTree(QTreeWidget *tree,
                                          const AcavJson *contextArray,
                                          bool highlightLast) {
  if (!tree) {
    return;
  }
  tree->clear();
  if (!contextArray || contextArray->empty()) {
    // No declaration context data - leave the tree empty
    return;
  }

  QTreeWidgetItem *parentItem = nullptr;
  const int count = static_cast<int>(contextArray->size());
  for (int i = 0; i < count; ++i) {
    const auto &entry = contextArray->at(i);
    if (!entry.is_object()) {
      continue;
    }

    QString kind = tr("(Unknown)");
    QString name;
    if (entry.contains("kind") && entry.at("kind").is_string()) {
      kind = QString::fromStdString(
          entry.at("kind").get<InternedString>().str());
    }
    if (entry.contains("name") && entry.at("name").is_string()) {
      name = QString::fromStdString(
          entry.at("name").get<InternedString>().str());
    }

    QString display = name.isEmpty() ? kind : QString("%1 %2").arg(kind, name);
    auto *item = new QTreeWidgetItem({display});
    applyKindStyling(item, kind);

    // Store node pointer for navigation
    if (entry.contains("nodePtr") && entry.at("nodePtr").is_number_unsigned()) {
      auto ptr = entry.at("nodePtr").get<uint64_t>();
      item->setData(0, Qt::UserRole, QVariant::fromValue(ptr));
    }

    const bool isSelected = highlightLast && i == count - 1;
    if (isSelected) {
      QFont font = item->font(0);
      font.setBold(true);
      item->setFont(0, font);
    }

    if (parentItem) {
      parentItem->addChild(item);
    } else {
      tree->addTopLevelItem(item);
    }

    if (isSelected) {
      tree->setCurrentItem(item);
      item->setSelected(true);
    }

    parentItem = item;
  }
}

void DeclContextView::onContextItemChanged(QTreeWidgetItem *current,
                                            QTreeWidgetItem *previous) {
  Q_UNUSED(previous);
  if (!current) {
    return;
  }

  QVariant data = current->data(0, Qt::UserRole);
  if (!data.isValid()) {
    return;
  }

  auto ptr = data.value<uint64_t>();
  if (ptr == 0) {
    return;
  }

  auto *node = reinterpret_cast<AstViewNode *>(ptr);
  emit contextNodeClicked(node);
}

void DeclContextView::applyKindStyling(QTreeWidgetItem *item,
                                       const QString &kind) const {
  if (kind == "TranslationUnitDecl") {
    item->setForeground(0, QColor(100, 100, 100)); // Gray
  } else if (kind == "NamespaceDecl") {
    item->setForeground(0, QColor(0, 100, 0)); // Dark green
  } else if (kind.contains("Record") || kind.contains("Class")) {
    item->setForeground(0, QColor(0, 0, 150)); // Blue
  } else if (kind.contains("Function") || kind.contains("Method") ||
             kind.contains("Constructor") || kind.contains("Destructor")) {
    item->setForeground(0, QColor(150, 0, 150)); // Purple
  } else if (kind == "EnumDecl") {
    item->setForeground(0, QColor(150, 100, 0)); // Brown/orange
  }
}

} // namespace acav
