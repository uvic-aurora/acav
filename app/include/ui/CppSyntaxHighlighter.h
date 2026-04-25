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

/// \file CppSyntaxHighlighter.h
/// \brief Lightweight C/C++ syntax highlighter for SourceCodeView.
#pragma once

#include <QRegularExpression>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <vector>

namespace acav {

/// \brief Syntax highlighter with four categories: comments, strings/chars,
/// keywords/types, and functions.
class CppSyntaxHighlighter : public QSyntaxHighlighter {
  Q_OBJECT

public:
  explicit CppSyntaxHighlighter(QTextDocument *parent);

protected:
  void highlightBlock(const QString &text) override;

private:
  enum BlockState {
    NormalState = 0,
    InBlockCommentState = 1,
  };

  QTextCharFormat commentFormat_;
  QTextCharFormat stringFormat_;
  QTextCharFormat keywordFormat_;
  QTextCharFormat functionFormat_;
  QTextCharFormat preprocessorFormat_;

  std::vector<QRegularExpression> keywordPatterns_;
  QRegularExpression functionPattern_;
  QSet<QString> nonFunctionWords_;

  void markCommentRange(const QString &text, int start, int length,
                        std::vector<bool> *masked);
  void markStringOrCharLiteral(const QString &text, int start,
                               std::vector<bool> *masked, int *nextIndex);
  bool isUnmaskedRange(const std::vector<bool> &masked, int start,
                       int length) const;
  void applyKeywordRules(const QString &text, const std::vector<bool> &masked);
  void applyFunctionRules(const QString &text, const std::vector<bool> &masked);
  void applyPreprocessorRules(const QString &text, std::vector<bool> *masked);
};

} // namespace acav
