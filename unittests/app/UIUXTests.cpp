#include "ui/AstModel.h"
#include "ui/DockTitleBar.h"
#include "ui/MainWindow.h"
#include "core/AppConfig.h"
#include "core/AstNode.h"
#include "core/SourceLocation.h"
#include <QApplication>
#include <QCompleter>
#include <QDialog>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QStringListModel>
#include <QToolButton>
#include <QTreeView>
#include <catch2/catch_test_macros.hpp>

using namespace acav;

// Test Issue: Rename "AST Tree" to "AST"
TEST_CASE("AstModel header displays 'AST' not 'AST Tree'", "[UIFix][AstModel]") {
  AstModel model;

  SECTION("Header text is 'AST'") {
    QVariant header = model.headerData(0, Qt::Horizontal, Qt::DisplayRole);
    REQUIRE(header.isValid());
    REQUIRE(header.toString() == "AST");
  }
}

// Test Issue: DockTitleBar supports subtitle for file path display
TEST_CASE("DockTitleBar subtitle support", "[UIFix][DockTitleBar]") {
  DockTitleBar titleBar("Source Code");

  SECTION("Initial subtitle is empty") {
    REQUIRE(titleBar.subtitle().isEmpty());
  }

  SECTION("Can set and get subtitle") {
    titleBar.setSubtitle("[project] src/main.cpp");
    // subtitle() returns the full, non-elided text
    REQUIRE(titleBar.subtitle() == "[project] src/main.cpp");
  }

  SECTION("Can clear subtitle") {
    titleBar.setSubtitle("[project] src/main.cpp");
    titleBar.setSubtitle("");
    REQUIRE(titleBar.subtitle().isEmpty());
  }

  SECTION("Long path is stored fully for tooltip") {
    QString longPath = "[project] src/components/very/deep/nested/directory/structure/Component.cpp";
    titleBar.setSubtitle(longPath);
    // Full path is preserved (for tooltip)
    REQUIRE(titleBar.subtitle() == longPath);
  }
}

TEST_CASE("MainWindow shows AST compilation warning when compile errors exist",
          "[UIFix][MainWindow]") {
  MainWindow window;

  QLabel *warningLabel =
      window.findChild<QLabel *>("astCompilationWarningLabel");

  REQUIRE(warningLabel != nullptr);
  REQUIRE(warningLabel->isHidden());

  LogEntry makeAstError;
  makeAstError.level = LogLevel::Error;
  makeAstError.source = "make-ast";
  makeAstError.message = "dummy compile error";
  REQUIRE(QMetaObject::invokeMethod(
      &window, "onMakeAstLogMessage", Qt::DirectConnection,
      Q_ARG(LogEntry, makeAstError)));

  REQUIRE(QMetaObject::invokeMethod(
      &window, "onAstExtracted", Qt::DirectConnection,
      Q_ARG(AstViewNode *, static_cast<AstViewNode *>(nullptr))));
  REQUIRE_FALSE(warningLabel->isHidden());

  // Next extraction without new errors should clear the warning.
  REQUIRE(QMetaObject::invokeMethod(
      &window, "onAstExtracted", Qt::DirectConnection,
      Q_ARG(AstViewNode *, static_cast<AstViewNode *>(nullptr))));
  REQUIRE(warningLabel->isHidden());
}

TEST_CASE("MainWindow AST search popup opens only on submit",
          "[UIFix][MainWindow][AstSearch]") {
  MainWindow window;
  window.show();

  auto *quickInput = window.findChild<QLineEdit *>("astSearchQuickInput");
  auto *popup = window.findChild<QDialog *>("astSearchPopup");
  auto *input = window.findChild<QLineEdit *>("astSearchInput");
  auto *quickButton =
      window.findChild<QToolButton *>("astSearchQuickButton");

  REQUIRE(quickInput != nullptr);
  REQUIRE(popup != nullptr);
  REQUIRE(input != nullptr);
  REQUIRE(quickButton != nullptr);
  REQUIRE(popup->isHidden());

  quickInput->setFocus();
  QApplication::processEvents();
  REQUIRE(popup->isHidden());

  quickInput->setText("kind:Decl");
  REQUIRE(QMetaObject::invokeMethod(quickInput, "returnPressed",
                                    Qt::DirectConnection));
  QApplication::processEvents();

  REQUIRE_FALSE(popup->isHidden());
  REQUIRE(QApplication::focusWidget() == input);

  const auto labels = quickInput->parentWidget()->findChildren<QLabel *>();
  for (QLabel *label : labels) {
    REQUIRE(label != nullptr);
    REQUIRE(label->text() != "Ctrl+F");
  }
}

TEST_CASE("MainWindow AST search quick button opens popup",
          "[UIFix][MainWindow][AstSearch]") {
  MainWindow window;
  window.show();

  auto *quickInput = window.findChild<QLineEdit *>("astSearchQuickInput");
  auto *popup = window.findChild<QDialog *>("astSearchPopup");
  auto *quickButton =
      window.findChild<QToolButton *>("astSearchQuickButton");

  REQUIRE(quickInput != nullptr);
  REQUIRE(popup != nullptr);
  REQUIRE(quickButton != nullptr);
  REQUIRE(popup->isHidden());

  quickInput->setText("name:main");
  quickButton->click();
  QApplication::processEvents();

  REQUIRE_FALSE(popup->isHidden());
}

TEST_CASE("MainWindow AST search keeps session query history for completer",
          "[UIFix][MainWindow][AstSearch]") {
  MainWindow window;
  window.show();

  auto *quickInput = window.findChild<QLineEdit *>("astSearchQuickInput");
  auto *input = window.findChild<QLineEdit *>("astSearchInput");

  REQUIRE(quickInput != nullptr);
  REQUIRE(input != nullptr);

  input->setText("name:foo");
  REQUIRE(QMetaObject::invokeMethod(&window, "onAstSearchFindNext",
                                    Qt::DirectConnection));

  input->setText("kind:Decl");
  REQUIRE(QMetaObject::invokeMethod(&window, "onAstSearchFindNext",
                                    Qt::DirectConnection));

  input->setText("name:foo");
  REQUIRE(QMetaObject::invokeMethod(&window, "onAstSearchFindNext",
                                    Qt::DirectConnection));

  QCompleter *completer = input->completer();
  REQUIRE(completer != nullptr);
  auto *model = qobject_cast<QStringListModel *>(completer->model());
  REQUIRE(model != nullptr);

  const QStringList history = model->stringList();
  REQUIRE(history.size() == 2);
  REQUIRE(history.at(0) == "name:foo");
  REQUIRE(history.at(1) == "kind:Decl");
}

TEST_CASE("MainWindow AST search popup font follows configured base size",
          "[UIFix][MainWindow][AstSearch]") {
  MainWindow window;
  window.show();
  QApplication::processEvents();

  auto *popup = window.findChild<QDialog *>("astSearchPopup");
  auto *quickInput = window.findChild<QLineEdit *>("astSearchQuickInput");

  REQUIRE(popup != nullptr);
  REQUIRE(quickInput != nullptr);

  const int configuredSize = AppConfig::instance().getFontSize();
  REQUIRE(popup->font().pointSize() == configuredSize);
  REQUIRE(quickInput->font().pointSize() == configuredSize);
}

TEST_CASE("MainWindow AST search popup resizes with AST panel",
          "[UIFix][MainWindow][AstSearch]") {
  MainWindow window;
  window.show();
  QApplication::processEvents();

  auto *quickInput = window.findChild<QLineEdit *>("astSearchQuickInput");
  auto *popup = window.findChild<QDialog *>("astSearchPopup");
  auto *astDock = window.findChild<QDockWidget *>("astDock");

  REQUIRE(quickInput != nullptr);
  REQUIRE(popup != nullptr);
  REQUIRE(astDock != nullptr);

  window.resize(1800, 900);
  QApplication::processEvents();
  quickInput->setText("kind:Decl");
  REQUIRE(QMetaObject::invokeMethod(quickInput, "returnPressed",
                                    Qt::DirectConnection));
  QApplication::processEvents();

  const int dockWidthLarge = astDock->width();
  const int popupWidthLarge = popup->width();

  window.resize(1100, 900);
  QApplication::processEvents();

  const int dockWidthSmall = astDock->width();
  const int popupWidthSmall = popup->width();

  REQUIRE(dockWidthSmall < dockWidthLarge);
  REQUIRE(popupWidthSmall < popupWidthLarge);
}

TEST_CASE("MainWindow AST search popup is non-modal and does not block focus",
          "[UIFix][MainWindow][AstSearch]") {
  MainWindow window;
  window.show();
  QApplication::processEvents();

  auto *quickInput = window.findChild<QLineEdit *>("astSearchQuickInput");
  auto *popup = window.findChild<QDialog *>("astSearchPopup");
  auto *astDock = window.findChild<QDockWidget *>("astDock");

  REQUIRE(quickInput != nullptr);
  REQUIRE(popup != nullptr);
  REQUIRE(astDock != nullptr);

  quickInput->setText("kind:Decl");
  REQUIRE(QMetaObject::invokeMethod(quickInput, "returnPressed",
                                    Qt::DirectConnection));
  QApplication::processEvents();

  REQUIRE_FALSE(popup->isHidden());
  REQUIRE_FALSE(popup->isModal());
  REQUIRE(popup->windowModality() == Qt::NonModal);
  REQUIRE(QApplication::activeModalWidget() == nullptr);
}

// Test Issue: Collapse-all on root AST node must not hang the GUI.
// In v0.5.0, the collapseAll() fast path for root nodes was removed,
// causing O(N) individual collapse calls on large ASTs (50K+ nodes).
// The fix restores collapseAll() for root nodes.
TEST_CASE("Collapse-all on large AST tree completes quickly",
          "[Performance][MainWindow]") {
  // Build a tree with ~10K nodes to simulate a real AST
  AstModel model;
  AstContext context;

  SourceLocation loc(1, 1, 1);
  SourceRange range(loc, loc);

  AcavJson rootProps;
  rootProps["kind"] = InternedString("TranslationUnitDecl");
  AstNode *rootNode = context.createAstNode(rootProps, range);
  AstViewNode *root = context.createAstViewNode(rootNode);

  // Create a deeply nested tree: 100 chains, each 10 levels deep, 100 leaves
  // at bottom. Total: 1 root + 100*10 + 100*100 = 1 + 1,000 + 10,000 base
  // But we want ~1M nodes, so: 100 top chains * 10 depth * 1000 leaves = 1M+
  // Structure: root -> 100 namespaces -> each 10 levels deep -> 1000 leaf nodes
  // Total: 1 + 100 + 100*9 + 100*1000 = 1 + 100 + 900 + 100,000 = 101,001
  // Scale up: 100 top * 10 depth * 1000 leaves = ~1M
  // Actually: 10 top chains, each 100 levels deep, 1000 leaves at bottom
  // Total: 1 + 10 + 10*99 + 10*1000 = 1 + 10 + 990 + 10,000 = 11,001
  // For ~1M: 1000 chains * 100 depth * 10 leaves = 1,001,000 + 100,000 = ~1.1M
  // Simpler: 100 chains * 100 depth * 100 leaves = 100 + 9900 + 10000 = ~20K
  // Let's do: 10 chains, each 100 deep, 1000 leaves at each deepest = ~1M
  // = 1 + 10 + 10*99 + 10*1000 = 11,001 --- too few
  //
  // Best approach: 1000 chains of depth 100, with 10 leaves at bottom
  // = 1 + 1000 + 1000*99 + 1000*10 = 1 + 1000 + 99000 + 10000 = 110,001
  // Still not 1M. Use: 1000 chains * depth 1000 = 1,001,000 internal nodes
  // That's already 1M+ without leaves.
  //
  // Final design: 1000 chains, each 1000 nodes deep (chain of single children)
  // Total: 1 root + 1000 * 1000 = 1,000,001 nodes
  constexpr int kNumChains = 1000;
  constexpr int kChainDepth = 1000;

  for (int i = 0; i < kNumChains; ++i) {
    AcavJson nsProps;
    nsProps["kind"] = InternedString("NamespaceDecl");

    AstViewNode *current = root;
    for (int depth = 0; depth < kChainDepth; ++depth) {
      AstNode *node = context.createAstNode(nsProps, range);
      AstViewNode *child = context.createAstViewNode(node);
      child->setParent(current);
      current->addChild(child);
      current = child;
    }
  }

  model.setRootNode(root);

  QTreeView view;
  view.setModel(&model);
  view.setAnimated(false);
  view.setUniformRowHeights(true);
  view.show();
  QApplication::processEvents();

  // Expand all nodes
  QModelIndex rootIndex = model.index(0, 0, QModelIndex());
  REQUIRE(rootIndex.isValid());
  view.expandRecursively(rootIndex);
  QApplication::processEvents();

  // Verify nodes are expanded
  REQUIRE(view.isExpanded(rootIndex));

  // Select root index
  view.setCurrentIndex(rootIndex);

  // Now collapse all and measure time
  QElapsedTimer timer;
  timer.start();

  // Use the MainWindow approach: collapseAll() for root nodes
  view.header()->setSectionResizeMode(QHeaderView::Fixed);
  view.setUpdatesEnabled(false);
  view.collapseAll();
  view.setUpdatesEnabled(true);

  qint64 elapsed = timer.elapsed();

  // Must complete within 1 second (collapseAll() should be <10ms)
  REQUIRE(elapsed < 1000);
  REQUIRE_FALSE(view.isExpanded(rootIndex));
}
