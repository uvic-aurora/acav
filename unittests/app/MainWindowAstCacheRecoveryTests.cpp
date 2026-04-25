#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <catch2/catch_test_macros.hpp>

using namespace acav;

namespace acav {

class MainWindowTestAccess {
public:
  static void setErrorState(MainWindow &window, const QString &compDbPath,
                            const QString &sourcePath,
                            const QString &astPath) {
    window.compilationDatabasePath_ = compDbPath;
    window.currentSourceFilePath_ = sourcePath;
    window.lastAstFilePath_ = astPath;
  }

  static MakeAstRunner *makeAstRunner(MainWindow &window) {
    return window.makeAstRunner_;
  }

  static void disconnectMakeAstErrorHandler(MainWindow &window) {
    QObject::disconnect(window.makeAstRunner_, &MakeAstRunner::error, &window,
                        &MainWindow::onAstError);
  }

  static void invokeOnAstError(MainWindow &window, const QString &errorMessage) {
    window.onAstError(errorMessage);
  }

  static void invokeOnAstExtracted(MainWindow &window) {
    window.onAstExtracted(nullptr);
  }

  static void persistAstCompilationErrorState(MainWindow &window,
                                              const QString &astPath,
                                              bool hasCompilationErrors) {
    window.persistAstCompilationErrorState(astPath, hasCompilationErrors);
  }

  static bool loadAstCompilationErrorState(MainWindow &window,
                                           const QString &astPath) {
    return window.loadAstCompilationErrorState(astPath);
  }

  static void setAstHasCompilationErrors(MainWindow &window, bool value) {
    window.astHasCompilationErrors_ = value;
  }
};

} // namespace acav

namespace {

void closeAllMessageBoxes() {
  const auto widgets = QApplication::topLevelWidgets();
  for (QWidget *widget : widgets) {
    if (auto *box = qobject_cast<QMessageBox *>(widget)) {
      box->accept();
    }
  }
}

void writeFile(const QString &path, const QByteArray &content = QByteArray()) {
  QFile file(path);
  REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  if (!content.isEmpty()) {
    REQUIRE(file.write(content) == content.size());
  }
}

} // namespace

TEST_CASE("MainWindow auto-regenerates stale AST cache without confirmation",
          "[MainWindow][AstCache]") {
  closeAllMessageBoxes();

  QTemporaryDir tempDir;
  REQUIRE(tempDir.isValid());

  const QString sourcePath = tempDir.path() + "/file.cpp";
  const QString astPath = tempDir.path() + "/file.cpp.ast";
  const QString astStatusPath = astPath + ".status";
  writeFile(sourcePath, "int main() { return 0; }\n");
  writeFile(astPath, "stale");
  writeFile(astStatusPath, "1\n");

  MainWindow window;
  MainWindowTestAccess::disconnectMakeAstErrorHandler(window);
  MainWindowTestAccess::setErrorState(window,
                                      tempDir.path() + "/compile_commands.json",
                                      sourcePath, astPath);

  QSignalSpy progressSpy(MainWindowTestAccess::makeAstRunner(window),
                         &MakeAstRunner::progress);
  REQUIRE(progressSpy.isValid());

  MainWindowTestAccess::invokeOnAstError(
      window, "Failed to load AST from file: " + astPath);

  REQUIRE_FALSE(QFile::exists(astPath));
  REQUIRE_FALSE(QFile::exists(astStatusPath));
  REQUIRE(progressSpy.count() >= 1);
  REQUIRE(progressSpy.at(0).at(0).toString().contains("Generating AST for"));

  bool foundOutdatedPrompt = false;
  const auto widgets = QApplication::topLevelWidgets();
  for (QWidget *widget : widgets) {
    if (auto *box = qobject_cast<QMessageBox *>(widget)) {
      if (box->windowTitle() == "AST Cache Outdated") {
        foundOutdatedPrompt = true;
      }
    }
  }
  REQUIRE_FALSE(foundOutdatedPrompt);

  closeAllMessageBoxes();
}

TEST_CASE("MainWindow keeps existing non-cache error behavior",
          "[MainWindow][AstCache]") {
  closeAllMessageBoxes();

  QTemporaryDir tempDir;
  REQUIRE(tempDir.isValid());

  const QString sourcePath = tempDir.path() + "/file.cpp";
  const QString astPath = tempDir.path() + "/file.cpp.ast";
  writeFile(sourcePath, "int main() { return 0; }\n");
  writeFile(astPath, "cache");

  MainWindow window;
  MainWindowTestAccess::disconnectMakeAstErrorHandler(window);
  MainWindowTestAccess::setErrorState(window,
                                      tempDir.path() + "/compile_commands.json",
                                      sourcePath, astPath);

  QSignalSpy progressSpy(MainWindowTestAccess::makeAstRunner(window),
                         &MakeAstRunner::progress);
  REQUIRE(progressSpy.isValid());

  QTimer::singleShot(0, []() { closeAllMessageBoxes(); });
  MainWindowTestAccess::invokeOnAstError(window,
                                         "make-ast failed with exit code 1");

  REQUIRE(QFile::exists(astPath));
  REQUIRE(progressSpy.count() == 0);

  closeAllMessageBoxes();
}

TEST_CASE("MainWindow restores persisted AST compilation warning state",
          "[MainWindow][AstCache][Warning]") {
  QTemporaryDir tempDir;
  REQUIRE(tempDir.isValid());

  const QString astPath = tempDir.path() + "/file.cpp.ast";

  MainWindow window;
  QLabel *warningLabel =
      window.findChild<QLabel *>("astCompilationWarningLabel");
  REQUIRE(warningLabel != nullptr);
  REQUIRE(warningLabel->isHidden());

  MainWindowTestAccess::persistAstCompilationErrorState(window, astPath, true);
  const bool cachedHasErrors =
      MainWindowTestAccess::loadAstCompilationErrorState(window, astPath);
  MainWindowTestAccess::setAstHasCompilationErrors(window, cachedHasErrors);
  MainWindowTestAccess::invokeOnAstExtracted(window);
  REQUIRE_FALSE(warningLabel->isHidden());

  MainWindowTestAccess::persistAstCompilationErrorState(window, astPath, false);
  const bool cachedNoErrors =
      MainWindowTestAccess::loadAstCompilationErrorState(window, astPath);
  MainWindowTestAccess::setAstHasCompilationErrors(window, cachedNoErrors);
  MainWindowTestAccess::invokeOnAstExtracted(window);
  REQUIRE(warningLabel->isHidden());
}
