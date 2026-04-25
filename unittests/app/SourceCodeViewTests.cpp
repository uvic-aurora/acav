#include "ui/SourceCodeView.h"
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextStream>
#include <catch2/catch_test_macros.hpp>

using namespace acav;

namespace {

QTextCharFormat formatAtPosition(const QTextBlock &block, int positionInBlock) {
  QTextLayout *layout = block.layout();
  if (!layout || positionInBlock < 0) {
    return QTextCharFormat();
  }

  const auto formats = layout->formats();
  for (const auto &formatRange : formats) {
    if (positionInBlock >= formatRange.start &&
        positionInBlock < formatRange.start + formatRange.length) {
      return formatRange.format;
    }
  }
  return QTextCharFormat();
}

QTextCharFormat formatForToken(const QTextBlock &block, const QString &token) {
  const int pos = block.text().indexOf(token);
  if (pos < 0) {
    return QTextCharFormat();
  }
  return formatAtPosition(block, pos);
}

} // namespace

TEST_CASE("SourceCodeView line number width calculation", "[SourceCodeView]") {
  SourceCodeView view;

  SECTION("Empty file (1 line minimum)") {
    view.setPlainText("");
    int width = view.lineNumberAreaWidth();
    REQUIRE(width > 0);
    // Should have space for at least 1 digit
  }

  SECTION("1-9 lines (1 digit)") {
    view.setPlainText("Line 1\nLine 2\nLine 3\nLine 4\nLine 5");
    int width5 = view.lineNumberAreaWidth();
    REQUIRE(width5 > 0);

    view.setPlainText("Line 1\nLine 2\nLine 3\nLine 4\nLine 5\nLine 6\nLine "
                      "7\nLine 8\nLine 9");
    int width9 = view.lineNumberAreaWidth();
    REQUIRE(width9 > 0);
    // Width should be the same for 1-9 lines (both need 1 digit)
    REQUIRE(width5 == width9);
  }

  SECTION("10-99 lines (2 digits)") {
    QString text;
    for (int i = 0; i < 50; ++i) {
      text += QString("Line %1\n").arg(i + 1);
    }
    view.setPlainText(text);
    int width50 = view.lineNumberAreaWidth();
    REQUIRE(width50 > 0);

    // Width for 50 lines should be greater than for 9 lines (needs 2 digits)
    view.setPlainText("Line 1\nLine 2\nLine 3\nLine 4\nLine 5\nLine 6\nLine "
                      "7\nLine 8\nLine 9");
    int width9 = view.lineNumberAreaWidth();
    REQUIRE(width50 > width9);
  }

  SECTION("100-999 lines (3 digits)") {
    QString text;
    for (int i = 0; i < 100; ++i) {
      text += QString("Line %1\n").arg(i + 1);
    }
    view.setPlainText(text);
    int width100 = view.lineNumberAreaWidth();
    REQUIRE(width100 > 0);

    // Width for 100 lines should be greater than for 99 lines (needs 3 digits)
    text.clear();
    for (int i = 0; i < 99; ++i) {
      text += QString("Line %1\n").arg(i + 1);
    }
    view.setPlainText(text);
    int width99 = view.lineNumberAreaWidth();
    REQUIRE(width100 >= width99);
  }

  SECTION("1000+ lines (4 digits)") {
    QString text;
    for (int i = 0; i < 1000; ++i) {
      text += QString("Line %1\n").arg(i + 1);
    }
    view.setPlainText(text);
    int width1000 = view.lineNumberAreaWidth();
    REQUIRE(width1000 > 0);

    // Width for 1000 lines should be greater than for 999 lines (needs 4
    // digits)
    text.clear();
    for (int i = 0; i < 999; ++i) {
      text += QString("Line %1\n").arg(i + 1);
    }
    view.setPlainText(text);
    int width999 = view.lineNumberAreaWidth();
    REQUIRE(width1000 >= width999);
  }
}

TEST_CASE("SourceCodeView line number area creation", "[SourceCodeView]") {
  SourceCodeView view;

  SECTION("Line number area width is positive") {
    // LineNumberArea should be created in constructor
    // Verify it has positive width
    int width = view.lineNumberAreaWidth();
    REQUIRE(width > 0);
  }

  SECTION("Line number area width matches expected for line count") {
    view.setPlainText("Line 1\nLine 2\nLine 3");
    int width = view.lineNumberAreaWidth();
    REQUIRE(width > 0);

    // Adding more lines should maintain or increase width
    view.setPlainText("Line 1\nLine 2\nLine 3\nLine 4\nLine 5");
    int width5 = view.lineNumberAreaWidth();
    REQUIRE(width5 >= width);
  }
}

TEST_CASE("SourceCodeView file loading with line numbers", "[SourceCodeView]") {
  SourceCodeView view;

  SECTION("Load file and verify line numbers appear") {
    // Create a temporary file
    QTemporaryFile tempFile;
    REQUIRE(tempFile.open());

    QTextStream out(&tempFile);
    out << "int main() {\n";
    out << "    return 0;\n";
    out << "}\n";
    tempFile.close();

    // Load the file
    bool loaded = view.loadFile(tempFile.fileName());
    REQUIRE(loaded);

    // Verify line number area width is calculated
    int width = view.lineNumberAreaWidth();
    REQUIRE(width > 0);

    // Verify content is loaded
    QString content = view.toPlainText();
    REQUIRE(content.contains("int main()"));
  }
}

TEST_CASE("SourceCodeView trailing newline handling", "[SourceCodeView]") {
  SourceCodeView view;

  SECTION("File without trailing newline - no extra line added") {
    QTemporaryFile tempFile;
    REQUIRE(tempFile.open());

    // Write content WITHOUT trailing newline
    QTextStream out(&tempFile);
    out << "line1\nline2\nline3";  // No \n at end
    out.flush();
    tempFile.close();

    bool loaded = view.loadFile(tempFile.fileName());
    REQUIRE(loaded);

    QString content = view.toPlainText();
    // Content should match exactly - no extra newline added
    REQUIRE(content == "line1\nline2\nline3");

    // Block count should be 3 (one per line)
    REQUIRE(view.blockCount() == 3);
  }

  SECTION("File with trailing newline - trimmed for display") {
    QTemporaryFile tempFile;
    REQUIRE(tempFile.open());

    // Write content WITH trailing newline
    QTextStream out(&tempFile);
    out << "line1\nline2\nline3\n";  // Has \n at end
    out.flush();
    tempFile.close();

    bool loaded = view.loadFile(tempFile.fileName());
    REQUIRE(loaded);

    QString content = view.toPlainText();
    // Trailing newline is trimmed to avoid an extra empty visual line.
    REQUIRE(content == "line1\nline2\nline3");

    // Block count should match the file's logical line count (3).
    REQUIRE(view.blockCount() == 3);
  }

  SECTION("File with trailing CRLF newline - trimmed for display") {
    QTemporaryFile tempFile;
    REQUIRE(tempFile.open());

    const QByteArray bytes = "line1\r\nline2\r\nline3\r\n";
    REQUIRE(tempFile.write(bytes) == bytes.size());
    REQUIRE(tempFile.flush());
    tempFile.close();

    bool loaded = view.loadFile(tempFile.fileName());
    REQUIRE(loaded);

    QString content = view.toPlainText();
    // CRLF is normalized by Qt text mode; the final line break is trimmed.
    REQUIRE(content == "line1\nline2\nline3");
    REQUIRE(view.blockCount() == 3);
  }

  SECTION("Empty file - single empty block") {
    QTemporaryFile tempFile;
    REQUIRE(tempFile.open());
    tempFile.close();

    bool loaded = view.loadFile(tempFile.fileName());
    REQUIRE(loaded);

    QString content = view.toPlainText();
    REQUIRE(content.isEmpty());

    // QPlainTextEdit always has at least 1 block
    REQUIRE(view.blockCount() == 1);
  }
}

TEST_CASE("SourceCodeView provides four syntax highlight categories",
          "[SourceCodeView][SyntaxHighlight]") {
  SourceCodeView view;
  view.setPlainText("int main() {\n"
                    "  // return in comment\n"
                    "  const char *s = \"if return\";\n"
                    "  printf(\"value\");\n"
                    "  if (main()) { return 0; }\n"
                    "  /* multi-line\n"
                    "     return comment */\n"
                    "  custom_call();\n"
                    "}\n");
  QApplication::processEvents();

  QTextBlock line0 = view.document()->findBlockByLineNumber(0);
  QTextBlock line1 = view.document()->findBlockByLineNumber(1);
  QTextBlock line2 = view.document()->findBlockByLineNumber(2);
  QTextBlock line3 = view.document()->findBlockByLineNumber(3);
  QTextBlock line4 = view.document()->findBlockByLineNumber(4);
  QTextBlock line6 = view.document()->findBlockByLineNumber(6);
  QTextBlock line7 = view.document()->findBlockByLineNumber(7);

  REQUIRE(line0.isValid());
  REQUIRE(line1.isValid());
  REQUIRE(line2.isValid());
  REQUIRE(line3.isValid());
  REQUIRE(line4.isValid());
  REQUIRE(line6.isValid());
  REQUIRE(line7.isValid());

  // Keywords/types
  QTextCharFormat intFormat = formatForToken(line0, "int");
  REQUIRE(intFormat.fontWeight() == QFont::Bold);
  REQUIRE_FALSE(intFormat.fontUnderline());

  QTextCharFormat ifKeywordFormat = formatForToken(line4, "if");
  REQUIRE(ifKeywordFormat.fontWeight() == QFont::Bold);
  REQUIRE_FALSE(ifKeywordFormat.fontUnderline());

  QTextCharFormat returnKeywordFormat = formatForToken(line4, "return");
  REQUIRE(returnKeywordFormat.fontWeight() == QFont::Bold);
  REQUIRE_FALSE(returnKeywordFormat.fontUnderline());

  // Comments: keyword-like token in comments must not be keyword-highlighted.
  QTextCharFormat returnInSingleLineComment = formatForToken(line1, "return");
  REQUIRE(returnInSingleLineComment.fontItalic());
  REQUIRE(returnInSingleLineComment.fontWeight() != QFont::Bold);

  QTextCharFormat returnInBlockComment = formatForToken(line6, "return");
  REQUIRE(returnInBlockComment.fontItalic());
  REQUIRE(returnInBlockComment.fontWeight() != QFont::Bold);

  // String literal
  QTextCharFormat ifInString = formatForToken(line2, "if");
  REQUIRE(ifInString.foreground().color() == QColor(10, 122, 76));
  REQUIRE(ifInString.fontWeight() != QFont::Bold);
  REQUIRE_FALSE(ifInString.fontUnderline());

  // Functions (including C standard library functions)
  QTextCharFormat printfFormat = formatForToken(line3, "printf");
  REQUIRE(printfFormat.fontUnderline());
  REQUIRE(printfFormat.fontWeight() != QFont::Bold);

  QTextCharFormat customCallFormat = formatForToken(line7, "custom_call");
  REQUIRE(customCallFormat.fontUnderline());
  REQUIRE(customCallFormat.fontWeight() != QFont::Bold);
}
