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

#include "ui/SourceCodeView.h"
#include "ui/CppSyntaxHighlighter.h"
#include "ui/LineNumberArea.h"
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextStream>

namespace acav {

namespace {

class CursorSignalBlocker {
public:
  explicit CursorSignalBlocker(bool &flag) : flag_(flag), previous_(flag) {
    flag_ = true;
  }
  ~CursorSignalBlocker() { flag_ = previous_; }

private:
  bool &flag_;
  bool previous_;
};

} // namespace

SourceCodeView::SourceCodeView(QWidget *parent)
    : QPlainTextEdit(parent), lineNumberArea_(nullptr) {
  setupEditor();
  keywordHighlighter_ = new CppSyntaxHighlighter(document());

  // Create line number area
  lineNumberArea_ = new LineNumberArea(this);

  // Connect signals for line number updates
  connect(this, &SourceCodeView::blockCountChanged, this,
          &SourceCodeView::updateLineNumberAreaWidth);
  connect(this, &SourceCodeView::updateRequest, this,
          &SourceCodeView::updateLineNumberArea);
  connect(this, &SourceCodeView::cursorPositionChanged, this,
          &SourceCodeView::highlightCurrentLine);

  // Initial setup
  updateLineNumberAreaWidth(0);
  highlightCurrentLine();
}

void SourceCodeView::setupEditor() {
  // Make read-only but allow cursor movement with keyboard
  setReadOnly(true);
  setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);

  // Use the default UI font
  QFontMetrics metrics(font());
  setTabStopDistance(4 * metrics.horizontalAdvance(' '));

  // Enable line wrapping
  setLineWrapMode(QPlainTextEdit::NoWrap);

  // Enable always-visible scroll bars
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  // Set some basic styling
  setObjectName("sourceCodeView");
  // Styled by QPlainTextEdit#sourceCodeView in style.qss
}

void SourceCodeView::applyFontSize(int pointSize) {
  QFont font = this->font();
  font.setPointSize(pointSize);
  setFont(font);

  QFontMetrics metrics(font);
  setTabStopDistance(4 * metrics.horizontalAdvance(' '));
  updateLineNumberAreaWidth(0);
  lineNumberArea_->update();
}

bool SourceCodeView::loadFile(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QString errorMsg =
        QString("Failed to open file: %1").arg(file.errorString());
    emit fileLoadError(errorMsg);
    return false;
  }

  QTextStream in(&file);
  QString content = in.readAll();
  file.close();

  // QPlainTextEdit treats a trailing line break as a new empty block, which
  // shows up as an extra blank line/line number. Trim exactly one trailing
  // line break so the displayed line count matches common tools/editors.
  if (content.endsWith(QStringLiteral("\r\n"))) {
    content.chop(2);
  } else if (content.endsWith('\n') || content.endsWith('\r')) {
    content.chop(1);
  }

  CursorSignalBlocker blockSignals(suppressCursorSignal_);
  setPlainText(content);
  clearSearchHighlight();
  searchCursor_ = QTextCursor();
  lastSearchTerm_.clear();
  currentFilePath_ = filePath;

  emit fileLoaded(filePath);
  return true;
}

void SourceCodeView::clearView() {
  clear();
  currentFilePath_.clear();
  currentFileId_ = FileManager::InvalidFileID;
  clearHighlight();
  clearSearchHighlight();
  searchCursor_ = QTextCursor();
  lastSearchTerm_.clear();
}

void SourceCodeView::highlightRange(const SourceRange &range, bool moveCursor) {
  // Verify range is in current file
  if (range.begin().fileID() != currentFileId_ ||
      currentFileId_ == FileManager::InvalidFileID) {
    return;
  }

  // Convert SourceRange to QTextCursor
  QTextDocument *doc = document();

  // Find begin position (line and column are 1-based)
  QTextBlock beginBlock = doc->findBlockByLineNumber(range.begin().line() - 1);
  if (!beginBlock.isValid()) {
    return;
  }
  int beginPos = beginBlock.position() + range.begin().column() - 1;

  // Find end position
  QTextBlock endBlock = doc->findBlockByLineNumber(range.end().line() - 1);
  if (!endBlock.isValid()) {
    return;
  }
  int endPos = endBlock.position() + range.end().column() - 1;

  // Create cursor and select range
  QTextCursor cursor(doc);
  cursor.setPosition(beginPos);
  cursor.setPosition(endPos, QTextCursor::KeepAnchor);

  // Store navigation highlight
  navigationHighlight_.cursor = cursor;
  QPalette pal = palette();
  navigationHighlight_.format.setBackground(
      pal.brush(QPalette::Highlight));
  navigationHighlight_.format.setForeground(
      pal.brush(QPalette::HighlightedText));

  // Update display
  updateHighlights();

  if (moveCursor) {
    // Scroll to beginning of selection
    CursorSignalBlocker blockSignals(suppressCursorSignal_);
    QTextCursor scrollCursor = textCursor();
    scrollCursor.setPosition(beginPos);
    setTextCursor(scrollCursor);
    ensureCursorVisible();
  }
}

bool SourceCodeView::findNext(const QString &term,
                              QTextDocument::FindFlags flags) {
  return performFind(term, flags);
}

bool SourceCodeView::findPrevious(const QString &term,
                                  QTextDocument::FindFlags flags) {
  return performFind(term, flags | QTextDocument::FindBackward);
}

void SourceCodeView::clearSearchHighlight() {
  searchHighlight_ = QTextEdit::ExtraSelection();
  searchCursor_ = QTextCursor();
  updateHighlights();
}

void SourceCodeView::clearHighlight() {
  navigationHighlight_ = QTextEdit::ExtraSelection();
  updateHighlights();
}

void SourceCodeView::updateHighlights() {
  QList<QTextEdit::ExtraSelection> extraSelections;

  // Add search highlight first so navigation highlight can override color
  if (!searchHighlight_.cursor.isNull()) {
    extraSelections.append(searchHighlight_);
  }

  // Add navigation highlight if present
  if (!navigationHighlight_.cursor.isNull()) {
    extraSelections.append(navigationHighlight_);
  }

  setExtraSelections(extraSelections);
}

bool SourceCodeView::performFind(const QString &term,
                                 QTextDocument::FindFlags flags) {
  if (term.isEmpty()) {
    clearSearchHighlight();
    lastSearchTerm_.clear();
    return false;
  }

  QTextDocument *doc = document();
  if (!doc) {
    return false;
  }

  const bool backward = flags.testFlag(QTextDocument::FindBackward);
  QTextCursor startCursor;

  if (lastSearchTerm_ == term && !searchCursor_.isNull()) {
    startCursor = searchCursor_;
    if (backward) {
      startCursor.setPosition(searchCursor_.selectionStart());
    } else {
      startCursor.setPosition(searchCursor_.selectionEnd());
    }
  } else {
    startCursor = textCursor();
  }

  QTextCursor match = doc->find(term, startCursor, flags);

  if (match.isNull()) {
    QTextCursor wrapCursor(doc);
    if (backward) {
      wrapCursor.movePosition(QTextCursor::End);
    } else {
      wrapCursor.movePosition(QTextCursor::Start);
    }
    match = doc->find(term, wrapCursor, flags);
  }

  if (match.isNull()) {
    clearSearchHighlight();
    searchCursor_ = QTextCursor();
    return false;
  }

  lastSearchTerm_ = term;
  searchCursor_ = match;
  CursorSignalBlocker blockSignals(suppressCursorSignal_);
  setTextCursor(match);
  setSearchHighlight(match);
  ensureCursorVisible();
  return true;
}

void SourceCodeView::setSearchHighlight(const QTextCursor &cursor) {
  searchHighlight_ = QTextEdit::ExtraSelection();
  searchHighlight_.cursor = cursor;
  searchHighlight_.format.setBackground(QColor(255, 235, 59, 160));
  updateHighlights();
}

int SourceCodeView::lineNumberAreaWidth() const {
  int digits = 1;
  int max = qMax(1, blockCount());
  while (max >= 10) {
    max /= 10;
    ++digits;
  }

  // Space for digits + padding on both sides
  int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
  return space;
}

void SourceCodeView::lineNumberAreaPaintEvent(QPaintEvent *event) {
  QPainter painter(lineNumberArea_);
  painter.fillRect(event->rect(),
                   QColor(240, 240, 240)); // Light gray background

  // Use the same font as the editor
  painter.setFont(font());

  QTextBlock block = firstVisibleBlock();
  int blockNumber = block.blockNumber();
  int top =
      qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
  int bottom = top + qRound(blockBoundingRect(block).height());

  while (block.isValid() && top <= event->rect().bottom()) {
    if (block.isVisible() && bottom >= event->rect().top()) {
      QString number = QString::number(blockNumber + 1); // 1-based line numbers
      painter.setPen(QColor(100, 100, 100));             // Dark gray text
      painter.drawText(0, top, lineNumberArea_->width(), fontMetrics().height(),
                       Qt::AlignRight, number);
    }

    block = block.next();
    top = bottom;
    bottom = top + qRound(blockBoundingRect(block).height());
    ++blockNumber;
  }
}

void SourceCodeView::resizeEvent(QResizeEvent *event) {
  QPlainTextEdit::resizeEvent(event);

  QRect cr = contentsRect();
  lineNumberArea_->setGeometry(
      QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void SourceCodeView::updateLineNumberAreaWidth(int /* newBlockCount */) {
  setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void SourceCodeView::updateLineNumberArea(const QRect &rect, int dy) {
  if (dy) {
    lineNumberArea_->scroll(0, dy);
  } else {
    lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(),
                            rect.height());
  }

  if (rect.contains(viewport()->rect())) {
    updateLineNumberAreaWidth(0);
  }
}

void SourceCodeView::highlightCurrentLine() {
  // Don't highlight current line - we're read-only and it would interfere
  // with navigation highlighting. Just update the existing highlights.
  updateHighlights();
  if (!suppressCursorSignal_) {
    emitCursorPosition();
  }
}

void SourceCodeView::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    clearHighlight();
    clearSearchHighlight();
    mouseSelectionActive_ = true;
    pressMousePos_ = event->pos();
    QTextCursor cursor = textCursor();
    pressSelectionStart_ = cursor.selectionStart();
    pressSelectionEnd_ = cursor.selectionEnd();
  }

  // Call base implementation after capturing current state
  QPlainTextEdit::mousePressEvent(event);

  // mouseSelectionActive_ stays true until release.
}

void SourceCodeView::mouseReleaseEvent(QMouseEvent *event) {
  // Call base implementation first
  QPlainTextEdit::mouseReleaseEvent(event);

  if (event->button() != Qt::LeftButton) {
    return;
  }

  mouseSelectionActive_ = false;

  QTextCursor cursor = textCursor();
  bool selectionChanged = cursor.selectionStart() != pressSelectionStart_ ||
                          cursor.selectionEnd() != pressSelectionEnd_;
  bool movedEnough =
      (event->pos() - pressMousePos_).manhattanLength() >=
      QApplication::startDragDistance();

  if (cursor.hasSelection() && (selectionChanged || movedEnough)) {
    emitSelectionRange();
    return;
  }

  emitCursorPosition();
}

void SourceCodeView::keyPressEvent(QKeyEvent *event) {
  // Handle Home/End keys for document navigation (not line navigation)
  // This makes behavior consistent with other panes (AST Tree, File Explorer)
  if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
    QTextCursor cursor = textCursor();
    int oldPos = cursor.position();
    int oldSelStart = cursor.selectionStart();
    int oldSelEnd = cursor.selectionEnd();

    QTextCursor::MoveMode moveMode = event->modifiers() & Qt::ShiftModifier
                                         ? QTextCursor::KeepAnchor
                                         : QTextCursor::MoveAnchor;

    if (event->key() == Qt::Key_Home) {
      cursor.movePosition(QTextCursor::Start, moveMode);
    } else {
      cursor.movePosition(QTextCursor::End, moveMode);
    }

    setTextCursor(cursor);
    ensureCursorVisible();

    if (currentFileId_ == FileManager::InvalidFileID) {
      return;
    }

    if (cursor.hasSelection() && (cursor.selectionStart() != oldSelStart ||
                                  cursor.selectionEnd() != oldSelEnd)) {
      emitSelectionRange();
    } else if (cursor.position() != oldPos) {
      emitCursorPosition();
    }
    return;
  }

  QTextCursor oldCursor = textCursor();
  int oldPos = oldCursor.position();
  int oldSelStart = oldCursor.selectionStart();
  int oldSelEnd = oldCursor.selectionEnd();

  QPlainTextEdit::keyPressEvent(event);

  if (currentFileId_ == FileManager::InvalidFileID) {
    return;
  }

  QTextCursor cursor = textCursor();
  if (cursor.hasSelection() &&
      (cursor.selectionStart() != oldSelStart ||
       cursor.selectionEnd() != oldSelEnd)) {
    emitSelectionRange();
    return;
  }

  if (cursor.position() != oldPos) {
    emitCursorPosition();
  }
}

void SourceCodeView::emitCursorPosition() {
  if (currentFileId_ == FileManager::InvalidFileID || mouseSelectionActive_) {
    return;
  }

  QTextCursor cursor = textCursor();
  if (cursor.hasSelection()) {
    return;
  }

  QTextBlock block = cursor.block();
  unsigned line = block.blockNumber() + 1;        // 1-based
  unsigned column = cursor.positionInBlock() + 1; // 1-based

  emit sourcePositionClicked(currentFileId_, line, column);
}

void SourceCodeView::emitSelectionRange() {
  if (currentFileId_ == FileManager::InvalidFileID) {
    return;
  }

  QTextCursor cursor = textCursor();
  if (!cursor.hasSelection()) {
    return;
  }

  int startPos = cursor.selectionStart();
  int endPos = cursor.selectionEnd();
  if (endPos <= startPos) {
    return;
  }

  QTextDocument *doc = document();
  if (!doc) {
    return;
  }

  QTextBlock startBlock = doc->findBlock(startPos);
  QTextBlock endBlock = doc->findBlock(endPos);
  if (!startBlock.isValid() || !endBlock.isValid()) {
    return;
  }

  unsigned startLine = startBlock.blockNumber() + 1;
  unsigned startColumn =
      static_cast<unsigned>(startPos - startBlock.position() + 1);
  unsigned endLine = endBlock.blockNumber() + 1;
  unsigned endColumn =
      static_cast<unsigned>(endPos - endBlock.position() + 1);

  emit sourceRangeSelected(currentFileId_, startLine, startColumn, endLine,
                           endColumn);
}

} // namespace acav
