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

/// \file LogDock.cpp
/// \brief Implementation of LogDock.

#include "ui/LogDock.h"
#include <QMenu>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace acav {

namespace {

QPlainTextEdit *createLogView(QWidget *parent) {
  auto *view = new QPlainTextEdit(parent);
  view->setReadOnly(true);
  view->setLineWrapMode(QPlainTextEdit::NoWrap);
  view->setMaximumBlockCount(5000);
  view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  view->setMinimumHeight(0); // Allow dock to be resized small
  return view;
}

QString levelToString(LogLevel level) {
  switch (level) {
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Warning:
    return "WARNING";
  case LogLevel::Error:
    return "ERROR";
  }
  return "INFO";
}

} // namespace

LogDock::LogDock(QWidget *parent)
    : QDockWidget(tr("Logs"), parent), tabs_(new QTabWidget(this)),
      allView_(createLogView(tabs_)), errorView_(createLogView(tabs_)),
      infoView_(createLogView(tabs_)), debugView_(createLogView(tabs_)),
      flushTimer_(new QTimer(this)) {
  setObjectName("logDock");

  auto *container = new QWidget(this);
  auto *layout = new QVBoxLayout(container);
  layout->setContentsMargins(6, 4, 6, 4);
  layout->setSpacing(4);
  layout->addWidget(tabs_);

  tabs_->addTab(allView_, tr("All"));
  tabs_->addTab(errorView_, tr("Errors"));
  tabs_->addTab(infoView_, tr("Info"));
  tabs_->addTab(debugView_, tr("Debug"));
  tabs_->setMinimumHeight(0); // Allow dock to be resized small
  setWidget(container);

  auto setupContextMenu = [this](QPlainTextEdit *view) {
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QWidget::customContextMenuRequested, this,
            [this, view](const QPoint &pos) {
              QMenu *menu = view->createStandardContextMenu();
              menu->addSeparator();
              QAction *clearAction = menu->addAction(tr("Clear"));
              QAction *chosen =
                  menu->exec(view->viewport()->mapToGlobal(pos));
              menu->deleteLater();
              if (chosen == clearAction) {
                clearAll();
              }
            });
  };
  setupContextMenu(allView_);
  setupContextMenu(errorView_);
  setupContextMenu(infoView_);
  setupContextMenu(debugView_);

  flushTimer_->setInterval(100);
  connect(flushTimer_, &QTimer::timeout, this, &LogDock::flushPending);
  flushTimer_->start();
}

void LogDock::enqueue(const LogEntry &entry) {
  QMutexLocker locker(&mutex_);
  LogEntry normalized = entry;
  if (normalized.level == LogLevel::Warning) {
    normalized.level = LogLevel::Info;
  }
  pending_.append(normalized);
  if (pending_.size() > maxPendingSize_) {
    const int dropCount = pending_.size() - maxPendingSize_;
    pending_.erase(pending_.begin(), pending_.begin() + dropCount);
  }
}

void LogDock::flushPending() {
  QVector<LogEntry> batch;
  {
    // Lock when output log contents
    QMutexLocker locker(&mutex_);
    if (pending_.isEmpty()) {
      return;
    }
    const int takeCount =
        std::min(maxBatchSize_, static_cast<int>(pending_.size()));
    batch = pending_.mid(0, takeCount);
    pending_.erase(pending_.begin(), pending_.begin() + takeCount);
  }

  appendBatch(batch);
}

void LogDock::appendBatch(const QVector<LogEntry> &batch) {
  QString allText;
  QString errorText;
  QString infoText;
  QString debugText;

  allText.reserve(batch.size() * 64);

  for (const LogEntry &entry : batch) {
    QString line = formatEntry(entry);
    allText += line + "\n";
    switch (entry.level) {
    case LogLevel::Error:
      errorText += line + "\n";
      break;
    case LogLevel::Warning:
    case LogLevel::Info:
      infoText += line + "\n";
      break;
    case LogLevel::Debug:
      debugText += line + "\n";
      break;
    }
  }

  appendText(allView_, allText);
  appendText(errorView_, errorText);
  appendText(infoView_, infoText);
  appendText(debugView_, debugText);
}

void LogDock::appendText(QPlainTextEdit *view, const QString &text) {
  QString clipped = text;
  if (clipped.endsWith('\n')) {
    clipped.chop(1);
  }
  if (clipped.isEmpty()) {
    return;
  }

  QScrollBar *scrollBar = view->verticalScrollBar();
  const int previousValue = scrollBar->value();
  const bool wasAtBottom = previousValue >= scrollBar->maximum() - 1;

  view->appendPlainText(clipped);

  if (autoScroll_ && wasAtBottom) {
    scrollBar->setValue(scrollBar->maximum());
  } else {
    scrollBar->setValue(previousValue);
  }
}

QString LogDock::formatEntry(const LogEntry &entry) {
  const QString time = entry.timestamp.toString("yyyy-MM-dd HH:mm:ss");
  const QString level = levelToString(entry.level);
  const QString source = entry.source.isEmpty() ? "acav" : entry.source;
  return QString("[%1] [%2] [%3] %4").arg(time, level, source, entry.message);
}

void LogDock::focusAllTab() {
  tabs_->setCurrentIndex(0); // "All" tab is index 0
  allView_->setFocus();
}

void LogDock::clearAll() {
  {
    QMutexLocker locker(&mutex_);
    pending_.clear();
  }

  allView_->clear();
  errorView_->clear();
  infoView_->clear();
  debugView_->clear();
}

} // namespace acav
