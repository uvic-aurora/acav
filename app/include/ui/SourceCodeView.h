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

/// \file SourceCodeView.h
/// \brief Source code display widget with line numbers.
#pragma once

#include "common/FileManager.h"
#include "core/SourceLocation.h"
#include <QPlainTextEdit>
#include <QPoint>
#include <QString>
#include <QSyntaxHighlighter>
#include <QTextCursor>
#include <QTextDocument>

namespace acav {

class LineNumberArea;  // Forward declaration

/// \brief Source code viewer with line numbers and lightweight syntax highlighting
///
/// This widget displays source code files in a read-only text editor with
/// line numbers in the left margin and lightweight C/C++ syntax highlighting.
///
class SourceCodeView : public QPlainTextEdit {
  Q_OBJECT

public:
  explicit SourceCodeView(QWidget *parent = nullptr);
  ~SourceCodeView() override = default;

  /// \brief Load and display a source file
  /// \param filePath Absolute path to the source file
  /// \return true if file was loaded successfully, false otherwise
  bool loadFile(const QString &filePath);

  /// \brief Clear the view
  void clearView();

  /// \brief Update font size and refresh editor metrics
  void applyFontSize(int pointSize);

  /// \brief Get the currently loaded file path
  QString currentFilePath() const { return currentFilePath_; }

  /// \brief Get the current file ID
  FileID currentFileId() const { return currentFileId_; }

  /// \brief Set the current file ID
  /// Should be called after loading a file
  void setCurrentFileId(FileID fileId) { currentFileId_ = fileId; }

  /// \brief Highlight source range with light green background
  /// \param range Source range to highlight
  void highlightRange(const SourceRange &range, bool moveCursor = true);

  /// \brief Clear current highlight
  void clearHighlight();

  /// \brief Find next occurrence of the term and highlight it
  bool findNext(const QString &term,
                QTextDocument::FindFlags flags = QTextDocument::FindFlags());

  /// \brief Find previous occurrence of the term and highlight it
  bool findPrevious(
      const QString &term,
      QTextDocument::FindFlags flags = QTextDocument::FindFlags());

  /// \brief Clear any active search highlight
  void clearSearchHighlight();

  /// \brief Calculate required width for line number area
  /// \return Width in pixels for current line count
  int lineNumberAreaWidth() const;

  /// \brief Paint line numbers for visible text blocks
  /// Called by LineNumberArea::paintEvent().
  /// \param event Paint event with update rectangle
  void lineNumberAreaPaintEvent(QPaintEvent *event);

signals:
  /// \brief Emitted when a file is successfully loaded
  void fileLoaded(const QString &filePath);

  /// \brief Emitted when file loading fails
  void fileLoadError(const QString &errorMessage);

  /// \brief Emitted when user clicks in editor
  /// \param fileId File ID of current file
  /// \param line Line number (1-based)
  /// \param column Column number (1-based)
  void sourcePositionClicked(FileID fileId, unsigned line, unsigned column);

  /// \brief Emitted when user selects a range in the editor
  /// \param fileId File ID of current file
  /// \param startLine Start line number (1-based)
  /// \param startColumn Start column number (1-based)
  /// \param endLine End line number (1-based)
  /// \param endColumn End column number (1-based)
  void sourceRangeSelected(FileID fileId, unsigned startLine,
                           unsigned startColumn, unsigned endLine,
                           unsigned endColumn);

protected:
  /// \brief Override to resize line number area with editor
  void resizeEvent(QResizeEvent *event) override;

  /// \brief Override to detect mouse press for selection tracking
  void mousePressEvent(QMouseEvent *event) override;

  /// \brief Override to detect mouse selection end
  void mouseReleaseEvent(QMouseEvent *event) override;

  /// \brief Override to emit navigation on keyboard movement
  void keyPressEvent(QKeyEvent *event) override;

private slots:
  /// \brief Update line number area width when block count changes
  void updateLineNumberAreaWidth(int newBlockCount);

  /// \brief Update line number area position/visibility on scroll
  void updateLineNumberArea(const QRect &rect, int dy);

  /// \brief Highlight current line for better navigation
  void highlightCurrentLine();

private:
  QString currentFilePath_;
  FileID currentFileId_ = FileManager::InvalidFileID;
  LineNumberArea *lineNumberArea_;  // Owned via Qt parent-child
  QSyntaxHighlighter *keywordHighlighter_ = nullptr; // Owned by document
  QTextEdit::ExtraSelection navigationHighlight_;  // Stores AST navigation highlight
  QTextEdit::ExtraSelection searchHighlight_; // Stores search match highlight
  QTextCursor searchCursor_;
  QString lastSearchTerm_;
  bool suppressCursorSignal_ = false;
  bool mouseSelectionActive_ = false;

  void setupEditor();
  void updateHighlights();  // Update all highlights (navigation, etc.)
  bool performFind(const QString &term, QTextDocument::FindFlags flags);
  void setSearchHighlight(const QTextCursor &cursor);
  void emitCursorPosition();
  void emitSelectionRange();

  int pressSelectionStart_ = 0;
  int pressSelectionEnd_ = 0;
  QPoint pressMousePos_;
};

} // namespace acav
