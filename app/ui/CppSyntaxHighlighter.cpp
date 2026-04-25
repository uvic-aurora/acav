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

#include "ui/CppSyntaxHighlighter.h"
#include <QColor>
#include <QStringList>

namespace acav {

CppSyntaxHighlighter::CppSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent),
      functionPattern_(
          QStringLiteral("\\b([A-Za-z_][A-Za-z0-9_]*)\\s*(?=\\()")) {
  commentFormat_.setForeground(QColor(110, 118, 129));
  commentFormat_.setFontItalic(true);

  stringFormat_.setForeground(QColor(10, 122, 76));

  keywordFormat_.setForeground(QColor(20, 80, 160));
  keywordFormat_.setFontWeight(QFont::Bold);

  functionFormat_.setForeground(QColor(146, 64, 14));
  functionFormat_.setFontUnderline(true);

  preprocessorFormat_.setForeground(QColor(130, 60, 130));

  static const QStringList keywords = {
      QStringLiteral("alignas"),     QStringLiteral("alignof"),
      QStringLiteral("asm"),         QStringLiteral("auto"),
      QStringLiteral("bool"),        QStringLiteral("break"),
      QStringLiteral("case"),        QStringLiteral("catch"),
      QStringLiteral("char"),        QStringLiteral("class"),
      QStringLiteral("const"),       QStringLiteral("constexpr"),
      QStringLiteral("consteval"),   QStringLiteral("constinit"),
      QStringLiteral("continue"),    QStringLiteral("decltype"),
      QStringLiteral("default"),     QStringLiteral("delete"),
      QStringLiteral("do"),          QStringLiteral("double"),
      QStringLiteral("else"),        QStringLiteral("enum"),
      QStringLiteral("explicit"),    QStringLiteral("extern"),
      QStringLiteral("false"),       QStringLiteral("float"),
      QStringLiteral("for"),         QStringLiteral("friend"),
      QStringLiteral("goto"),        QStringLiteral("if"),
      QStringLiteral("inline"),      QStringLiteral("int"),
      QStringLiteral("long"),        QStringLiteral("mutable"),
      QStringLiteral("namespace"),   QStringLiteral("new"),
      QStringLiteral("noexcept"),    QStringLiteral("nullptr"),
      QStringLiteral("operator"),    QStringLiteral("private"),
      QStringLiteral("protected"),   QStringLiteral("public"),
      QStringLiteral("register"),    QStringLiteral("reinterpret_cast"),
      QStringLiteral("requires"),    QStringLiteral("return"),
      QStringLiteral("short"),       QStringLiteral("signed"),
      QStringLiteral("sizeof"),      QStringLiteral("static"),
      QStringLiteral("static_assert"), QStringLiteral("struct"),
      QStringLiteral("switch"),      QStringLiteral("template"),
      QStringLiteral("this"),        QStringLiteral("thread_local"),
      QStringLiteral("throw"),       QStringLiteral("true"),
      QStringLiteral("try"),         QStringLiteral("typedef"),
      QStringLiteral("typename"),    QStringLiteral("union"),
      QStringLiteral("unsigned"),    QStringLiteral("using"),
      QStringLiteral("virtual"),     QStringLiteral("void"),
      QStringLiteral("volatile"),    QStringLiteral("while")};

  keywordPatterns_.reserve(static_cast<std::size_t>(keywords.size()));
  for (const QString &keyword : keywords) {
    keywordPatterns_.push_back(
        QRegularExpression(QStringLiteral("\\b%1\\b").arg(keyword)));
  }

  nonFunctionWords_ = QSet<QString>(
      keywords.begin(), keywords.end()); // Prevent keywords from function style
}

void CppSyntaxHighlighter::highlightBlock(const QString &text) {
  std::vector<bool> masked(static_cast<std::size_t>(text.size()), false);
  int index = 0;
  int state = previousBlockState();
  setCurrentBlockState(NormalState);

  if (state == InBlockCommentState) {
    int end = text.indexOf(QStringLiteral("*/"));
    if (end < 0) {
      markCommentRange(text, 0, text.size(), &masked);
      setCurrentBlockState(InBlockCommentState);
      return;
    }
    markCommentRange(text, 0, end + 2, &masked);
    index = end + 2;
  }

  while (index < text.size()) {
    const QChar ch = text.at(index);

    if (ch == '"' || ch == '\'') {
      int nextIndex = index + 1;
      markStringOrCharLiteral(text, index, &masked, &nextIndex);
      index = nextIndex;
      continue;
    }

    if (ch == '/' && index + 1 < text.size()) {
      const QChar next = text.at(index + 1);
      if (next == '/') {
        markCommentRange(text, index, text.size() - index, &masked);
        break;
      }
      if (next == '*') {
        int end = text.indexOf(QStringLiteral("*/"), index + 2);
        if (end < 0) {
          markCommentRange(text, index, text.size() - index, &masked);
          setCurrentBlockState(InBlockCommentState);
          break;
        }
        markCommentRange(text, index, end - index + 2, &masked);
        index = end + 2;
        continue;
      }
    }

    ++index;
  }

  applyKeywordRules(text, masked);
  applyFunctionRules(text, masked);
  applyPreprocessorRules(text, &masked);
}

void CppSyntaxHighlighter::markCommentRange(const QString &text, int start,
                                            int length,
                                            std::vector<bool> *masked) {
  if (!masked || start < 0 || length <= 0 || start >= text.size()) {
    return;
  }

  int safeLength = qMin(length, text.size() - start);
  setFormat(start, safeLength, commentFormat_);
  for (int i = start; i < start + safeLength; ++i) {
    (*masked)[static_cast<std::size_t>(i)] = true;
  }
}

void CppSyntaxHighlighter::markStringOrCharLiteral(const QString &text,
                                                   int start,
                                                   std::vector<bool> *masked,
                                                   int *nextIndex) {
  if (!masked || !nextIndex || start < 0 || start >= text.size()) {
    return;
  }

  const QChar quote = text.at(start);
  int end = start + 1;
  bool escaped = false;

  while (end < text.size()) {
    const QChar ch = text.at(end);
    if (escaped) {
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else if (ch == quote) {
      ++end;
      break;
    }
    ++end;
  }

  int length = end - start;
  if (length <= 0) {
    *nextIndex = start + 1;
    return;
  }

  setFormat(start, length, stringFormat_);
  for (int i = start; i < start + length && i < text.size(); ++i) {
    (*masked)[static_cast<std::size_t>(i)] = true;
  }
  *nextIndex = end;
}

bool CppSyntaxHighlighter::isUnmaskedRange(const std::vector<bool> &masked,
                                           int start, int length) const {
  if (start < 0 || length <= 0) {
    return false;
  }
  for (int i = start; i < start + length; ++i) {
    if (i >= static_cast<int>(masked.size()) ||
        masked[static_cast<std::size_t>(i)]) {
      return false;
    }
  }
  return true;
}

void CppSyntaxHighlighter::applyKeywordRules(const QString &text,
                                             const std::vector<bool> &masked) {
  for (const QRegularExpression &pattern : keywordPatterns_) {
    auto it = pattern.globalMatch(text);
    while (it.hasNext()) {
      const auto match = it.next();
      const int start = match.capturedStart();
      const int length = match.capturedLength();
      if (isUnmaskedRange(masked, start, length)) {
        setFormat(start, length, keywordFormat_);
      }
    }
  }
}

void CppSyntaxHighlighter::applyFunctionRules(const QString &text,
                                              const std::vector<bool> &masked) {
  auto it = functionPattern_.globalMatch(text);
  while (it.hasNext()) {
    const auto match = it.next();
    const QString identifier = match.captured(1);
    if (identifier.isEmpty() || nonFunctionWords_.contains(identifier)) {
      continue;
    }
    const int start = match.capturedStart(1);
    const int length = match.capturedLength(1);
    if (isUnmaskedRange(masked, start, length)) {
      setFormat(start, length, functionFormat_);
    }
  }
}

void CppSyntaxHighlighter::applyPreprocessorRules(const QString &text,
                                                  std::vector<bool> *masked) {
  if (!masked) {
    return;
  }

  // Find first non-whitespace character
  int start = 0;
  while (start < text.size() && text.at(start).isSpace()) {
    ++start;
  }

  // Must start with '#'
  if (start >= text.size() || text.at(start) != '#') {
    return;
  }

  // Find the directive word after '#'
  int directiveStart = start + 1;
  while (directiveStart < text.size() && text.at(directiveStart).isSpace()) {
    ++directiveStart;
  }
  int directiveEnd = directiveStart;
  while (directiveEnd < text.size() && text.at(directiveEnd).isLetterOrNumber()) {
    ++directiveEnd;
  }

  // Highlight '#directive' with preprocessor format (only unmasked parts)
  if (directiveEnd > start) {
    for (int i = start; i < directiveEnd; ++i) {
      if (!(*masked)[static_cast<std::size_t>(i)]) {
        setFormat(i, 1, preprocessorFormat_);
      }
    }
  }

  // For #include, also highlight <...> as a string
  const QStringView directive =
      QStringView(text).mid(directiveStart, directiveEnd - directiveStart);
  if (directive == QStringLiteral("include")) {
    int pos = directiveEnd;
    while (pos < text.size() && text.at(pos).isSpace()) {
      ++pos;
    }
    if (pos < text.size() && text.at(pos) == '<') {
      int angleEnd = text.indexOf('>', pos + 1);
      if (angleEnd >= 0) {
        int length = angleEnd - pos + 1;
        setFormat(pos, length, stringFormat_);
        for (int i = pos; i < pos + length; ++i) {
          (*masked)[static_cast<std::size_t>(i)] = true;
        }
      }
    }
  }
}

} // namespace acav
