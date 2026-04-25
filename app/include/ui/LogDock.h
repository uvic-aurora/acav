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

/// \file LogDock.h
/// \brief Dock widget for displaying logs and diagnostics.
#pragma once

#include "core/LogEntry.h"
#include <QDockWidget>
#include <QMutex>
#include <QTimer>
#include <QVector>

class QPlainTextEdit;
class QTabWidget;

namespace acav {

class LogDock : public QDockWidget {
  Q_OBJECT

public:
  explicit LogDock(QWidget *parent = nullptr);

public slots:
  void enqueue(const LogEntry &entry);

  /// \brief Switch to All tab and set focus
  void focusAllTab();

private slots:
  void flushPending();
  void clearAll();

private:
  // For safety print log to avoid log flood
  void appendBatch(const QVector<LogEntry> &batch);
  void appendText(QPlainTextEdit *view, const QString &text);
  static QString formatEntry(const LogEntry &entry);

  QTabWidget *tabs_;
  QPlainTextEdit *allView_;
  QPlainTextEdit *errorView_;
  QPlainTextEdit *infoView_;
  QPlainTextEdit *debugView_;
  bool autoScroll_ = true;

  QMutex mutex_;
  QVector<LogEntry> pending_;
  QTimer *flushTimer_;
  int maxBatchSize_ = 500;
  int maxPendingSize_ = 20000;
};

} // namespace acav
