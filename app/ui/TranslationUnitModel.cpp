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

#include "ui/TranslationUnitModel.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <functional>
#include <limits>

namespace acav {

// Custom Qt roles for storing file information
static constexpr int FilePathRole = Qt::UserRole + 1;  // Full file path
static constexpr int FileIDRole = Qt::UserRole + 2;    // FileID (for identification)
static constexpr int IsSourceFileRole = Qt::UserRole + 3; // True if this is a source file node
static constexpr int SourceHeadersRole = Qt::UserRole + 4; // QJsonArray of headers for a source file
static constexpr int HeadersFetchedRole = Qt::UserRole + 5; // Whether headers are populated in tree
static constexpr int CachedHeaderPathsRole = Qt::UserRole + 6; // QStringList cached header paths

namespace {

/// Normalize path: resolve symlinks, "." / ".." segments, and separators.
QString normalizePath(const QString &path) {
  if (path.isEmpty()) {
    return QString();
  }
  // Use canonicalFilePath to resolve symlinks (like FileManager does)
  QFileInfo info(path);
  QString canonical = info.canonicalFilePath();
  // canonicalFilePath returns empty if file doesn't exist, fall back to cleanPath
  if (canonical.isEmpty()) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
  }
  return canonical;
}

/// Normalize path without filesystem access (no symlink resolution).
QString normalizePathLexical(const QString &path) {
  if (path.isEmpty()) {
    return QString();
  }
  return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool isPathUnderRoot(const QString &path, const QString &root) {
  if (root.isEmpty()) {
    return false;
  }
  if (path == root) {
    return true;
  }
  return path.startsWith(root + "/");
}

/// Helper structure for header classification
struct HeaderClassification {
  QStringList projectDirect;
  QStringList projectIndirect;
  QStringList externalDirect;
  QStringList externalIndirect;
};

/// Classify headers into Project/External and Direct/Indirect categories
HeaderClassification classifyHeaders(const QJsonArray &headersArray,
                                     const QString &projectRoot) {
  HeaderClassification result;

  // If project root is empty, we can't meaningfully distinguish
  // project vs external, so treat all as external
  bool hasValidProjectRoot = !projectRoot.isEmpty();

  for (const QJsonValue &headerValue : headersArray) {
    QJsonObject headerObj = headerValue.toObject();
    QString headerPath = normalizePath(headerObj["path"].toString());
    bool isDirect = headerObj["direct"].toBool();

    // Classify based on path (inside project root or not)
    bool isProjectHeader =
        hasValidProjectRoot && isPathUnderRoot(headerPath, projectRoot);

    if (isProjectHeader) {
      if (isDirect) {
        result.projectDirect.append(headerPath);
      } else {
        result.projectIndirect.append(headerPath);
      }
    } else {
      if (isDirect) {
        result.externalDirect.append(headerPath);
      } else {
        result.externalIndirect.append(headerPath);
      }
    }
  }

  return result;
}

/// Trie node for building directory tree
struct PathTreeNode {
  QString name;
  QString fullPath;
  QMap<QString, PathTreeNode *> children;
  bool isFile = false;

  ~PathTreeNode() { qDeleteAll(children); }
};

/// Insert a path into the trie
void insertPath(PathTreeNode *root, const QString &normalizedPath,
                const QString &normalizedRoot, const QString &originalFullPath) {
  // Get relative path from root
  QString relativePath = normalizedPath;
  if (!normalizedRoot.isEmpty()) {
    if (normalizedPath.startsWith(normalizedRoot + "/")) {
      relativePath = normalizedPath.mid(normalizedRoot.length() + 1);
    } else if (normalizedPath.startsWith(normalizedRoot) &&
               normalizedPath.length() > normalizedRoot.length()) {
      relativePath = normalizedPath.mid(normalizedRoot.length());
      if (relativePath.startsWith('/')) {
        relativePath = relativePath.mid(1);
      }
    }
  }

  // Split into components
  QStringList components = relativePath.split('/', Qt::SkipEmptyParts);
  if (components.isEmpty()) {
    return;
  }

  // Navigate/create tree
  PathTreeNode *current = root;
  for (int i = 0; i < components.size(); ++i) {
    const QString &component = components[i];
    bool isLast = (i == components.size() - 1);

    if (!current->children.contains(component)) {
      PathTreeNode *newNode = new PathTreeNode();
      newNode->name = component;
      newNode->isFile = isLast;
      if (isLast) {
        newNode->fullPath = originalFullPath;
      }
      current->children[component] = newNode;
    }
    current = current->children[component];
  }
}

/// Get sorted children (directories first, then files, both alphabetically)
QList<PathTreeNode *> getSortedChildren(PathTreeNode *node) {
  QList<PathTreeNode *> dirs, files;
  for (auto *child : node->children) {
    if (child->isFile) {
      files.append(child);
    } else {
      dirs.append(child);
    }
  }

  auto sortByName = [](PathTreeNode *a, PathTreeNode *b) {
    return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
  };
  std::sort(dirs.begin(), dirs.end(), sortByName);
  std::sort(files.begin(), files.end(), sortByName);

  return dirs + files;
}

} // namespace

TranslationUnitModel::TranslationUnitModel(FileManager &fileManager,
                                           QObject *parent)
    : QStandardItemModel(parent), fileManager_(fileManager) {
  setHorizontalHeaderLabels(QStringList() << "Files");
}

QString TranslationUnitModel::computeProjectRoot(
    const QStringList &sourceFilePaths) const {
  if (sourceFilePaths.isEmpty()) {
    return QString();
  }

  if (sourceFilePaths.size() == 1) {
    // Single file: use its parent directory (normalized)
    QFileInfo info(sourceFilePaths.first());
    QString root = normalizePath(info.path());
    return (root == ".") ? QString() : root;
  }

  // Split all paths into components (paths are already normalized)
  QVector<QStringList> allComponents;
  QString prefix;
  bool firstPathSet = false;
  bool firstIsAbsolute = false;
  for (const QString &path : sourceFilePaths) {
    bool isAbsolute = QDir::isAbsolutePath(path);
    if (!firstPathSet) {
      firstPathSet = true;
      firstIsAbsolute = isAbsolute;
      if (path.startsWith("//")) {
        prefix = "//";
      } else if (path.startsWith("/")) {
        prefix = "/";
      }
    } else if (isAbsolute != firstIsAbsolute) {
      return QString();
    }
    QStringList components = path.split('/', Qt::SkipEmptyParts);
    allComponents.append(components);
  }

  // Find common prefix length
  int minLength = std::numeric_limits<int>::max();
  for (const auto &components : allComponents) {
    minLength = qMin(minLength, components.size());
  }

  // Find common components (excluding filename, hence -1)
  QStringList commonComponents;
  for (int i = 0; i < minLength - 1; ++i) {
    QString component = allComponents[0][i];
    bool allMatch = true;
    for (int j = 1; j < allComponents.size(); ++j) {
      if (allComponents[j][i] != component) {
        allMatch = false;
        break;
      }
    }
    if (allMatch) {
      commonComponents.append(component);
    } else {
      break;
    }
  }

  if (commonComponents.isEmpty()) {
    // No common prefix - return empty to indicate all headers are external
    return QString();
  }

  QString root = prefix + commonComponents.join('/');
  return (root == ".") ? QString() : root;
}

QString TranslationUnitModel::computeProjectRootSmart(
    const QStringList &sourceFilePaths,
    const QString &compilationDbPath) const {
  if (sourceFilePaths.isEmpty()) {
    return QString();
  }

  // Step 1: Try simple common ancestor of ALL files
  QString commonRoot = computeProjectRoot(sourceFilePaths);
  if (!commonRoot.isEmpty()) {
    // All files share a common root - use it
    return commonRoot;
  }

  // Step 2: Files diverge - group by top-level directory and find majority
  // e.g., /home/user/project/... vs /tmp/gen/... → group by /home vs /tmp
  QMap<QString, QStringList> clusters; // top-level dir -> files under it

  for (const QString &path : sourceFilePaths) {
    QStringList components = path.split('/', Qt::SkipEmptyParts);
    if (components.isEmpty()) {
      continue;
    }
    // Use first component as cluster key (e.g., "home", "tmp", "var")
    QString topDir = "/" + components.first();
    clusters[topDir].append(path);
  }

  // Step 3: Find cluster with most files
  QString bestRoot;
  int maxCount = 0;
  QStringList tiedRoots;

  for (auto it = clusters.begin(); it != clusters.end(); ++it) {
    // Compute common root for this cluster
    QString clusterRoot = computeProjectRoot(it.value());
    if (clusterRoot.isEmpty()) {
      clusterRoot = it.key(); // Fall back to top-level dir
    }
    int count = it.value().size();

    if (count > maxCount) {
      maxCount = count;
      bestRoot = clusterRoot;
      tiedRoots.clear();
      tiedRoots.append(clusterRoot);
    } else if (count == maxCount) {
      tiedRoots.append(clusterRoot);
    }
  }

  // Step 4: If tied, pick the one that is parent of compile_commands.json
  if (tiedRoots.size() > 1 && !compilationDbPath.isEmpty()) {
    QString dbDir = normalizePath(QFileInfo(compilationDbPath).absolutePath());
    for (const QString &root : tiedRoots) {
      if (dbDir.startsWith(root + "/") || dbDir == root) {
        return root;
      }
    }
  }

  return bestRoot;
}

void TranslationUnitModel::buildDirectoryTree(
    QStandardItem *parent, const QStringList &filePaths,
    const QString &rootPath, const QHash<QString, QJsonObject> *fileDataMap) {

  if (filePaths.isEmpty()) {
    return;
  }

  // Build trie
  PathTreeNode root;
  root.name = QFileInfo(rootPath).fileName();
  if (root.name.isEmpty()) {
    root.name = rootPath;
  }

  const QString normalizedRootPath = normalizePathLexical(rootPath);

  for (const QString &path : filePaths) {
    // filePaths are already normalized by the caller; avoid re-canonicalizing
    insertPath(&root, path, normalizedRootPath, path);
  }

  QElapsedTimer yieldTimer;
  yieldTimer.start();

  auto maybeYield = [&yieldTimer]() {
    if (yieldTimer.elapsed() >= 35) {
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      yieldTimer.restart();
    }
  };

  // Convert trie to QStandardItem tree (recursive lambda)
  std::function<void(PathTreeNode *, QStandardItem *)> convertNode =
      [&](PathTreeNode *node, QStandardItem *parentItem) {
        QList<PathTreeNode *> sortedChildren = getSortedChildren(node);

        for (PathTreeNode *child : sortedChildren) {
          maybeYield();
          QStandardItem *item = new QStandardItem(child->name);
          item->setEditable(false);

          if (child->isFile) {
            item->setData(child->fullPath, FilePathRole);
            item->setToolTip(child->fullPath);

            // Check if this is a source file (has entry in fileDataMap)
            if (fileDataMap && fileDataMap->contains(child->fullPath)) {
              // Register with FileManager and store FileID
              FileID fileId =
                  fileManager_.registerFile(child->fullPath.toStdString());
              item->setData(QVariant::fromValue(fileId), FileIDRole);
              item->setData(true, IsSourceFileRole);

              // Get header count for tooltip
              QJsonObject fileObj = fileDataMap->value(child->fullPath);
              int headerCount = fileObj["headerCount"].toInt();
              item->setToolTip(
                  QString("%1\n%2 headers").arg(child->fullPath).arg(headerCount));

              // Store headers for lazy population when expanded / needed.
              item->setData(fileObj["headers"].toArray(), SourceHeadersRole);
              item->setData(false, HeadersFetchedRole);
              sourceItemByPath_.insert(child->fullPath, item);
            }
          } else {
            // Directory node
            item->setData(QString(), FilePathRole);
            convertNode(child, item);
          }

          parentItem->appendRow(item);
        }
      };

  convertNode(&root, parent);
}

void TranslationUnitModel::addHeaderCategory(QStandardItem *parent,
                                             const QString &categoryName,
                                             const QStringList &headers,
                                             const QString &rootPath) {
  if (headers.isEmpty()) {
    return;
  }

  QStandardItem *categoryItem = new QStandardItem(categoryName);
  categoryItem->setEditable(false);
  categoryItem->setData(QString(), FilePathRole);

  // Build tree for these headers
  buildDirectoryTree(categoryItem, headers, rootPath, nullptr);

  if (categoryItem->rowCount() > 0) {
    parent->appendRow(categoryItem);
  } else {
    delete categoryItem;
  }
}

void TranslationUnitModel::populateFromDependencies(
    const QJsonObject &dependenciesJson, const QString &overrideProjectRoot,
    const QString &compilationDbPath) {
  clear();

  QJsonArray filesArray = dependenciesJson["files"].toArray();

  // Phase 1: Collect all unique source file paths and their data
  QSet<QString> seenPaths;
  QStringList sourceFilePaths;
  QHash<QString, QJsonObject> fileDataMap;
  sourceFilePaths.reserve(filesArray.size());
  seenPaths.reserve(filesArray.size());
  fileDataMap.reserve(filesArray.size());

  for (const QJsonValue &fileValue : filesArray) {
    QJsonObject fileObj = fileValue.toObject();
    QString filePath = normalizePath(fileObj["path"].toString());
    if (!seenPaths.contains(filePath)) {
      seenPaths.insert(filePath);
      sourceFilePaths.append(filePath);
      fileDataMap.insert(filePath, fileObj);
    }
  }

  if (sourceFilePaths.isEmpty()) {
    return;
  }

  // Phase 2: Determine project root
  // Priority: 1) user override, 2) compute from source files
  if (!overrideProjectRoot.isEmpty()) {
    projectRoot_ = normalizePath(overrideProjectRoot);
  } else {
    projectRoot_ = computeProjectRootSmart(sourceFilePaths, compilationDbPath);
  }

  // Phase 3: Classify source files into project and external
  QStringList projectFiles;
  QStringList externalFiles;

  for (const QString &path : sourceFilePaths) {
    if (isPathUnderRoot(path, projectRoot_)) {
      projectFiles.append(path);
    } else {
      externalFiles.append(path);
    }
  }

  // Phase 4: Create "Project Files" root node
  if (!projectFiles.isEmpty()) {
    QString projectDisplayName =
        projectRoot_.isEmpty()
            ? QStringLiteral("Project Files")
            : QStringLiteral("Project Files (%1)")
                  .arg(QFileInfo(projectRoot_).fileName());

    QStandardItem *projectRootItem = new QStandardItem(projectDisplayName);
    projectRootItem->setEditable(false);
    projectRootItem->setData(QString(), FilePathRole);
    projectRootItem->setToolTip(
        projectRoot_.isEmpty() ? tr("Project files") : projectRoot_);

    buildDirectoryTree(projectRootItem, projectFiles, projectRoot_,
                       &fileDataMap);
    invisibleRootItem()->appendRow(projectRootItem);
  }

  // Phase 5: Create "External Files" root node (relative to /)
  if (!externalFiles.isEmpty()) {
    QStandardItem *externalRootItem =
        new QStandardItem(QStringLiteral("External Files"));
    externalRootItem->setEditable(false);
    externalRootItem->setData(QString(), FilePathRole);
    externalRootItem->setToolTip(tr("Files outside project root (relative to /)"));

    // Use "/" as root for external files so paths are shown as absolute-style
    buildDirectoryTree(externalRootItem, externalFiles, QStringLiteral("/"),
                       &fileDataMap);
    invisibleRootItem()->appendRow(externalRootItem);
  }
}

bool TranslationUnitModel::hasChildren(const QModelIndex &parent) const {
  if (parent.isValid() && canFetchMore(parent)) {
    return true;
  }
  return QStandardItemModel::hasChildren(parent);
}

bool TranslationUnitModel::canFetchMore(const QModelIndex &parent) const {
  if (!parent.isValid()) {
    return false;
  }
  QStandardItem *item = itemFromIndex(parent);
  if (!item) {
    return false;
  }
  if (!item->data(IsSourceFileRole).toBool()) {
    return false;
  }
  if (item->data(HeadersFetchedRole).toBool()) {
    return false;
  }
  QJsonArray headersArray = item->data(SourceHeadersRole).toJsonArray();
  return !headersArray.isEmpty();
}

void TranslationUnitModel::fetchMore(const QModelIndex &parent) {
  if (!canFetchMore(parent)) {
    return;
  }
  QStandardItem *item = itemFromIndex(parent);
  if (!item) {
    return;
  }
  populateHeadersForSourceItem(item);
}

void TranslationUnitModel::populateHeadersForSourceItem(
    QStandardItem *sourceItem) {
  if (!sourceItem) {
    return;
  }
  if (!sourceItem->data(IsSourceFileRole).toBool()) {
    return;
  }
  if (sourceItem->data(HeadersFetchedRole).toBool()) {
    return;
  }

  QJsonArray headersArray = sourceItem->data(SourceHeadersRole).toJsonArray();
  if (headersArray.isEmpty()) {
    sourceItem->setData(true, HeadersFetchedRole);
    return;
  }

  HeaderClassification headers = classifyHeaders(headersArray, projectRoot_);

  // Project Headers
  if (!headers.projectDirect.isEmpty() || !headers.projectIndirect.isEmpty()) {
    QStandardItem *projectHeaders = new QStandardItem("Project Headers");
    projectHeaders->setEditable(false);
    projectHeaders->setData(QString(), FilePathRole);

    addHeaderCategory(projectHeaders, "Direct", headers.projectDirect,
                      projectRoot_);
    addHeaderCategory(projectHeaders, "Indirect", headers.projectIndirect,
                      projectRoot_);

    if (projectHeaders->rowCount() > 0) {
      sourceItem->appendRow(projectHeaders);
    } else {
      delete projectHeaders;
    }
  }

  // External Headers
  if (!headers.externalDirect.isEmpty() || !headers.externalIndirect.isEmpty()) {
    QStandardItem *externalHeaders = new QStandardItem("External Headers");
    externalHeaders->setEditable(false);
    externalHeaders->setData(QString(), FilePathRole);

    QStringList allExternal = headers.externalDirect + headers.externalIndirect;
    QString externalRoot = computeProjectRoot(allExternal);

    addHeaderCategory(externalHeaders, "Direct", headers.externalDirect,
                      externalRoot);
    addHeaderCategory(externalHeaders, "Indirect", headers.externalIndirect,
                      externalRoot);

    if (externalHeaders->rowCount() > 0) {
      sourceItem->appendRow(externalHeaders);
    } else {
      delete externalHeaders;
    }
  }

  sourceItem->setData(true, HeadersFetchedRole);
}

QString TranslationUnitModel::getSourceFilePathFromIndex(
    const QModelIndex &index) const {
  if (!index.isValid()) {
    return QString();
  }

  QStandardItem *item = itemFromIndex(index);
  if (!item) {
    return QString();
  }

  QVariant pathVariant = item->data(FilePathRole);
  return pathVariant.toString();
}

QStringList TranslationUnitModel::getIncludedHeadersForSource(
    const QString &sourceFilePath) const {
  QStringList headers;

  QString normalizedSourcePath = normalizePath(sourceFilePath);
  QModelIndex sourceIndex = findIndexByFilePath(normalizedSourcePath);
  if (!sourceIndex.isValid()) {
    return headers;
  }

  QStandardItem *sourceItem = itemFromIndex(sourceIndex);
  if (!sourceItem) {
    return headers;
  }

  QVariant cached = sourceItem->data(CachedHeaderPathsRole);
  if (cached.isValid()) {
    return cached.toStringList();
  }

  QJsonArray headersArray = sourceItem->data(SourceHeadersRole).toJsonArray();
  if (!headersArray.isEmpty()) {
    headers.reserve(headersArray.size());
    for (const QJsonValue &headerValue : headersArray) {
      QJsonObject headerObj = headerValue.toObject();
      QString headerPath = normalizePath(headerObj["path"].toString());
      if (!headerPath.isEmpty()) {
        headers.append(headerPath);
      }
    }
    sourceItem->setData(headers, CachedHeaderPathsRole);
    return headers;
  }

  // Fallback: if headers have been populated in the tree, collect them.
  std::function<void(QStandardItem *)> collectHeaders =
      [&](QStandardItem *item) {
        QString path = item->data(FilePathRole).toString();
        if (!path.isEmpty() && path != normalizedSourcePath) {
          headers.append(path);
        }
        for (int i = 0; i < item->rowCount(); ++i) {
          collectHeaders(item->child(i));
        }
      };

  collectHeaders(sourceItem);
  sourceItem->setData(headers, CachedHeaderPathsRole);
  return headers;
}

QModelIndex
TranslationUnitModel::findIndexByFilePath(const QString &filePath) const {
  QString normalizedPath = normalizePath(filePath);
  auto it = sourceItemByPath_.find(normalizedPath);
  if (it != sourceItemByPath_.end() && it.value()) {
    return indexFromItem(it.value());
  }
  // Recursive search through tree
  std::function<QModelIndex(QStandardItem *)> searchItem =
      [&](QStandardItem *item) -> QModelIndex {
    QString itemPath = item->data(FilePathRole).toString();
    if (itemPath == normalizedPath &&
        item->data(IsSourceFileRole).toBool()) {
      return indexFromItem(item);
    }

    for (int i = 0; i < item->rowCount(); ++i) {
      QModelIndex found = searchItem(item->child(i));
      if (found.isValid()) {
        return found;
      }
    }

    return QModelIndex();
  };

  for (int i = 0; i < invisibleRootItem()->rowCount(); ++i) {
    QModelIndex found = searchItem(invisibleRootItem()->child(i));
    if (found.isValid()) {
      return found;
    }
  }

  return QModelIndex();
}

QModelIndex
TranslationUnitModel::findIndexByAnyFilePath(const QString &filePath) const {
  QString normalizedPath = normalizePath(filePath);

  std::function<QModelIndex(QStandardItem *)> searchItem =
      [&](QStandardItem *item) -> QModelIndex {
    QString itemPath = item->data(FilePathRole).toString();
    if (!itemPath.isEmpty() && itemPath == normalizedPath) {
      return indexFromItem(item);
    }

    for (int i = 0; i < item->rowCount(); ++i) {
      QModelIndex found = searchItem(item->child(i));
      if (found.isValid()) {
        return found;
      }
    }

    return QModelIndex();
  };

  for (int i = 0; i < invisibleRootItem()->rowCount(); ++i) {
    QModelIndex found = searchItem(invisibleRootItem()->child(i));
    if (found.isValid()) {
      return found;
    }
  }

  return QModelIndex();
}

QModelIndex TranslationUnitModel::findIndexByAnyFilePathUnder(
    const QString &filePath, const QModelIndex &root) const {
  QString normalizedPath = normalizePath(filePath);
  if (!root.isValid()) {
    return QModelIndex();
  }

  QStandardItem *rootItem = itemFromIndex(root);
  if (!rootItem) {
    return QModelIndex();
  }

  std::function<QModelIndex(QStandardItem *)> searchItem =
      [&](QStandardItem *item) -> QModelIndex {
    QString itemPath = item->data(FilePathRole).toString();
    if (!itemPath.isEmpty() && itemPath == normalizedPath) {
      return indexFromItem(item);
    }

    for (int i = 0; i < item->rowCount(); ++i) {
      QModelIndex found = searchItem(item->child(i));
      if (found.isValid()) {
        return found;
      }
    }

    return QModelIndex();
  };

  return searchItem(rootItem);
}

QModelIndex TranslationUnitModel::findIndexByFileId(FileID fileId) const {
  // Recursive search through tree
  std::function<QModelIndex(QStandardItem *)> searchItem =
      [&](QStandardItem *item) -> QModelIndex {
    QVariant fileIdVariant = item->data(FileIDRole);
    if (fileIdVariant.isValid() && fileIdVariant.value<FileID>() == fileId) {
      return indexFromItem(item);
    }

    for (int i = 0; i < item->rowCount(); ++i) {
      QModelIndex found = searchItem(item->child(i));
      if (found.isValid()) {
        return found;
      }
    }

    return QModelIndex();
  };

  for (int i = 0; i < invisibleRootItem()->rowCount(); ++i) {
    QModelIndex found = searchItem(invisibleRootItem()->child(i));
    if (found.isValid()) {
      return found;
    }
  }

  return QModelIndex();
}

void TranslationUnitModel::clear() {
  projectRoot_.clear();
  sourceItemByPath_.clear();
  QStandardItemModel::clear();
  setHorizontalHeaderLabels(QStringList() << "Files");
}

} // namespace acav
