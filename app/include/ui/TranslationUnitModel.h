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

/// \file TranslationUnitModel.h
/// \brief Qt model for translation unit tree view.
#pragma once

#include "common/FileManager.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QMap>
#include <QStandardItemModel>
#include <QString>

namespace acav {

/// \brief Model for displaying translation units in a tree view
///
/// This model represents the list of source files from the compilation database
/// and their dependencies. It parses the JSON output from query-dependencies
/// and displays it in a hierarchical structure with folders for direct and
/// indirect includes.
///
/// Uses FileManager for consistent file identification via FileID throughout
/// the application. File paths are stored for display purposes only.
///
class TranslationUnitModel : public QStandardItemModel {
  Q_OBJECT

public:
  explicit TranslationUnitModel(FileManager &fileManager, QObject *parent = nullptr);
  ~TranslationUnitModel() override = default;

  bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
  bool canFetchMore(const QModelIndex &parent) const override;
  void fetchMore(const QModelIndex &parent) override;

  /// \brief Populate model from query-dependencies JSON output
  /// \param dependenciesJson JSON object with structure:
  ///   {
  ///     "statistics": { "sourceFileCount": N, "totalHeaderCount": M },
  ///     "files": [ { "path": "...", "headerCount": N, "headers": [...] } ]
  ///   }
  /// \param overrideProjectRoot Optional project root override (computed from sources if empty)
  /// \param compilationDbPath Path to compile_commands.json (used for tie-breaking)
  void populateFromDependencies(const QJsonObject &dependenciesJson,
                                const QString &overrideProjectRoot = QString(),
                                const QString &compilationDbPath = QString());

  /// \brief Get the source file path from a model index
  /// \param index QModelIndex from the tree view
  /// \return Full path to the source file, or empty string if not a file node
  QString getSourceFilePathFromIndex(const QModelIndex &index) const;

  /// \brief Get all included headers for a source file
  /// \param sourceFilePath Path to the source file
  /// \return List of all included headers (direct + indirect)
  QStringList getIncludedHeadersForSource(const QString &sourceFilePath) const;

  /// \brief Find model index by file path (legacy method for compatibility)
  /// \param filePath Full path to the file to find
  /// \return QModelIndex for the file, or invalid index if not found
  QModelIndex findIndexByFilePath(const QString &filePath) const;
  QModelIndex findIndexByAnyFilePath(const QString &filePath) const;
  QModelIndex findIndexByAnyFilePathUnder(const QString &filePath,
                                          const QModelIndex &root) const;

  /// \brief Find model index by FileID
  /// \param fileId FileID to find
  /// \return QModelIndex for the file, or invalid index if not found
  QModelIndex findIndexByFileId(FileID fileId) const;

  /// \brief Clear all data from the model
  void clear();

  /// \brief Get the computed project root directory
  /// \return Project root path, or empty string if not yet populated
  QString projectRoot() const { return projectRoot_; }

private:
  /// \brief Compute project root from source file paths (simple common ancestor)
  /// \param sourceFilePaths List of all source file paths
  /// \return Common parent directory of all source files
  QString computeProjectRoot(const QStringList &sourceFilePaths) const;

  /// \brief Smart project root computation with majority voting and tie-breaking
  /// \param sourceFilePaths List of all source file paths
  /// \param compilationDbPath Path to compile_commands.json for tie-breaking
  /// \return Best project root based on: common ancestor, majority, or db parent
  QString computeProjectRootSmart(const QStringList &sourceFilePaths,
                                  const QString &compilationDbPath) const;

  /// \brief Build directory tree structure from file paths
  /// \param parent Parent item to add children to
  /// \param filePaths List of file paths to organize
  /// \param rootPath Root path for computing relative paths
  /// \param fileDataMap Map of file path to JSON data (for source files)
  void buildDirectoryTree(QStandardItem *parent, const QStringList &filePaths,
                          const QString &rootPath,
                          const QHash<QString, QJsonObject> *fileDataMap);

  /// \brief Add header category (Project/External, Direct/Indirect) to source file
  /// \param parent Parent item (the source file)
  /// \param categoryName Name of the category
  /// \param headers List of header paths
  /// \param rootPath Root path for tree structure
  void addHeaderCategory(QStandardItem *parent, const QString &categoryName,
                         const QStringList &headers, const QString &rootPath);

  void populateHeadersForSourceItem(QStandardItem *sourceItem);

  FileManager &fileManager_;  ///< Reference to file manager for FileID registration
  QString projectRoot_;       ///< Computed project root directory
  QHash<QString, QStandardItem *> sourceItemByPath_;
};

} // namespace acav
