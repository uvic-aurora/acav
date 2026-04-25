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

#include "ui/AstModel.h"
#include "common/FileManager.h"
#include "common/InternedString.h"
#include <QBrush>
#include <QColor>
#include <QDebug>
#include <QFont>
#include <QStringList>

namespace {

QString valueToString(const acav::AcavJson &value) {
  if (value.is_boolean()) {
    return value.get<bool>() ? "true" : "false";
  }
  if (value.is_number_integer()) {
    return QString::number(value.get<int64_t>());
  }
  if (value.is_number_unsigned()) {
    return QString::number(value.get<uint64_t>());
  }
  if (value.is_number_float()) {
    return QString::number(value.get<double>());
  }
  if (value.is_string()) {
    return QString::fromStdString(
        value.get<acav::InternedString>().str());
  }
  return {};
}

QString buildDetailString(const acav::AcavJson &props) {
  QStringList attrs;
  auto addAttr = [&](const char *key, const char *label) {
    if (props.contains(key) && props.at(key).is_boolean() &&
        props.at(key).get<bool>()) {
      attrs << label;
    }
  };
  addAttr("isConstexpr", "constexpr");
  addAttr("isStatic", "static");
  addAttr("isVirtual", "virtual");
  addAttr("isPure", "pure");
  addAttr("isInlined", "inline");

  QString attrStr;
  if (!attrs.isEmpty()) {
    attrStr = " {" + attrs.join(" ") + "}";
  }

  // Template args
  QString templateStr;
  if (props.contains("templateArgs") && props.at("templateArgs").is_array()) {
    const auto &arr = props.at("templateArgs");
    if (!arr.empty()) {
      QStringList parts;
      for (const auto &arg : arr) {
        if (arg.contains("value")) {
          parts << valueToString(arg.at("value"));
        } else if (arg.contains("kindName")) {
          parts << QString::fromStdString(
              arg.at("kindName").get<acav::InternedString>().str());
        }
      }
      templateStr = "<" + parts.join(", ") + ">";
    }
  }

  // Value category (Expr only)
  QString vc;
  if (props.contains("isLValue") && props.at("isLValue").is_boolean() &&
      props.at("isLValue").get<bool>()) {
    vc = " vc=lvalue";
  } else if (props.contains("isXValue") && props.at("isXValue").is_boolean() &&
             props.at("isXValue").get<bool>()) {
    vc = " vc=xvalue";
  } else if (props.contains("isPRValue") &&
             props.at("isPRValue").is_boolean() &&
             props.at("isPRValue").get<bool>()) {
    vc = " vc=prvalue";
  }

  // Dependence flag
  QString dep;
  const bool typeDep = props.contains("isTypeDependent") &&
                       props.at("isTypeDependent").is_boolean() &&
                       props.at("isTypeDependent").get<bool>();
  const bool valDep = props.contains("isValueDependent") &&
                      props.at("isValueDependent").is_boolean() &&
                      props.at("isValueDependent").get<bool>();
  if (typeDep || valDep) {
    dep = " dep";
  }

  // Type selections
  auto getStr = [&](const char *key) -> QString {
    if (props.contains(key) && props.at(key).is_string()) {
      return QString::fromStdString(
          props.at(key).get<acav::InternedString>().str());
    }
    return {};
  };
  QString type = getStr("type");
  if (type.isEmpty()) {
    type = getStr("spelledType");
  }
  QString canonical = getStr("canonicalType");
  QString desugared = getStr("desugaredType");

  // Drop canonical/desugared if identical to primary type
  if (!canonical.isEmpty() && canonical == type) {
    canonical.clear();
  }
  if (!desugared.isEmpty() && desugared == type) {
    desugared.clear();
  }

  QString canonicalPart;
  if (!canonical.isEmpty()) {
    canonicalPart = QString(" canon: %1").arg(canonical);
  }
  QString desugPart;
  if (!desugared.isEmpty()) {
    desugPart = QString(" desug: %1").arg(desugared);
  }

  QString templatePart;
  if (!templateStr.isEmpty()) {
    templatePart = " " + templateStr;
  }

  QString valuePart;
  if (props.contains("value")) {
    QString v = valueToString(props.at("value"));
    if (!v.isEmpty()) {
      valuePart = QString(" = %1").arg(v);
    }
  }

  QString combined = canonicalPart + desugPart + attrStr + templatePart + vc +
                     dep + valuePart;
  return combined.trimmed();
}

} // namespace

namespace acav {

AstModel::AstModel(QObject *parent)
    : QAbstractItemModel(parent), root_(nullptr),
      emptyMessage_(tr("No AST available (code not yet compiled).")) {}

AstModel::~AstModel() {
  // Don't delete nodes - AstContext owns them
  root_ = nullptr;
}

void AstModel::setEmptyMessage(const QString &message) {
  if (emptyMessage_ == message) {
    return;
  }
  beginResetModel();
  emptyMessage_ = message;
  endResetModel();
}

void AstModel::setRootNode(AstViewNode *root) {
  beginResetModel();
  root_ = root;
  selectedNode_ = nullptr;
  endResetModel();
}

void AstModel::clear() {
  beginResetModel();
  root_ = nullptr;
  selectedNode_ = nullptr;
  totalNodeCount_ = 0;
  endResetModel();
}

int AstModel::visibleNodeCount() const {
  return static_cast<int>(totalNodeCount_);
}

QModelIndex AstModel::index(int row, int column,
                            const QModelIndex &parent) const {
  if (!hasIndex(row, column, parent)) {
    return QModelIndex();
  }

  if (!parent.isValid()) {
    if (row != 0) {
      return QModelIndex();
    }
    if (!root_) {
      if (emptyMessage_.isEmpty()) {
        return QModelIndex();
      }
      return createIndex(row, column, static_cast<void *>(nullptr));
    }
    return createIndex(row, column, root_);
  }

  AstViewNode *parentNode = getNodeFromIndex(parent);
  if (!parentNode) {
    return QModelIndex();
  }
  const std::vector<AstViewNode *> &children = visibleChildrenFor(parentNode);
  if (row < 0 || row >= static_cast<int>(children.size())) {
    return QModelIndex();
  }

  AstViewNode *childNode = children[row];
  return createIndex(row, column, childNode);
}

QModelIndex AstModel::parent(const QModelIndex &child) const {
  if (!child.isValid()) {
    return QModelIndex();
  }

  AstViewNode *childNode = getNodeFromIndex(child);
  if (!childNode) {
    return QModelIndex();
  }

  AstViewNode *parentNode = childNode->getParent();
  if (!parentNode) {
    return QModelIndex();
  }

  int row = visibleRow(parentNode);
  if (row < 0) {
    return QModelIndex();
  }

  return createIndex(row, 0, parentNode);
}

int AstModel::rowCount(const QModelIndex &parent) const {
  if (!root_) {
    if (!parent.isValid() && !emptyMessage_.isEmpty()) {
      return 1;
    }
    return 0;
  }

  if (parent.column() > 0) {
    return 0;
  }

  if (!parent.isValid()) {
    return 1;
  }

  AstViewNode *node = getNodeFromIndex(parent);
  return static_cast<int>(visibleChildrenFor(node).size());
}

int AstModel::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent);
  return 1; // Single column, formatted
}

QVariant AstModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }

  AstViewNode *node = getNodeFromIndex(index);
  if (!node) {
    if (!root_ && index.row() == 0 && index.column() == 0) {
      if (role == Qt::DisplayRole) {
        return emptyMessage_;
      }
      if (role == Qt::ForegroundRole) {
        return QBrush(QColor(128, 128, 128));
      }
      if (role == Qt::FontRole) {
        QFont font;
        font.setItalic(true);
        return font;
      }
      if (role == Qt::ToolTipRole) {
        return tr("Double-click a source file to generate and load an AST.");
      }
    }
    return QVariant();
  }

  const AcavJson &props = node->getProperties();

  if (role == Qt::DisplayRole && index.column() == 0) {
    QString kind = "<unknown>";
    if (props.contains("kind") && props.at("kind").is_string()) {
      kind = QString::fromStdString(
          props.at("kind").get<InternedString>().str());
    }

    QString name;
    if (props.contains("name") && props.at("name").is_string()) {
      name = QString::fromStdString(
          props.at("name").get<InternedString>().str());
    } else if (props.contains("declName") && props.at("declName").is_string()) {
      name = QString::fromStdString(
          props.at("declName").get<InternedString>().str());
    } else if (props.contains("memberName") &&
               props.at("memberName").is_string()) {
      name = QString::fromStdString(
          props.at("memberName").get<InternedString>().str());
    }

    QString type;
    if (props.contains("type") && props.at("type").is_string()) {
      type = QString::fromStdString(
          props.at("type").get<InternedString>().str());
    } else if (props.contains("spelledType") &&
               props.at("spelledType").is_string()) {
      type = QString::fromStdString(
          props.at("spelledType").get<InternedString>().str());
    }

    QString head = name.isEmpty() ? kind : QString("%1 %2").arg(kind, name);
    QString tail;
    if (!type.isEmpty()) {
      tail = QString(" : %1").arg(type);
    }

    QString detail = buildDetailString(props);
    if (detail.isEmpty()) {
      return head + tail;
    }
    return tail.isEmpty() ? (head + " " + detail)
                          : (head + tail + " " + detail);
  }

  if (role == NodePtrRole) {
    return QVariant::fromValue(static_cast<void *>(node));
  }

  if (role == Qt::ToolTipRole) {
    QString detail = buildDetailString(props);
    const SourceRange &range = node->getSourceRange();
    QString loc;
    if (range.begin().fileID() == FileManager::InvalidFileID) {
      loc = "Range: <invalid>";
    } else {
      loc = QString("Range: %1:%2 - %3:%4")
                .arg(range.begin().line())
                .arg(range.begin().column())
                .arg(range.end().line())
                .arg(range.end().column());
    }
    return detail.isEmpty() ? loc : (detail + "\n" + loc);
  }

  return QVariant();
}

QVariant AstModel::headerData(int section, Qt::Orientation orientation,
                              int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    if (section == 0) {
      return "AST";
    }
  }
  return QVariant();
}

Qt::ItemFlags AstModel::flags(const QModelIndex &index) const {
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }

  if (!root_ && index.row() == 0 && index.column() == 0) {
    return Qt::ItemIsEnabled;
  }

  Qt::ItemFlags defaultFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

  // Mark leaf nodes with ItemNeverHasChildren for performance optimization
  // This avoids unnecessary expand/collapse checks for nodes without children
  auto *node = static_cast<AstViewNode *>(index.internalPointer());
  if (node && visibleChildrenFor(node).empty()) {
    defaultFlags |= Qt::ItemNeverHasChildren;
  }

  return defaultFlags;
}

AstViewNode *AstModel::getNodeFromIndex(const QModelIndex &index) const {
  return static_cast<AstViewNode *>(index.internalPointer());
}

QModelIndex AstModel::selectNode(AstViewNode *node) {
  if (!node || !root_) {
    qDebug() << "AstModel::selectNode: null node or no root";
    return QModelIndex();
  }

  QModelIndex index = findNodeIndex(node);
  if (index.isValid()) {
    // Store old selection to update its display
    AstViewNode *oldSelection = selectedNode_;
    QModelIndex oldIndex;
    if (oldSelection && oldSelection != node) {
      oldIndex = findNodeIndex(oldSelection);
    }

    selectedNode_ = node;
    emit nodeSelected(node);

    // Notify view to repaint old and new selections
    if (oldIndex.isValid()) {
      emit dataChanged(oldIndex, oldIndex, {Qt::BackgroundRole});
    }
    emit dataChanged(index, index, {Qt::BackgroundRole});
  } else {
    qDebug() << "AstModel::selectNode: findNodeIndex failed for node" << node;
  }

  return index;
}

void AstModel::updateSelectionFromIndex(const QModelIndex &index) {
  if (!index.isValid()) {
    return;
  }

  AstViewNode *node = getNodeFromIndex(index);
  if (!node || node == selectedNode_) {
    return;
  }

  // Clear old selection
  QModelIndex oldIndex;
  if (selectedNode_) {
    oldIndex = findNodeIndex(selectedNode_);
  }

  selectedNode_ = node;

  // Notify view to repaint
  if (oldIndex.isValid()) {
    emit dataChanged(oldIndex, oldIndex, {Qt::BackgroundRole});
  }
  emit dataChanged(index, index, {Qt::BackgroundRole});
}

/// \brief Find model index for given node by traversing tree
/// \param node
/// \return The QModelIndex of the node, or invalid if not found
/// TODO: Optimize with caching/indexing if performance is an issue
QModelIndex AstModel::findNodeIndex(AstViewNode *node) const {
  if (!node || !root_) {
    return QModelIndex();
  }

  std::vector<AstViewNode *> path;
  AstViewNode *current = node;
  while (current) {
    path.push_back(current);
    current = current->getParent();
  }

  if (path.back() != root_) {
    qDebug() << "AstModel::findNodeIndex: path does not lead to root";
    return QModelIndex();
  }

  std::reverse(path.begin(), path.end());

  QModelIndex index; // Invalid to start
  for (AstViewNode *entry : path) {
    int row = visibleRow(entry);
    if (row < 0) {
      qDebug() << "AstModel::findNodeIndex: failed to find row for" << entry;
      return QModelIndex();
    }
    index = this->index(row, 0, index);
    if (!index.isValid()) {
      qDebug() << "AstModel::findNodeIndex: failed to create index at row" << row;
      return QModelIndex();
    }
  }

  return index;
}

const std::vector<AstViewNode *> &
AstModel::visibleChildrenFor(AstViewNode *parent) const {
  static const std::vector<AstViewNode *> kEmpty;
  if (!parent) {
    return kEmpty;
  }
  return parent->getChildren();
}

int AstModel::visibleRow(AstViewNode *node) const {
  if (!node) {
    return -1;
  }

  AstViewNode *parent = node->getParent();
  if (!parent) {
    return node == root_ ? 0 : -1;
  }
  const auto &siblings = parent->getChildren();
  for (std::size_t i = 0; i < siblings.size(); ++i) {
    if (siblings[i] == node) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

} // namespace acav
