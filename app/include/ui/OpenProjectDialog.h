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

/// \file OpenProjectDialog.h
/// \brief Dialog for opening a project with compilation database and optional project root.
#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

namespace acav {

/// \brief Dialog for opening a project
///
/// Provides input fields for:
/// - Compilation database path (required)
/// - Project root directory (optional, auto-detected if not specified)
///
class OpenProjectDialog : public QDialog {
  Q_OBJECT

public:
  explicit OpenProjectDialog(QWidget *parent = nullptr);

  /// \brief Get the selected compilation database path
  /// \return Path to compilation database file
  QString compilationDatabasePath() const;

  /// \brief Get the selected project root path
  /// \return Path to project root, or empty string if not specified
  QString projectRootPath() const;

private slots:
  void browseCompilationDatabase();
  void browseProjectRoot();
  void validateAndAccept();

private:
  QLineEdit *dbPathEdit_;
  QLineEdit *projectRootEdit_;
  QPushButton *dbBrowseButton_;
  QPushButton *projectRootBrowseButton_;
};

} // namespace acav
