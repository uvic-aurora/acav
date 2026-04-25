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

/// \file OpenProjectDialog.cpp
/// \brief Implementation of the Open Project dialog.
#include "ui/OpenProjectDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>

namespace acav {

namespace {

QString expandUserPath(const QString &path) {
  if (path.startsWith("~")) {
    return QDir::homePath() + path.mid(1); // Handles both "~" and "~/"
  }
  return path;
}

} // namespace

OpenProjectDialog::OpenProjectDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Open Project"));
  setMinimumWidth(400);
  resize(700, 120); // Larger initial size, user can resize as needed

  auto *layout = new QVBoxLayout(this);

  // Form layout for inputs
  auto *formLayout = new QFormLayout();

  // Compilation database row
  auto *dbRow = new QHBoxLayout();
  dbPathEdit_ = new QLineEdit(this);
  // Don't need placeholder
  // dbPathEdit_->setPlaceholderText(tr("/path/to/compile_commands.json"));
  dbPathEdit_->setClearButtonEnabled(true);
  dbBrowseButton_ = new QPushButton(tr("Browse..."), this);
  dbRow->addWidget(dbPathEdit_);
  dbRow->addWidget(dbBrowseButton_);
  formLayout->addRow(tr("Compilation Database:"), dbRow);

  // Project root row (optional)
  auto *projectRow = new QHBoxLayout();
  projectRootEdit_ = new QLineEdit(this);
  projectRootEdit_->setClearButtonEnabled(true);
  projectRootBrowseButton_ = new QPushButton(tr("Browse..."), this);
  projectRow->addWidget(projectRootEdit_);
  projectRow->addWidget(projectRootBrowseButton_);
  formLayout->addRow(tr("Project Root (optional):"), projectRow);

  layout->addLayout(formLayout);

  // Dialog buttons
  auto *buttonBox =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(buttonBox);

  // Connections
  connect(dbBrowseButton_, &QPushButton::clicked, this,
          &OpenProjectDialog::browseCompilationDatabase);
  connect(projectRootBrowseButton_, &QPushButton::clicked, this,
          &OpenProjectDialog::browseProjectRoot);
  connect(buttonBox, &QDialogButtonBox::accepted, this,
          &OpenProjectDialog::validateAndAccept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  dbPathEdit_->setFocus();
}

QString OpenProjectDialog::compilationDatabasePath() const {
  return dbPathEdit_->text();
}

QString OpenProjectDialog::projectRootPath() const {
  return projectRootEdit_->text();
}

void OpenProjectDialog::browseCompilationDatabase() {
  QString initialDir;
  QString current = dbPathEdit_->text().trimmed();
  if (!current.isEmpty()) {
    QFileInfo info(current);
    initialDir = info.absolutePath();
  }
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Select Compilation Database"), initialDir,
      tr("Compilation Database (*.json);;All Files (*)"));
  if (!fileName.isEmpty()) {
    dbPathEdit_->setText(fileName);
  }
}

void OpenProjectDialog::browseProjectRoot() {
  QString dir = QFileDialog::getExistingDirectory(
      this, tr("Select Project Root Directory"),
      projectRootEdit_->text().trimmed(), QFileDialog::ShowDirsOnly);
  if (!dir.isEmpty()) {
    projectRootEdit_->setText(dir);
  }
}

void OpenProjectDialog::validateAndAccept() {
  const QString rawDbPath = dbPathEdit_->text().trimmed();
  if (rawDbPath.isEmpty()) {
    QMessageBox::warning(this, tr("Validation Error"),
                         tr("Please enter a compilation database path."));
    return;
  }

  const QString expandedDbPath = expandUserPath(rawDbPath);
  QFileInfo dbInfo(expandedDbPath);
  if (!dbInfo.exists() || !dbInfo.isFile()) {
    QMessageBox::warning(
        this, tr("Validation Error"),
        tr("Compilation database not found:\n%1").arg(expandedDbPath));
    return;
  }

  dbPathEdit_->setText(dbInfo.absoluteFilePath());

  const QString rawProjectRoot = projectRootEdit_->text().trimmed();
  if (!rawProjectRoot.isEmpty()) {
    const QString expandedProjectRoot = expandUserPath(rawProjectRoot);
    QFileInfo rootInfo(expandedProjectRoot);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
      QMessageBox::warning(
          this, tr("Validation Error"),
          tr("Project root directory not found:\n%1").arg(expandedProjectRoot));
      return;
    }
    projectRootEdit_->setText(rootInfo.absoluteFilePath());
  } else {
    projectRootEdit_->clear();
  }

  accept();
}

} // namespace acav
