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

#include "ui/MainWindow.h"
#include "Version.h"
#include "common/ClangUtils.h"
#include "common/InternedString.h"
#include "core/AppConfig.h"
#include "core/MemoryProfiler.h"
#include "core/SourceLocation.h"
#include "ui/NodeDetailsDialog.h"
#include "ui/OpenProjectDialog.h"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QColor>
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeySequence>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMetaType>
#include <QMoveEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QStringListModel>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QResizeEvent>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <chrono>
#include <cstddef>
#include <exception>
#include <unordered_set>
#include <vector>

namespace acav {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), fileMenu_(nullptr), viewMenu_(nullptr),
      openAction_(nullptr), exitAction_(nullptr), settingsAction_(nullptr),
      reloadConfigAction_(nullptr), navBackAction_(nullptr),
      navForwardAction_(nullptr), goToMacroDefinitionAction_(nullptr),
      zoomInAction_(nullptr), zoomOutAction_(nullptr),
      zoomResetAction_(nullptr), navToolBar_(nullptr),
      resetLayoutAction_(nullptr), tuDock_(nullptr), sourceDock_(nullptr),
      astDock_(nullptr), declContextDock_(nullptr), logDock_(nullptr),
      tuView_(nullptr), sourceView_(nullptr), astView_(nullptr),
      declContextView_(nullptr), tuModel_(nullptr), astModel_(nullptr),
      queryRunner_(nullptr), parallelQueryRunner_(nullptr),
      makeAstRunner_(nullptr), astExtractorRunner_(nullptr),
      astWorkerThread_(nullptr), sourceSearchInput_(nullptr),
      sourceSearchPrevButton_(nullptr), sourceSearchNextButton_(nullptr),
      sourceSearchStatus_(nullptr), sourceSearchDebounce_(nullptr),
      nodeCycleWidget_(nullptr), isAstExtractionInProgress_(false) {

  qRegisterMetaType<LogEntry>();

  // Set window properties early so initial dock sizing is based on the final
  // window geometry (otherwise QMainWindow/QSplitter will scale dock sizes).
  setWindowTitle("ACAV");
  setWindowIcon(QIcon(":/resources/aurora_icon.png"));
  // Default window size
  resize(1600, 900);

  setupUI();
  setupMenuBar();
  setupDockWidgets();
  setupModels();
  connectSignals();

  // Apply configured font size to all panels for consistent initial appearance
  applyFontSize(AppConfig::instance().getFontSize());

  logStatus(LogLevel::Info, tr("Ready"));
}

MainWindow::~MainWindow() {
  QObject::disconnect(qApp, &QApplication::focusChanged, this,
                      &MainWindow::onFocusChanged);
  if (makeAstRunner_) {
    QObject::disconnect(makeAstRunner_, &MakeAstRunner::logMessage, this,
                        &MainWindow::onMakeAstLogMessage);
  }
  MemoryProfiler::setLogCallback(nullptr);
  // Detach the AST model before destroying the AstContext to avoid
  // views touching freed nodes during QWidget teardown.
  if (astView_) {
    astView_->setModel(nullptr);
  }
  if (astModel_) {
    astModel_->clear();
  }

  // Clean shutdown of worker thread
  if (astWorkerThread_) {
    astWorkerThread_->quit();
    astWorkerThread_->wait();
  }
  if (astExportThread_) {
    astExportThread_->quit();
    astExportThread_->wait();
  }
  // Qt parent-child ownership handles rest of cleanup
}

void MainWindow::applyUnifiedSelectionPalette() {
  QPalette pal = QApplication::palette();
  const QBrush activeHighlight =
      pal.brush(QPalette::Active, QPalette::Highlight);
  const QBrush activeText =
      pal.brush(QPalette::Active, QPalette::HighlightedText);
  pal.setBrush(QPalette::Inactive, QPalette::Highlight, activeHighlight);
  pal.setBrush(QPalette::Inactive, QPalette::HighlightedText, activeText);
  QApplication::setPalette(pal);
}

void MainWindow::setupViewMenuDockActions() {
  if (!viewMenu_) {
    return;
  }

  viewMenu_->addSeparator();

  const std::initializer_list<std::pair<QDockWidget *, QString>> docks = {
      {tuDock_, tr("File Explorer")},
      {sourceDock_, tr("Source Code")},
      {astDock_, tr("AST")},
      {declContextDock_, tr("Declaration Context")},
      {logDock_, tr("Logs")}};
  for (const auto &[dock, text] : docks) {
    if (dock) {
      QAction *action = dock->toggleViewAction();
      action->setText(text);
      viewMenu_->addAction(action);
    }
  }

  viewMenu_->addSeparator();

  resetLayoutAction_ = new QAction(tr("Reset Layout"), this);
  resetLayoutAction_->setStatusTip(tr("Restore the default dock layout"));
  connect(resetLayoutAction_, &QAction::triggered, this,
          &MainWindow::onResetLayout);
  viewMenu_->addAction(resetLayoutAction_);
}

void MainWindow::setupUI() {
  applyUnifiedSelectionPalette();

  setDockNestingEnabled(true);
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                 QMainWindow::AllowTabbedDocks);

  // No central widget - use dock-only layout
  setCentralWidget(nullptr);
}

void MainWindow::setupMenuBar() {
  // Create File menu
  fileMenu_ = menuBar()->addMenu(tr("&File"));

  // Open Project action
  openAction_ = new QAction(tr("Open &Project..."), this);
  openAction_->setShortcut(QKeySequence::Open);
  openAction_->setStatusTip(tr("Open a compilation database file"));
  fileMenu_->addAction(openAction_);
  settingsAction_ = new QAction(tr("Open Config File..."), this);
  settingsAction_->setShortcut(QKeySequence::Preferences);
  settingsAction_->setStatusTip(
      tr("Open configuration file in external editor"));
  fileMenu_->addAction(settingsAction_);
  connect(settingsAction_, &QAction::triggered, this,
          &MainWindow::onOpenConfigFile);
  reloadConfigAction_ = new QAction(tr("Reload Configuration"), this);
  reloadConfigAction_->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
  reloadConfigAction_->setStatusTip(
      tr("Reload configuration file and reapply settings"));
  fileMenu_->addAction(reloadConfigAction_);
  connect(reloadConfigAction_, &QAction::triggered, this,
          &MainWindow::onReloadConfig);

  fileMenu_->addSeparator();

  // Exit action
  exitAction_ = new QAction(tr("E&xit"), this);
  exitAction_->setShortcut(QKeySequence::Quit);
  exitAction_->setStatusTip(tr("Exit the application"));
  fileMenu_->addAction(exitAction_);

  // View menu
  viewMenu_ = menuBar()->addMenu(tr("&View"));

  // Navigation actions (Cmd+[ / Cmd+] on macOS, Ctrl+[ / Ctrl+] elsewhere)
  navBackAction_ = new QAction(tr("Back"), this);
  navBackAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));
  navBackAction_->setShortcutContext(Qt::ApplicationShortcut);
  navBackAction_->setEnabled(false);
  navForwardAction_ = new QAction(tr("Forward"), this);
  navForwardAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
  navForwardAction_->setShortcutContext(Qt::ApplicationShortcut);
  navForwardAction_->setEnabled(false);
  addAction(navBackAction_);
  addAction(navForwardAction_);
  connect(navBackAction_, &QAction::triggered, this,
          [this]() { navigateHistory(-1); });
  connect(navForwardAction_, &QAction::triggered, this,
          [this]() { navigateHistory(1); });

  goToMacroDefinitionAction_ = new QAction(tr("Go to Macro Definition"), this);
  goToMacroDefinitionAction_->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
  goToMacroDefinitionAction_->setShortcutContext(Qt::ApplicationShortcut);
  goToMacroDefinitionAction_->setEnabled(false);
  addAction(goToMacroDefinitionAction_);
  viewMenu_->addAction(goToMacroDefinitionAction_);
  connect(goToMacroDefinitionAction_, &QAction::triggered, this,
          [this]() { onGoToMacroDefinition(); });

  zoomInAction_ = new QAction(tr("Zoom In"), this);
  zoomInAction_->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Plus),
                               QKeySequence(Qt::CTRL | Qt::Key_Equal)});
  zoomInAction_->setShortcutContext(Qt::ApplicationShortcut);
  addAction(zoomInAction_);
  viewMenu_->addAction(zoomInAction_);
  connect(zoomInAction_, &QAction::triggered, this,
          [this]() { adjustFontSize(1); });

  zoomOutAction_ = new QAction(tr("Zoom Out"), this);
  zoomOutAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
  zoomOutAction_->setShortcutContext(Qt::ApplicationShortcut);
  addAction(zoomOutAction_);
  viewMenu_->addAction(zoomOutAction_);
  connect(zoomOutAction_, &QAction::triggered, this,
          [this]() { adjustFontSize(-1); });

  zoomResetAction_ = new QAction(tr("Reset Zoom"), this);
  zoomResetAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
  zoomResetAction_->setShortcutContext(Qt::ApplicationShortcut);
  addAction(zoomResetAction_);
  viewMenu_->addAction(zoomResetAction_);
  connect(zoomResetAction_, &QAction::triggered, this, [this]() {
    int defaultSize = AppConfig::instance().getFontSize();
    currentFontFamily_ = AppConfig::instance().getFontFamily();
    QFont baseFont = QApplication::font();
    if (!currentFontFamily_.isEmpty()) {
      baseFont.setFamily(currentFontFamily_);
    }
    baseFont.setPointSize(defaultSize);

    // Reset only the focused panel's font size
    if (focusedDock_ == tuDock_ && tuView_) {
      tuFontSize_ = defaultSize;
      tuView_->setFont(baseFont);
    } else if (focusedDock_ == sourceDock_ && sourceView_) {
      sourceFontSize_ = defaultSize;
      sourceView_->setFont(baseFont);
      sourceView_->applyFontSize(defaultSize);
    } else if (focusedDock_ == astDock_ && astView_) {
      astFontSize_ = defaultSize;
      astView_->setFont(baseFont);
      if (astSearchQuickInput_) {
        astSearchQuickInput_->setFont(baseFont);
      }
      if (astSearchPopup_) {
        astSearchPopup_->setFont(baseFont);
      }
      if (astSearchCompleter_ && astSearchCompleter_->popup()) {
        astSearchCompleter_->popup()->setFont(baseFont);
      }
    } else if (focusedDock_ == declContextDock_ && declContextView_) {
      declContextFontSize_ = defaultSize;
      declContextView_->applyFont(baseFont);
    } else if (focusedDock_ == logDock_) {
      logFontSize_ = defaultSize;
      logDock_->setFont(baseFont);
    } else {
      // Fallback: reset all if no dock focused
      tuFontSize_ = defaultSize;
      sourceFontSize_ = defaultSize;
      astFontSize_ = defaultSize;
      declContextFontSize_ = defaultSize;
      logFontSize_ = defaultSize;
      applyFontSize(defaultSize);
    }
  });

  // Optional toolbar for navigation
  navToolBar_ = addToolBar(tr("Navigation"));
  navToolBar_->addAction(navBackAction_);
  navToolBar_->addAction(navForwardAction_);
  navToolBar_->setMovable(false);

  // Help menu
  ::QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));

  QAction *shortcutsAction = new QAction(tr("&Keyboard Shortcuts"), this);
  shortcutsAction->setStatusTip(tr("Show keyboard shortcuts"));
  connect(shortcutsAction, &QAction::triggered, this, [this]() {
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Keyboard Shortcuts"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    QColor headerBg = palette().color(QPalette::Highlight);
    QColor headerFg = palette().color(QPalette::HighlightedText);

    auto makeTable = [&](const QString &title) {
      auto *group = new QGroupBox(title, dialog);
      auto *table = new QTableWidget(group);
      table->setColumnCount(2);
      table->setHorizontalHeaderLabels({tr("Shortcut"), tr("Action")});
      table->horizontalHeader()->setStretchLastSection(true);
      table->verticalHeader()->setVisible(false);
      table->setEditTriggers(QAbstractItemView::NoEditTriggers);
      table->setSelectionMode(QAbstractItemView::NoSelection);
      table->setFocusPolicy(Qt::NoFocus);
      table->setShowGrid(false);
      table->setAlternatingRowColors(true);
      auto *layout = new QVBoxLayout(group);
      layout->setContentsMargins(0, 0, 0, 0);
      layout->addWidget(table);
      return std::make_pair(group, table);
    };

    auto addCategory = [&](QTableWidget *table, const QString &name) {
      int row = table->rowCount();
      table->insertRow(row);
      auto *item = new QTableWidgetItem(name);
      QFont f = item->font();
      f.setBold(true);
      item->setFont(f);
      item->setBackground(headerBg);
      item->setForeground(headerFg);
      table->setItem(row, 0, item);
      auto *spacer = new QTableWidgetItem();
      spacer->setBackground(headerBg);
      table->setItem(row, 1, spacer);
      table->setSpan(row, 0, 1, 2);
    };

    auto addShortcut = [](QTableWidget *table, const QString &key,
                          const QString &action) {
      int row = table->rowCount();
      table->insertRow(row);
      auto *keyItem = new QTableWidgetItem(key);
      QFont f = keyItem->font();
      f.setBold(true);
      keyItem->setFont(f);
      table->setItem(row, 0, keyItem);
      table->setItem(row, 1, new QTableWidgetItem(action));
    };

    auto finishTable = [](QTableWidget *table) {
      table->resizeColumnsToContents();
      table->resizeRowsToContents();
      table->setMinimumWidth(table->columnWidth(0) + table->columnWidth(1) +
                             30);
    };

    // Left column
    auto [leftGroup, leftTable] = makeTable(tr(""));
    addCategory(leftTable, tr("Focus Switching"));
    addShortcut(leftTable, "Tab", tr("Cycle focus between panes"));
    addShortcut(leftTable, "Ctrl+1", tr("File Explorer"));
    addShortcut(leftTable, "Ctrl+2", tr("Source Code"));
    addShortcut(leftTable, "Ctrl+3", tr("AST"));
    addShortcut(leftTable, "Ctrl+4", tr("Decl Context (Semantic)"));
    addShortcut(leftTable, "Ctrl+5", tr("Decl Context (Lexical)"));
    addShortcut(leftTable, "Ctrl+6", tr("Logs"));
    addCategory(leftTable, tr("Tree Views"));
    addShortcut(leftTable, "Ctrl+Shift+E", tr("Expand all children"));
    addShortcut(leftTable, "Ctrl+Shift+C", tr("Collapse all children"));
    addShortcut(leftTable, "F5", tr("Extract AST for selected file"));
    addCategory(leftTable, tr("Search"));
    addShortcut(leftTable, "Ctrl+F", tr("Focus search in active panel"));
    addCategory(leftTable, tr("Source Code"));
    addShortcut(leftTable, "Home", tr("Go to start of file"));
    addShortcut(leftTable, "End", tr("Go to end of file"));
    finishTable(leftTable);

    // Right column
    auto [rightGroup, rightTable] = makeTable(tr(""));
    addCategory(rightTable, tr("View"));
    addShortcut(rightTable, "Ctrl++", tr("Zoom in"));
    addShortcut(rightTable, "Ctrl+-", tr("Zoom out"));
    addShortcut(rightTable, "Ctrl+0", tr("Reset zoom"));
    addCategory(rightTable, tr("File"));
    addShortcut(rightTable, "Ctrl+O", tr("Open project"));
    addShortcut(rightTable, "Ctrl+Shift+R", tr("Reload configuration"));
    addShortcut(rightTable, "Ctrl+Q", tr("Quit"));
    addCategory(rightTable, tr("Navigation"));
    addShortcut(rightTable, "Ctrl+[", tr("Navigate back"));
    addShortcut(rightTable, "Ctrl+]", tr("Navigate forward"));
    addCategory(rightTable, tr("AST"));
    addShortcut(rightTable, "Ctrl+Shift+M", tr("Go to macro definition"));
    addShortcut(rightTable, "Ctrl+I", tr("Inspect node details"));
    finishTable(rightTable);

    auto *columnsLayout = new QHBoxLayout;
    columnsLayout->addWidget(leftGroup);
    columnsLayout->addWidget(rightGroup);

    auto *closeButton = new QPushButton(tr("Close"), dialog);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);

    auto *mainLayout = new QVBoxLayout(dialog);
    mainLayout->addLayout(columnsLayout);
    mainLayout->addLayout(buttonLayout);

    dialog->resize(680, 480);
    dialog->show();
  });
  helpMenu->addAction(shortcutsAction);

  helpMenu->addSeparator();

  QAction *aboutAction = new QAction(tr("&About"), this);
  aboutAction->setStatusTip(tr("About this application"));
  connect(aboutAction, &QAction::triggered, this, &MainWindow::onShowAbout);
  helpMenu->addAction(aboutAction);
}

void MainWindow::onOpenConfigFile() {
  QString configPath = AppConfig::instance().getConfigFilePath();

  QFileInfo fileInfo(configPath);
  if (!fileInfo.exists()) {
    QMessageBox::warning(this, tr("Config File Not Found"),
                         tr("Configuration file not found:\n%1\n\n"
                            "The file should have been created automatically. "
                            "Try restarting the application.")
                             .arg(configPath));
    return;
  }

  // Open config file with system default editor
  QUrl fileUrl = QUrl::fromLocalFile(configPath);
  if (!QDesktopServices::openUrl(fileUrl)) {
    QMessageBox::information(
        this, tr("Open Config File"),
        tr("Could not open the configuration file automatically.\n\n"
           "Please open it manually with a text editor:\n%1")
            .arg(configPath));
  } else {
    logStatus(LogLevel::Info,
              tr("Opened config file: %1").arg(fileInfo.fileName()));
  }
}

void MainWindow::onReloadConfig() {
  AppConfig &config = AppConfig::instance();
  const bool configAvailable = config.reload();

  applyFontSize(config.getFontSize());

  if (parallelQueryRunner_) {
    parallelQueryRunner_->setParallelCount(config.getParallelProcessorCount());
  }
  if (astExtractorRunner_) {
    astExtractorRunner_->setCommentExtractionEnabled(
        config.getCommentExtractionEnabled());
  }

  if (!configAvailable) {
    logStatus(LogLevel::Warning,
              tr("Config file missing; created defaults and reloaded."));
    return;
  }

  logStatus(LogLevel::Info,
            tr("Reloaded configuration: %1").arg(config.getConfigFilePath()));
}

void MainWindow::onResetLayout() {
  if (defaultDockState_.isEmpty()) {
    return;
  }
  restoreState(defaultDockState_);
}

void MainWindow::onFocusChanged(QWidget *old, QWidget *now) {
  Q_UNUSED(old);

  // Map docks to their title bars for data-driven focus handling
  const std::initializer_list<std::pair<QDockWidget *, DockTitleBar *>>
      dockTitleBars = {{tuDock_, tuTitleBar_},
                       {sourceDock_, sourceTitleBar_},
                       {astDock_, astTitleBar_},
                       {declContextDock_, declContextTitleBar_},
                       {logDock_, logTitleBar_}};

  // Find which dock now has focus
  QDockWidget *activeDock = nullptr;
  if (now && astSearchPopup_ && astSearchPopup_->isAncestorOf(now)) {
    activeDock = astDock_;
  }
  for (const auto &[dock, titleBar] : dockTitleBars) {
    if (activeDock) {
      break;
    }
    if (now && dock && dock->isAncestorOf(now)) {
      activeDock = dock;
      break;
    }
  }

  if (focusedDock_ != activeDock) {
    // Update title bars using fast QPalette (not setStyleSheet)
    for (const auto &[dock, titleBar] : dockTitleBars) {
      if (titleBar) {
        titleBar->setFocused(dock == activeDock);
      }
    }
    focusedDock_ = activeDock;
  }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  if (astSearchPopup_ && astSearchPopup_->isVisible()) {
    syncAstSearchPopupGeometry();
  }
}

void MainWindow::moveEvent(QMoveEvent *event) {
  QMainWindow::moveEvent(event);
  if (astSearchPopup_ && astSearchPopup_->isVisible()) {
    syncAstSearchPopupGeometry();
  }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  if (watched == astDock_ && astSearchPopup_ && astSearchPopup_->isVisible()) {
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::Hide:
      syncAstSearchPopupGeometry();
      break;
    default:
      break;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onShowAbout() {
  QString aboutText =
      tr("<h2>ACAV (Clang AST Viewer)</h2>"
         "<p><b>Version:</b> %1</p>"
         "<p><b>Organization:</b> University of Victoria</p>"
         "<p><b>Supervisor:</b> Professor Michael Adams</p>"
         "<p><b>Developer:</b> Min Liu</p>"
         "<hr>"
         "<p>A tool for visualizing and exploring Clang Abstract Syntax Trees "
         "(AST) "
         "with support for C++20 modules.</p>"
         "<p><b>Features:</b></p>"
         "<ul>"
         "<li>Load and display AST from compilation databases</li>"
         "<li>Bidirectional navigation between source code and AST</li>"
         "<li>Support for C++20 modules</li>"
         "</ul>"
         "<p>Built with Qt and LLVM/Clang.</p>")
          .arg(ACAV_VERSION_STRING);

  QMessageBox::about(this, tr("About ACAV"), aboutText);
}

namespace {

class HistoryLineEdit : public QLineEdit {
public:
  explicit HistoryLineEdit(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
  void focusInEvent(QFocusEvent *event) override {
    QLineEdit::focusInEvent(event);
    showHistoryPopup();
  }

  void mousePressEvent(QMouseEvent *event) override {
    QLineEdit::mousePressEvent(event);
    if (event->button() == Qt::LeftButton) {
      showHistoryPopup();
    }
  }

private:
  void showHistoryPopup() {
    QCompleter *historyCompleter = completer();
    if (!historyCompleter) {
      return;
    }
    historyCompleter->setCompletionPrefix(text().trimmed());
    historyCompleter->complete();
  }
};

AcavJson sourceLocationToJson(const SourceLocation &loc,
                                const FileManager &fileManager) {
  AcavJson obj = AcavJson::object();
  if (!loc.isValid()) {
    obj["valid"] = false;
    return obj;
  }

  obj["valid"] = true;
  obj["fileId"] = loc.fileID();
  obj["line"] = loc.line();
  obj["column"] = loc.column();

  std::string_view filePath = fileManager.getFilePath(loc.fileID());
  if (!filePath.empty()) {
    obj["filePath"] = InternedString(std::string(filePath));
  }

  return obj;
}

AcavJson sourceRangeToJson(const SourceRange &range,
                             const FileManager &fileManager) {
  AcavJson obj = AcavJson::object();
  obj["begin"] = sourceLocationToJson(range.begin(), fileManager);
  obj["end"] = sourceLocationToJson(range.end(), fileManager);
  return obj;
}

bool parseSourceLocationJson(const AcavJson &obj, SourceLocation *out) {
  if (!out || !obj.is_object()) {
    return false;
  }
  auto fileIdIt = obj.find("fileId");
  auto lineIt = obj.find("line");
  auto columnIt = obj.find("column");
  if (fileIdIt == obj.end() || lineIt == obj.end() || columnIt == obj.end()) {
    return false;
  }
  if (!fileIdIt->is_number_integer() || !lineIt->is_number_integer() ||
      !columnIt->is_number_integer()) {
    return false;
  }

  FileID fileId = static_cast<FileID>(fileIdIt->get<uint64_t>());
  unsigned line = static_cast<unsigned>(lineIt->get<uint64_t>());
  unsigned column = static_cast<unsigned>(columnIt->get<uint64_t>());
  if (fileId == FileManager::InvalidFileID || line == 0 || column == 0) {
    return false;
  }

  *out = SourceLocation(fileId, line, column);
  return true;
}

bool parseSourceRangeJson(const AcavJson &obj, SourceRange *out) {
  if (!out || !obj.is_object()) {
    return false;
  }
  auto beginIt = obj.find("begin");
  auto endIt = obj.find("end");
  if (beginIt == obj.end() || endIt == obj.end()) {
    return false;
  }
  SourceLocation begin(FileManager::InvalidFileID, 0, 0);
  SourceLocation end(FileManager::InvalidFileID, 0, 0);
  if (!parseSourceLocationJson(*beginIt, &begin) ||
      !parseSourceLocationJson(*endIt, &end)) {
    return false;
  }
  *out = SourceRange(begin, end);
  return true;
}

AcavJson buildAstJsonTree(AstViewNode *root, const FileManager &fileManager) {
  if (!root) {
    return AcavJson();
  }

  auto makeNodeJson = [&fileManager](AstViewNode *node) {
    AcavJson obj = AcavJson::object();
    obj["properties"] = node->getProperties();
    obj["sourceRange"] = sourceRangeToJson(node->getSourceRange(), fileManager);
    return obj;
  };

  AcavJson rootJson = makeNodeJson(root);
  std::vector<std::pair<AstViewNode *, AcavJson *>> stack;
  stack.reserve(128);
  stack.push_back({root, &rootJson});

  while (!stack.empty()) {
    auto [node, jsonPtr] = stack.back();
    stack.pop_back();

    const auto &children = node->getChildren();
    AcavJson childArray = AcavJson::array();
    childArray.get_ref<AcavJson::array_t &>().reserve(children.size());
    (*jsonPtr)["children"] = std::move(childArray);
    auto &storedChildren = (*jsonPtr)["children"];

    for (AstViewNode *child : children) {
      if (!child) {
        continue;
      }
      AcavJson childJson = makeNodeJson(child);
      storedChildren.push_back(childJson);
      stack.push_back({child, &storedChildren.back()});
    }
  }

  return rootJson;
}

} // namespace

void MainWindow::setupDockWidgets() {

  // Create views
  tuView_ = new QTreeView(this);
  sourceView_ = new SourceCodeView(this);
  astView_ = new QTreeView(this);

  // Create dock widgets with custom title bars for fast focus indication
  tuDock_ = new QDockWidget(this);
  tuDock_->setObjectName("tuDock");
  tuDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  tuTitleBar_ = new DockTitleBar(tr("File Explorer"), tuDock_);
  tuDock_->setTitleBarWidget(tuTitleBar_);

  sourceDock_ = new QDockWidget(this);
  sourceDock_->setObjectName("sourceDock");
  sourceDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  sourceTitleBar_ = new DockTitleBar(tr("Source Code"), sourceDock_);
  sourceDock_->setTitleBarWidget(sourceTitleBar_);
  QWidget *sourceContainer = new QWidget(sourceDock_);
  setupSourceSearchPanel(sourceContainer);
  sourceDock_->setWidget(sourceContainer);

  astDock_ = new QDockWidget(this);
  astDock_->setObjectName("astDock");
  astDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  astTitleBar_ = new DockTitleBar(tr("AST"), astDock_);
  astDock_->setTitleBarWidget(astTitleBar_);
  QWidget *astContainer = new QWidget(astDock_);
  setupAstSearchPanel(astContainer);
  astDock_->setWidget(astContainer);

  setupTuSearch();

  // Create Declaration Context dock (to the right of AST dock)
  declContextDock_ = new QDockWidget(this);
  declContextDock_->setObjectName("declContextDock");
  declContextDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  declContextTitleBar_ =
      new DockTitleBar(tr("Declaration Context"), declContextDock_);
  declContextDock_->setTitleBarWidget(declContextTitleBar_);
  declContextView_ = new DeclContextView(this);
  declContextDock_->setWidget(declContextView_);

  logDock_ = new LogDock(this);
  logDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  logTitleBar_ = new DockTitleBar(tr("Logs"), logDock_);
  logDock_->setTitleBarWidget(logTitleBar_);
  logDock_->setFeatures(logDock_->features() | QDockWidget::DockWidgetMovable |
                        QDockWidget::DockWidgetFloatable);
  QPointer<LogDock> logDockPtr = logDock_;
  MemoryProfiler::setLogCallback([logDockPtr](const QString &message) {
    if (!logDockPtr) {
      return;
    }
    LogEntry entry;
    entry.level = LogLevel::Debug;
    entry.source = QStringLiteral("acav-memory");
    entry.message = message;
    entry.timestamp = QDateTime::currentDateTime();
    QMetaObject::invokeMethod(logDockPtr, "enqueue", Qt::QueuedConnection,
                              Q_ARG(LogEntry, entry));
  });

  // Layout: Top row (TU | Source | AST | DeclContext) with Log below
  // Add all docks to the same area and split them for proper resize handles
  addDockWidget(Qt::TopDockWidgetArea, tuDock_);
  addDockWidget(Qt::TopDockWidgetArea, sourceDock_);
  addDockWidget(Qt::TopDockWidgetArea, astDock_);
  addDockWidget(Qt::TopDockWidgetArea, declContextDock_);
  addDockWidget(Qt::BottomDockWidgetArea, logDock_);

  // Now we have all the docks
  // Begine to define the layout
  // Split top docks horizontally: TU | Source | AST | DeclContext
  splitDockWidget(tuDock_, sourceDock_, Qt::Horizontal);
  splitDockWidget(sourceDock_, astDock_, Qt::Horizontal);
  splitDockWidget(astDock_, declContextDock_, Qt::Horizontal);

  // The whole window size: 1600x900
  // Set initial sizes for top row (horizontal)
  resizeDocks({tuDock_, sourceDock_, astDock_, declContextDock_},
              {300, 450, 450, 200}, Qt::Horizontal);

  // Set vertical distribution: top docks large, log dock small (~4 lines)
  const int lineHeight = QFontMetrics(logDock_->font()).lineSpacing();
  const int logDockHeight =
      lineHeight * 4 + 70; // 4 lines + padding for tab bar + toolbar
  const int topDockHeight =
      900 - logDockHeight - 60; // Window height minus log and margins
  resizeDocks({sourceDock_, logDock_}, {topDockHeight, logDockHeight},
              Qt::Vertical);

  setupViewMenuDockActions();
  ::QTimer::singleShot(0, this, [this]() { defaultDockState_ = saveState(); });
}

void MainWindow::setupSourceSearchPanel(QWidget *container) {
  if (!container) {
    return;
  }

  auto *outerLayout = new QVBoxLayout(container);
  outerLayout->setContentsMargins(4, 4, 4, 4);
  outerLayout->setSpacing(4);

  auto *controlsLayout = new QHBoxLayout();
  controlsLayout->setContentsMargins(0, 0, 0, 0);
  controlsLayout->setSpacing(4);

  sourceSearchInput_ = new QLineEdit(container);
  sourceSearchInput_->setPlaceholderText(tr("Search source code..."));
  sourceSearchInput_->setClearButtonEnabled(true);
  sourceSearchInput_->setProperty("searchField", true);
  sourceSearchInput_->setMinimumHeight(28);

  sourceSearchPrevButton_ = new QToolButton(container);
  sourceSearchPrevButton_->setText(tr("Prev"));
  sourceSearchPrevButton_->setEnabled(false);
  sourceSearchPrevButton_->setProperty("searchButton", true);
  sourceSearchPrevButton_->setMinimumHeight(28);

  sourceSearchNextButton_ = new QToolButton(container);
  sourceSearchNextButton_->setText(tr("Next"));
  sourceSearchNextButton_->setEnabled(false);
  sourceSearchNextButton_->setProperty("searchButton", true);
  sourceSearchNextButton_->setMinimumHeight(28);

  sourceSearchStatus_ = new QLabel(container);
  sourceSearchStatus_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  sourceSearchStatus_->setMinimumWidth(80);
  sourceSearchStatus_->setProperty("searchStatus", true);

  sourceSearchDebounce_ = new ::QTimer(container);
  sourceSearchDebounce_->setSingleShot(true);
  sourceSearchDebounce_->setInterval(200);

  controlsLayout->addWidget(sourceSearchInput_, /*stretch=*/1);
  controlsLayout->addWidget(sourceSearchPrevButton_);
  controlsLayout->addWidget(sourceSearchNextButton_);
  controlsLayout->addWidget(sourceSearchStatus_);

  outerLayout->addLayout(controlsLayout);
  outerLayout->addWidget(sourceView_, /*stretch=*/1);

  connect(sourceSearchInput_, &QLineEdit::returnPressed, this,
          &MainWindow::onSourceSearchFindNext);
  connect(sourceSearchInput_, &QLineEdit::textChanged, this,
          &MainWindow::onSourceSearchTextChanged);
  connect(sourceSearchDebounce_, &::QTimer::timeout, this,
          &MainWindow::onSourceSearchDebounced);
  connect(sourceSearchPrevButton_, &QToolButton::clicked, this,
          &MainWindow::onSourceSearchFindPrevious);
  connect(sourceSearchNextButton_, &QToolButton::clicked, this,
          &MainWindow::onSourceSearchFindNext);
}

void MainWindow::setupAstSearchPanel(QWidget *container) {
  if (!container) {
    return;
  }

  auto *outerLayout = new QVBoxLayout(container);
  outerLayout->setContentsMargins(8, 8, 8, 8);
  outerLayout->setSpacing(8);

  // === Quick Search Section with Enhanced Visual Design ===
  auto *quickSearchFrame = new QWidget(container);
  quickSearchFrame->setObjectName("astQuickSearchFrame");
  
  auto *quickLayout = new QHBoxLayout(quickSearchFrame);
  quickLayout->setContentsMargins(8, 6, 8, 6);
  quickLayout->setSpacing(6);

  astSearchQuickInput_ = new QLineEdit(quickSearchFrame);
  astSearchQuickInput_->setObjectName("astSearchQuickInput");
  astSearchQuickInput_->setPlaceholderText(tr("Search AST nodes..."));
  astSearchQuickInput_->setClearButtonEnabled(true);
  astSearchQuickInput_->setProperty("searchField", true);
  astSearchQuickInput_->setMinimumHeight(28);

  auto *quickSearchButton = new QToolButton(quickSearchFrame);
  quickSearchButton->setObjectName("astSearchQuickButton");
  quickSearchButton->setText(tr("Advanced"));
  quickSearchButton->setToolTip(tr("Open advanced search options (Ctrl+F)"));
  quickSearchButton->setProperty("searchButton", true);
  quickSearchButton->setMinimumHeight(28);
  quickSearchButton->setMinimumWidth(90);

  quickLayout->addWidget(astSearchQuickInput_, /*stretch=*/1);
  quickLayout->addWidget(quickSearchButton);

  // === Advanced Search Popup with Modern Design ===
  astSearchPopup_ = new QDialog(this);
  astSearchPopup_->setObjectName("astSearchPopup");
  astSearchPopup_->setWindowTitle(tr("AST Advanced Search"));
  astSearchPopup_->setModal(false);
  astSearchPopup_->setWindowModality(Qt::NonModal);
  astSearchPopup_->setMinimumSize(320, 180);
  astSearchPopup_->resize(600, 220);

  auto *popupLayout = new QVBoxLayout(astSearchPopup_);
  popupLayout->setContentsMargins(12, 12, 12, 12);
  popupLayout->setSpacing(10);

  // Search input with improved styling
  auto *inputLabel = new QLabel(tr("<b>Search Pattern:</b>"), astSearchPopup_);
  popupLayout->addWidget(inputLabel);

  astSearchInput_ = new HistoryLineEdit(astSearchPopup_);
  astSearchInput_->setObjectName("astSearchInput");
  astSearchInput_->setPlaceholderText(
      tr("e.g., kind:FunctionDecl  name:main  type:.*int.*"));
  astSearchInput_->setClearButtonEnabled(true);
  astSearchInput_->setProperty("searchField", true);
  astSearchInput_->setMinimumHeight(32);
  astSearchInput_->setToolTip(
      tr("Use qualifiers like kind:, name:, type: for precise matching.\n"
         "Supports regex patterns.\n"
         "Examples: kind:FunctionDecl  name:main  type:.*int.*"));

  astSearchHistoryModel_ = new QStringListModel(astSearchPopup_);
  astSearchCompleter_ = new QCompleter(astSearchHistoryModel_, astSearchPopup_);
  astSearchCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
  astSearchCompleter_->setCompletionMode(QCompleter::PopupCompletion);
  astSearchCompleter_->setFilterMode(Qt::MatchContains);
  astSearchInput_->setCompleter(astSearchCompleter_);

  popupLayout->addWidget(astSearchInput_);

  // Separator line
  auto *separator = new QFrame(astSearchPopup_);
  separator->setFrameShape(QFrame::HLine);
  separator->setFrameShadow(QFrame::Sunken);
  popupLayout->addWidget(separator);

  // Controls section with better organization
  auto *controlsFrame = new QWidget(astSearchPopup_);
  auto *popupControlsLayout = new QHBoxLayout(controlsFrame);
  popupControlsLayout->setContentsMargins(0, 0, 0, 0);
  popupControlsLayout->setSpacing(8);

  // Project filter
  astSearchProjectFilter_ = new QCheckBox(tr("Project Files Only"), astSearchPopup_);
  astSearchProjectFilter_->setChecked(true);
  astSearchProjectFilter_->setToolTip(tr("Restrict search to files within the project root"));

  popupControlsLayout->addWidget(astSearchProjectFilter_);
  popupControlsLayout->addStretch(1);

  // Navigation buttons with better styling
  astSearchPrevButton_ = new QToolButton(astSearchPopup_);
  astSearchPrevButton_->setText(tr("Previous"));
  astSearchPrevButton_->setToolTip(tr("Find previous match (Shift+Enter)"));
  astSearchPrevButton_->setEnabled(false);
  astSearchPrevButton_->setProperty("searchButton", true);
  astSearchPrevButton_->setMinimumWidth(90);
  astSearchPrevButton_->setMinimumHeight(28);

  astSearchNextButton_ = new QToolButton(astSearchPopup_);
  astSearchNextButton_->setText(tr("Next"));
  astSearchNextButton_->setToolTip(tr("Find next match (Enter)"));
  astSearchNextButton_->setEnabled(false);
  astSearchNextButton_->setProperty("searchButton", true);
  astSearchNextButton_->setMinimumWidth(90);
  astSearchNextButton_->setMinimumHeight(28);

  auto *astSearchButton = new QToolButton(astSearchPopup_);
  astSearchButton->setText(tr("Search"));
  astSearchButton->setToolTip(tr("Execute search"));
  astSearchButton->setProperty("searchButton", true);
  astSearchButton->setMinimumWidth(90);
  astSearchButton->setMinimumHeight(28);

  popupControlsLayout->addWidget(astSearchPrevButton_);
  popupControlsLayout->addWidget(astSearchNextButton_);
  popupControlsLayout->addWidget(astSearchButton);

  popupLayout->addWidget(controlsFrame);

  // Status bar at the bottom
  astSearchStatus_ = new QLabel(astSearchPopup_);
  astSearchStatus_->setObjectName("astSearchStatus");
  astSearchStatus_->setAlignment(Qt::AlignCenter);
  astSearchStatus_->setMinimumHeight(24);
  astSearchStatus_->setProperty("searchStatus", true);
  popupLayout->addWidget(astSearchStatus_);

  // === Compilation Warning Banner ===
  astCompilationWarningLabel_ = new QLabel(container);
  astCompilationWarningLabel_->setObjectName("astCompilationWarningLabel");
  astCompilationWarningLabel_->setText(
      tr("<b>Compilation Errors Detected:</b> AST may be incomplete. "
         "See log for details."));
  astCompilationWarningLabel_->setTextFormat(Qt::RichText);
  astCompilationWarningLabel_->setWordWrap(true);
  // Styled by #astCompilationWarningLabel in style.qss
  astCompilationWarningLabel_->setVisible(false);

  outerLayout->addWidget(quickSearchFrame);
  outerLayout->addWidget(astCompilationWarningLabel_);
  outerLayout->addWidget(astView_, /*stretch=*/1);

  connect(astSearchQuickInput_, &QLineEdit::textChanged, this,
          [this](const QString &text) {
            if (astSearchInput_ && astSearchInput_->text() != text) {
              astSearchInput_->setText(text);
            }
          });
  auto triggerQuickSearch = [this]() {
    if (!astSearchInput_ || !astSearchQuickInput_) {
      return;
    }
    const QString query = astSearchQuickInput_->text();
    if (astSearchInput_->text() != query) {
      astSearchInput_->setText(query);
    }
    showAstSearchPopup(false);
    onAstSearchFindNext();
  };
  connect(astSearchQuickInput_, &QLineEdit::returnPressed, this,
          triggerQuickSearch);
  connect(quickSearchButton, &QToolButton::clicked, this, triggerQuickSearch);
  connect(astSearchInput_, &QLineEdit::returnPressed, this,
          &MainWindow::onAstSearchFindNext);
  connect(astSearchInput_, &QLineEdit::textChanged, this,
          &MainWindow::onAstSearchTextChanged);
  connect(astSearchInput_, &QLineEdit::textChanged, this,
          [this](const QString &text) {
            if (astSearchQuickInput_ && astSearchQuickInput_->text() != text) {
              astSearchQuickInput_->setText(text);
            }
          });
  connect(astSearchButton, &QToolButton::clicked, this,
          &MainWindow::onAstSearchFindNext);
  connect(astSearchPrevButton_, &QToolButton::clicked, this,
          &MainWindow::onAstSearchFindPrevious);
  connect(astSearchNextButton_, &QToolButton::clicked, this,
          &MainWindow::onAstSearchFindNext);
  connect(astSearchProjectFilter_, &QCheckBox::toggled, this,
          &MainWindow::clearAstSearchState);

  auto *prevShortcut =
      new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return), astSearchPopup_);
  connect(prevShortcut, &QShortcut::activated, this,
          &MainWindow::onAstSearchFindPrevious);
  auto *prevNumpadShortcut =
      new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Enter), astSearchPopup_);
  connect(prevNumpadShortcut, &QShortcut::activated, this,
          &MainWindow::onAstSearchFindPrevious);
  auto *closeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape),
                                      astSearchPopup_);
  connect(closeShortcut, &QShortcut::activated, astSearchPopup_,
          &QDialog::hide);

  if (astDock_) {
    astDock_->installEventFilter(this);
  }

  // Ensure popup starts with configured base font size.
  QFont popupFont = QApplication::font();
  popupFont.setPointSize(AppConfig::instance().getFontSize());
  QString configuredFamily = AppConfig::instance().getFontFamily();
  if (!configuredFamily.isEmpty()) {
    popupFont.setFamily(configuredFamily);
  }
  astSearchPopup_->setFont(popupFont);
  if (astSearchCompleter_ && astSearchCompleter_->popup()) {
    astSearchCompleter_->popup()->setFont(popupFont);
  }

  astSearchPopup_->hide();
}

void MainWindow::setupTuSearch() {
  tuSearch_ = new QLineEdit(this);
  tuSearch_->setPlaceholderText(tr("Filter files..."));
  tuSearch_->setClearButtonEnabled(true);
  tuSearch_->setProperty("searchField", true);
  tuSearch_->setMinimumHeight(28);
  connect(tuSearch_, &QLineEdit::textChanged, this,
          [this](const QString &text) {
            QString needle = text.trimmed();

            // Suspend updates during batch setRowHidden calls to avoid
            // O(N) layout recalculations (one per row).
            tuView_->setUpdatesEnabled(false);

            // Recursive filter that shows parents when children match
            std::function<bool(const QModelIndex &)> filterRecursive =
                [&](const QModelIndex &parent) -> bool {
              bool anyChildVisible = false;
              int rowCount = tuModel_->rowCount(parent);

              for (int i = 0; i < rowCount; ++i) {
                QModelIndex idx = tuModel_->index(i, 0, parent);
                QString name = tuModel_->data(idx, Qt::DisplayRole).toString();

                // Check if this item matches
                bool matches = needle.isEmpty() ||
                               name.contains(needle, Qt::CaseInsensitive);

                // Recursively check children
                bool childVisible = filterRecursive(idx);

                // Item is visible if it matches or has visible children
                bool visible = matches || childVisible;
                tuView_->setRowHidden(i, parent, !visible);

                if (visible) {
                  anyChildVisible = true;
                }
              }

              return anyChildVisible;
            };

            filterRecursive(QModelIndex());

            tuView_->setUpdatesEnabled(true);
          });

  // Place the search above the TU tree
  QWidget *container = new QWidget(tuDock_);
  QVBoxLayout *layout = new QVBoxLayout(container);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);
  layout->addWidget(tuSearch_);
  layout->addWidget(tuView_);
  tuDock_->setWidget(container);
}

void MainWindow::triggerSourceSearch(bool forward) {
  if (!sourceView_ || !sourceSearchInput_) {
    return;
  }

  const QString term = sourceSearchInput_->text();
  if (term.trimmed().isEmpty()) {
    if (sourceSearchStatus_) {
      sourceSearchStatus_->setText(tr("Enter text"));
    }
    return;
  }

  bool found =
      forward ? sourceView_->findNext(term) : sourceView_->findPrevious(term);
  if (sourceSearchStatus_) {
    sourceSearchStatus_->setText(found ? QString() : tr("No matches"));
  }
}

void MainWindow::onSourceSearchTextChanged(const QString &text) {
  const bool hasText = !text.trimmed().isEmpty();

  // These pointers are set up in setupSourceSearchPanel and always valid
  sourceSearchPrevButton_->setEnabled(hasText);
  sourceSearchNextButton_->setEnabled(hasText);
  sourceView_->clearSearchHighlight();
  sourceSearchStatus_->clear();

  if (hasText) {
    sourceSearchDebounce_->start();
  } else {
    sourceSearchDebounce_->stop();
  }
}

void MainWindow::onSourceSearchDebounced() { triggerSourceSearch(true); }

void MainWindow::onSourceSearchFindNext() {
  if (sourceSearchDebounce_) {
    sourceSearchDebounce_->stop();
  }
  triggerSourceSearch(true);
}

void MainWindow::onSourceSearchFindPrevious() {
  if (sourceSearchDebounce_) {
    sourceSearchDebounce_->stop();
  }
  triggerSourceSearch(false);
}

void MainWindow::onAstSearchTextChanged(const QString &text) {
  const bool hasText = !text.trimmed().isEmpty();

  astSearchPrevButton_->setEnabled(hasText);
  astSearchNextButton_->setEnabled(hasText);
  clearAstSearchState();
}

void MainWindow::onAstSearchDebounced() { triggerAstSearch(true); }

void MainWindow::onAstSearchFindNext() { triggerAstSearch(true); }

void MainWindow::onAstSearchFindPrevious() { triggerAstSearch(false); }

void MainWindow::triggerAstSearch(bool forward) {
  if (!astSearchInput_) {
    return;
  }

  const QString expression = astSearchInput_->text().trimmed();
  if (expression.isEmpty()) {
    return;
  }
  rememberAstSearchQuery(expression);

  if (!astModel_ || !astModel_->hasNodes()) {
    setAstSearchStatus(tr("No AST loaded"), true);
    return;
  }

  if (astSearchMatches_.empty()) {
    collectAstSearchMatches(expression);
    // collectAstSearchMatches sets status on invalid regex
    if (astSearchMatches_.empty()) {
      if (astSearchStatus_ &&
          astSearchStatus_->text() != tr("Invalid pattern")) {
        setAstSearchStatus(tr("No matches"), false);
      }
      return;
    }
    // First search: start at first match for forward, last for backward
    astSearchCurrentIndex_ =
        forward ? 0 : static_cast<int>(astSearchMatches_.size()) - 1;
  } else {
    int count = static_cast<int>(astSearchMatches_.size());
    if (forward) {
      astSearchCurrentIndex_ = (astSearchCurrentIndex_ + 1) % count;
    } else {
      astSearchCurrentIndex_ = (astSearchCurrentIndex_ - 1 + count) % count;
    }
  }

  navigateToAstMatch(astSearchCurrentIndex_);
  setAstSearchStatus(QString("%1 of %2")
                         .arg(astSearchCurrentIndex_ + 1)
                         .arg(astSearchMatches_.size()),
                     false);
}

void MainWindow::collectAstSearchMatches(const QString &expression) {
  astSearchMatches_.clear();
  astSearchCurrentIndex_ = -1;

  struct AstSearchCondition {
    QString key;
    QRegularExpression regex;
  };

  // Parse expression: scan for qualifier boundaries (word:)
  std::vector<AstSearchCondition> conditions;
  QRegularExpression qualifierPattern(QStringLiteral("(\\w+):"));
  auto it = qualifierPattern.globalMatch(expression);

  // Collect qualifier positions
  struct QualifierPos {
    int start;      // start of "key:"
    int valueStart; // position after ":"
    QString key;
  };
  std::vector<QualifierPos> qualifiers;
  while (it.hasNext()) {
    auto match = it.next();
    qualifiers.push_back({static_cast<int>(match.capturedStart()),
                          static_cast<int>(match.capturedEnd()),
                          match.captured(1)});
  }

  if (qualifiers.empty()) {
    // Entire expression is bare text
    QRegularExpression regex(expression,
                             QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid()) {
      setAstSearchStatus(tr("Invalid pattern"), true);
      return;
    }
    conditions.push_back({QString(), std::move(regex)});
  } else {
    // Text before first qualifier is bare text
    if (qualifiers.front().start > 0) {
      QString bareText = expression.left(qualifiers.front().start).trimmed();
      if (!bareText.isEmpty()) {
        QRegularExpression regex(bareText,
                                 QRegularExpression::CaseInsensitiveOption);
        if (!regex.isValid()) {
          setAstSearchStatus(tr("Invalid pattern"), true);
          return;
        }
        conditions.push_back({QString(), std::move(regex)});
      }
    }

    // Process each qualifier
    for (std::size_t i = 0; i < qualifiers.size(); ++i) {
      int valueStart = qualifiers[i].valueStart;
      int valueEnd = (i + 1 < qualifiers.size()) ? qualifiers[i + 1].start
                                                 : expression.size();
      QString value =
          expression.mid(valueStart, valueEnd - valueStart).trimmed();
      // Anchor qualified searches for exact matching:
      // name:main matches "main" but not "remainder"
      QString anchored =
          value.isEmpty() ? value : QStringLiteral("^(?:%1)$").arg(value);
      QRegularExpression regex(anchored,
                               QRegularExpression::CaseInsensitiveOption);
      if (!regex.isValid()) {
        setAstSearchStatus(tr("Invalid pattern"), true);
        return;
      }
      conditions.push_back({qualifiers[i].key, std::move(regex)});
    }
  }

  // Project-only filter state
  QString projectRoot = tuModel_ ? tuModel_->projectRoot() : QString();
  bool projectFilterActive =
      astSearchProjectFilter_ && astSearchProjectFilter_->isChecked();
  std::unordered_map<FileID, bool> projectFileCache;

  // DFS traversal over AST (iterative pre-order)
  AstViewNode *root = astModel_->selectedNode();
  // We need the actual tree root, not the selected node
  // Access root via the model's index(0,0) which gives the first top-level node
  QModelIndex rootIndex = astModel_->index(0, 0);
  if (!rootIndex.isValid()) {
    return;
  }
  root = static_cast<AstViewNode *>(
      rootIndex.data(AstModel::NodePtrRole).value<void *>());
  if (!root) {
    return;
  }
  // The root in the model is the actual root - walk from there
  // But we need to go up to the true root (parent of root index)
  // Actually, the model's root_ is not directly accessible. We traverse
  // starting from the model's top-level children.
  // Let's collect all top-level nodes and DFS from each.
  std::vector<AstViewNode *> stack;
  int topLevelCount = astModel_->rowCount();
  for (int i = topLevelCount - 1; i >= 0; --i) {
    QModelIndex idx = astModel_->index(i, 0);
    if (idx.isValid()) {
      auto *node = static_cast<AstViewNode *>(
          idx.data(AstModel::NodePtrRole).value<void *>());
      if (node) {
        stack.push_back(node);
      }
    }
  }

  auto jsonValueToString = [](const AcavJson &value) -> QString {
    if (value.is_boolean()) {
      return value.get<bool>() ? QStringLiteral("true")
                               : QStringLiteral("false");
    }
    if (value.is_number_integer()) {
      return QString::number(value.get<int64_t>());
    }
    if (value.is_number_unsigned()) {
      return QString::number(value.get<uint64_t>());
    }
    if (value.is_number_float()) {
      return QString::number(value.get<double>());
    }
    if (value.is_string()) {
      return QString::fromStdString(value.get<InternedString>().str());
    }
    return {};
  };

  while (!stack.empty()) {
    AstViewNode *node = stack.back();
    stack.pop_back();

    // Project-only filter: skip nodes from external files
    if (projectFilterActive) {
      FileID fileId = node->getSourceRange().begin().fileID();
      auto cacheIt = projectFileCache.find(fileId);
      if (cacheIt == projectFileCache.end()) {
        bool isProject = false;
        if (fileId != FileManager::InvalidFileID && !projectRoot.isEmpty()) {
          std::string_view path = fileManager_.getFilePath(fileId);
          if (!path.empty()) {
            QString qpath =
                QString::fromUtf8(path.data(), static_cast<int>(path.size()));
            isProject = (qpath == projectRoot) ||
                        qpath.startsWith(projectRoot + QChar('/'));
          }
        }
        cacheIt = projectFileCache.emplace(fileId, isProject).first;
      }
      if (!cacheIt->second) {
        // Still traverse children -- they may be in project files
        const auto &children = node->getChildren();
        for (auto childIt = children.rbegin(); childIt != children.rend();
             ++childIt) {
          if (*childIt)
            stack.push_back(*childIt);
        }
        continue;
      }
    }

    const auto &props = node->getProperties();
    bool allMatch = true;

    for (const auto &cond : conditions) {
      if (cond.key.isEmpty()) {
        // Bare text: search all properties on the node
        bool anyPropMatch = false;
        if (props.is_object()) {
          for (auto it = props.begin(); it != props.end(); ++it) {
            QString val = jsonValueToString(*it);
            if (!val.isEmpty() && cond.regex.match(val).hasMatch()) {
              anyPropMatch = true;
              break;
            }
          }
        }
        if (!anyPropMatch) {
          allMatch = false;
          break;
        }
      } else {
        // Qualified: check specific property
        QByteArray keyUtf8 = cond.key.toUtf8();
        const char *keyStr = keyUtf8.constData();
        auto propIt = props.find(keyStr);
        if (propIt == props.end()) {
          allMatch = false;
          break;
        }
        QString val = jsonValueToString(*propIt);
        if (!cond.regex.match(val).hasMatch()) {
          allMatch = false;
          break;
        }
      }
    }

    if (allMatch) {
      astSearchMatches_.push_back(node);
    }

    // Push children in reverse order for pre-order traversal
    const auto &children = node->getChildren();
    for (auto childIt = children.rbegin(); childIt != children.rend();
         ++childIt) {
      if (*childIt) {
        stack.push_back(*childIt);
      }
    }
  }
}

void MainWindow::navigateToAstMatch(int index) {
  if (index < 0 || index >= static_cast<int>(astSearchMatches_.size())) {
    return;
  }
  AstViewNode *node = astSearchMatches_[index];
  QModelIndex modelIndex = astModel_->selectNode(node);
  astView_->setCurrentIndex(modelIndex);
  astView_->scrollTo(modelIndex);
}

void MainWindow::clearAstSearchState() {
  astSearchMatches_.clear();
  astSearchCurrentIndex_ = -1;
  if (astSearchStatus_) {
    astSearchStatus_->clear();
    astSearchStatus_->setProperty("searchStatus", true);
    astSearchStatus_->style()->unpolish(astSearchStatus_);
    astSearchStatus_->style()->polish(astSearchStatus_);
  }
}

void MainWindow::setAstSearchStatus(const QString &text, bool isError) {
  if (!astSearchStatus_) {
    return;
  }
  astSearchStatus_->setText(text);

  // Toggle between normal/error appearance via dynamic property.
  // The QSS selectors [searchStatus="true"] and [searchStatus="error"]
  // handle the actual visual styling (see style.qss).
  if (isError) {
    astSearchStatus_->setProperty("searchStatus", QStringLiteral("error"));
  } else {
    astSearchStatus_->setProperty("searchStatus", true);
  }
  astSearchStatus_->style()->unpolish(astSearchStatus_);
  astSearchStatus_->style()->polish(astSearchStatus_);
}

void MainWindow::showAstSearchPopup(bool selectAll) {
  if (!astSearchPopup_ || !astSearchInput_) {
    return;
  }

  syncAstSearchPopupGeometry();
  astSearchPopup_->show();

  astSearchPopup_->raise();
  astSearchPopup_->activateWindow();
  astSearchInput_->setFocus();
  if (selectAll) {
    astSearchInput_->selectAll();
  }
}

void MainWindow::syncAstSearchPopupGeometry() {
  if (!astSearchPopup_) {
    return;
  }

  const int minWidth = astSearchPopup_->minimumWidth();
  const int maxWidth = 900;
  int targetWidth = astSearchPopup_->width();
  QPoint anchor(0, 0);

  if (astDock_) {
    const int availableWidth = qMax(minWidth, astDock_->width() - 16);
    const int preferredWidth = static_cast<int>(availableWidth * 0.85);
    targetWidth = qBound(minWidth, preferredWidth, qMin(maxWidth, availableWidth));
    const int x = qMax(8, astDock_->width() - targetWidth - 8);
    anchor = astDock_->mapToGlobal(QPoint(x, 40));
  } else {
    const int availableWidth = qMax(minWidth, width() - 32);
    const int preferredWidth = static_cast<int>(availableWidth * 0.55);
    targetWidth = qBound(minWidth, preferredWidth, qMin(maxWidth, availableWidth));
    anchor = mapToGlobal(QPoint(qMax(8, width() - targetWidth - 16), 40));
  }

  int targetHeight = qMax(astSearchPopup_->sizeHint().height(),
                          astSearchPopup_->minimumHeight());
  astSearchPopup_->resize(targetWidth, targetHeight);
  astSearchPopup_->move(anchor);
}

void MainWindow::rememberAstSearchQuery(const QString &query) {
  const QString trimmed = query.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }

  astSearchHistory_.removeAll(trimmed);
  astSearchHistory_.prepend(trimmed);

  constexpr int kMaxQueryHistory = 20;
  while (astSearchHistory_.size() > kMaxQueryHistory) {
    astSearchHistory_.removeLast();
  }

  if (astSearchHistoryModel_) {
    astSearchHistoryModel_->setStringList(astSearchHistory_);
  }
}

void MainWindow::setAstCompilationWarningVisible(bool visible) {
  if (!astCompilationWarningLabel_) {
    return;
  }
  astCompilationWarningLabel_->setVisible(visible);
}

void MainWindow::applyFontSize(int size) {
  // Validate and clamp font size to valid range
  if (size < kMinFontSize) {
    size = kMinFontSize;
  } else if (size > kMaxFontSize) {
    size = kMaxFontSize;
  }
  currentFontSize_ = size;
  tuFontSize_ = size;
  sourceFontSize_ = size;
  astFontSize_ = size;
  declContextFontSize_ = size;
  logFontSize_ = size;
  currentFontFamily_ = AppConfig::instance().getFontFamily();

  QFont baseFont = QApplication::font();
  if (!currentFontFamily_.isEmpty()) {
    baseFont.setFamily(currentFontFamily_);
  }
  baseFont.setPointSize(size);

  auto applyFont = [&baseFont](QWidget *widget) {
    if (!widget) {
      return;
    }
    widget->setFont(baseFont);
  };

  applyFont(tuView_);
  applyFont(astView_);
  if (declContextView_) {
    declContextView_->applyFont(baseFont);
  }
  applyFont(logDock_);
  applyFont(nodeCycleWidget_);
  applyFont(astSearchQuickInput_);
  applyFont(astSearchPopup_);
  if (astSearchCompleter_ && astSearchCompleter_->popup()) {
    astSearchCompleter_->popup()->setFont(baseFont);
  }
  if (sourceView_) {
    sourceView_->setFont(baseFont);
    sourceView_->applyFontSize(size);
  }
}

void MainWindow::adjustFontSize(int delta) {
  // Per-subwindow font size: adjust only the focused dock
  currentFontFamily_ = AppConfig::instance().getFontFamily();
  QFont baseFont = QApplication::font();
  if (!currentFontFamily_.isEmpty()) {
    baseFont.setFamily(currentFontFamily_);
  }

  auto adjustWidget = [&](QWidget *widget, int *fontSize) {
    if (!widget || !fontSize) {
      return;
    }
    int nextSize = *fontSize + delta;
    if (nextSize < kMinFontSize) {
      nextSize = kMinFontSize;
    } else if (nextSize > kMaxFontSize) {
      nextSize = kMaxFontSize;
    }
    if (nextSize == *fontSize) {
      return;
    }
    *fontSize = nextSize;
    baseFont.setPointSize(nextSize);
    widget->setFont(baseFont);
  };

  if (focusedDock_ == tuDock_) {
    adjustWidget(tuView_, &tuFontSize_);
  } else if (focusedDock_ == sourceDock_) {
    int nextSize = sourceFontSize_ + delta;
    if (nextSize < kMinFontSize) {
      nextSize = kMinFontSize;
    } else if (nextSize > kMaxFontSize) {
      nextSize = kMaxFontSize;
    }
    if (nextSize != sourceFontSize_ && sourceView_) {
      sourceFontSize_ = nextSize;
      baseFont.setPointSize(nextSize);
      sourceView_->setFont(baseFont);
      sourceView_->applyFontSize(nextSize);
    }
  } else if (focusedDock_ == astDock_) {
    adjustWidget(astView_, &astFontSize_);
    baseFont.setPointSize(astFontSize_);
    if (astSearchQuickInput_) {
      astSearchQuickInput_->setFont(baseFont);
    }
    if (astSearchPopup_) {
      astSearchPopup_->setFont(baseFont);
    }
    if (astSearchCompleter_ && astSearchCompleter_->popup()) {
      astSearchCompleter_->popup()->setFont(baseFont);
    }
  } else if (focusedDock_ == declContextDock_) {
    int nextSize = declContextFontSize_ + delta;
    if (nextSize < kMinFontSize) {
      nextSize = kMinFontSize;
    } else if (nextSize > kMaxFontSize) {
      nextSize = kMaxFontSize;
    }
    if (nextSize != declContextFontSize_ && declContextView_) {
      declContextFontSize_ = nextSize;
      baseFont.setPointSize(nextSize);
      declContextView_->applyFont(baseFont);
    }
  } else if (focusedDock_ == logDock_) {
    adjustWidget(logDock_, &logFontSize_);
  } else {
    // Fallback: apply to all (e.g., when no dock focused)
    int nextSize = currentFontSize_ + delta;
    if (nextSize < kMinFontSize) {
      nextSize = kMinFontSize;
    } else if (nextSize > kMaxFontSize) {
      nextSize = kMaxFontSize;
    }
    if (nextSize != currentFontSize_) {
      applyFontSize(nextSize);
    }
  }
}

void MainWindow::expandFileExplorerTopLevel() {
  if (!tuView_ || !tuModel_) {
    return;
  }

  // Expand top-level items (Project Files, External Files)
  int topLevelCount = tuModel_->rowCount();
  for (int i = 0; i < topLevelCount; ++i) {
    QModelIndex topLevelIndex = tuModel_->index(i, 0);
    tuView_->expand(topLevelIndex);
  }
}

void MainWindow::setupModels() {
  // Create models
  tuModel_ = new TranslationUnitModel(fileManager_, this);
  astModel_ = new AstModel(this);

  // Set models to views
  tuView_->setModel(tuModel_);
  astView_->setModel(astModel_);

  // Configure tree views for performance
  auto configureTreeView = [](QTreeView *view) {
    view->setHeaderHidden(true);
    view->setAnimated(false);
    view->setUniformRowHeights(true);
    view->setTextElideMode(Qt::ElideNone);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->header()->setStretchLastSection(false);
    // Use ResizeToContents so the column auto-sizes to the widest node.
    // Batch expand/collapse operations temporarily switch to Fixed mode
    // to avoid O(N) width recalculation on every individual expand/collapse.
    view->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  };
  configureTreeView(tuView_);
  configureTreeView(astView_);

  // Create query runners
  queryRunner_ = new QueryDependenciesRunner(this);
  parallelQueryRunner_ = new QueryDependenciesParallelRunner(this);

  // Apply saved parallel processor count
  parallelQueryRunner_->setParallelCount(
      AppConfig::instance().getParallelProcessorCount());

  // Create make-ast runner
  makeAstRunner_ = new MakeAstRunner(this);

  // Resolve Clang resource directory once and share with all runners
  const std::string clangResourceDir = acav::getClangResourceDir();
  if (!clangResourceDir.empty()) {
    QString dir = QString::fromStdString(clangResourceDir);
    queryRunner_->setClangResourceDir(dir);
    parallelQueryRunner_->setClangResourceDir(dir);
    makeAstRunner_->setClangResourceDir(dir);
  }

  // Create initial AstContext
  astContext_ = std::make_unique<AstContext>();
  ++astVersion_;
  applyFontSize(AppConfig::instance().getFontSize());

  // Create worker thread for AST extraction
  astWorkerThread_ = new QThread(this);
  astExtractorRunner_ = new AstExtractorRunner(astContext_.get(), fileManager_);
  astExtractorRunner_->setCommentExtractionEnabled(
      AppConfig::instance().getCommentExtractionEnabled());

  // Create node cycle widget
  nodeCycleWidget_ = new NodeCycleWidget(this);
  astExtractorRunner_->moveToThread(astWorkerThread_);
  astWorkerThread_->start();
}

void MainWindow::connectSignals() {
  // Menu actions
  connect(openAction_, &QAction::triggered, this,
          &MainWindow::onOpenCompilationDatabase);
  connect(exitAction_, &QAction::triggered, this, &MainWindow::onExit);

  // Focus tracking for visual indicator (uses fast QPalette, not setStyleSheet)
  connect(qApp, &QApplication::focusChanged, this, &MainWindow::onFocusChanged);

  // Keyboard shortcuts for switching focus between panes
  auto addFocusShortcut = [this](const QKeySequence &key, auto callback) {
    auto *action = new QAction(this);
    action->setShortcut(key);
    connect(action, &QAction::triggered, this, callback);
    addAction(action);
  };

  addFocusShortcut(Qt::CTRL | Qt::Key_1, [this]() { tuView_->setFocus(); });
  addFocusShortcut(Qt::CTRL | Qt::Key_2, [this]() { sourceView_->setFocus(); });
  addFocusShortcut(Qt::CTRL | Qt::Key_3, [this]() { astView_->setFocus(); });
  addFocusShortcut(Qt::CTRL | Qt::Key_4,
                   [this]() { declContextView_->focusSemanticTree(); });
  addFocusShortcut(Qt::CTRL | Qt::Key_5,
                   [this]() { declContextView_->focusLexicalTree(); });
  addFocusShortcut(Qt::CTRL | Qt::Key_6, [this]() { logDock_->focusAllTab(); });
  addFocusShortcut(QKeySequence::Find, [this]() {
    if (focusedDock_ == tuDock_ && tuSearch_) {
      tuSearch_->setFocus();
      tuSearch_->selectAll();
    } else if (focusedDock_ == astDock_ && astSearchQuickInput_) {
      astSearchQuickInput_->setFocus();
      astSearchQuickInput_->selectAll();
    } else if (sourceSearchInput_) {
      sourceSearchInput_->setFocus();
      sourceSearchInput_->selectAll();
    }
  });

  // Expand/Collapse shortcuts for tree views
  addFocusShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_E, [this]() {
    if (astView_->hasFocus()) {
      expandAllChildren(astView_);
    } else if (tuView_->hasFocus()) {
      onExpandAllTuChildren(); // Context-aware: directories vs source files
    }
  });
  addFocusShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_C, [this]() {
    if (astView_->hasFocus()) {
      collapseAllChildren(astView_);
    } else if (tuView_->hasFocus()) {
      onCollapseAllTuChildren(); // Context-aware: directories vs source files
    }
  });

  // F5 to extract AST for selected file
  addFocusShortcut(Qt::Key_F5, [this]() {
    QModelIndex index = tuView_->currentIndex();
    if (index.isValid()) {
      onTranslationUnitSelected(index);
    }
  });

  // Ctrl+I to inspect (view details of) the selected AST node
  addFocusShortcut(Qt::CTRL | Qt::Key_I, [this]() {
    QModelIndex index = astView_->currentIndex();
    if (!index.isValid()) {
      return;
    }
    auto *node = static_cast<AstViewNode *>(index.internalPointer());
    if (node) {
      onViewNodeDetails(node);
    }
  });

  // View selections (keyboard/mouse current change to load source,
  // double-click to load AST)
  if (tuView_->selectionModel()) {
    connect(tuView_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &current, const QModelIndex &) {
              onTranslationUnitClicked(current);
            });
  }
  connect(tuView_, &QTreeView::doubleClicked, this,
          &MainWindow::onTranslationUnitSelected);

  // Navigation signals
  connect(astView_->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &MainWindow::onAstNodeSelected);
  // Update DeclContext panel when AST node selection changes
  connect(
      astView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
      [this](const QModelIndex &current, const QModelIndex &) {
        if (isNavigatingFromDeclContext_) {
          return; // Don't update when navigation originated from DeclContext
        }
        if (!current.isValid()) {
          declContextView_->clear();
          return;
        }
        auto *node = static_cast<AstViewNode *>(current.internalPointer());
        declContextView_->setSelectedNode(node);
      });
  // Navigate AST when user clicks on a declaration context entry
  connect(declContextView_, &DeclContextView::contextNodeClicked, this,
          [this](AstViewNode *node) {
            if (!node) {
              return;
            }
            isNavigatingFromDeclContext_ = true;
            QModelIndex modelIndex = astModel_->selectNode(node);
            if (modelIndex.isValid()) {
              astView_->selectionModel()->setCurrentIndex(
                  modelIndex, QItemSelectionModel::ClearAndSelect);
              astView_->scrollTo(modelIndex);
            }
            isNavigatingFromDeclContext_ = false;
          });
  astView_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(astView_, &QTreeView::customContextMenuRequested, this,
          &MainWindow::onAstContextMenuRequested);
  tuView_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tuView_, &QTreeView::customContextMenuRequested, this,
          &MainWindow::onTuContextMenuRequested);
  connect(sourceView_, &SourceCodeView::sourcePositionClicked, this,
          &MainWindow::onSourcePositionClicked);
  connect(sourceView_, &SourceCodeView::sourceRangeSelected, this,
          &MainWindow::onSourceRangeSelected);
  connect(nodeCycleWidget_, &NodeCycleWidget::nodeSelected, this,
          &MainWindow::onCycleNodeSelected);
  connect(nodeCycleWidget_, &NodeCycleWidget::closed, this,
          &MainWindow::onCycleWidgetClosed);

  // Query runner signals (sequential)
  connect(queryRunner_, &QueryDependenciesRunner::dependenciesReady, this,
          &MainWindow::onDependenciesReady);
  connect(queryRunner_, &QueryDependenciesRunner::dependenciesReadyWithErrors,
          this, &MainWindow::onDependenciesReadyWithErrors);
  connect(queryRunner_, &QueryDependenciesRunner::error, this,
          &MainWindow::onDependenciesError);
  connect(queryRunner_, &QueryDependenciesRunner::progress, this,
          &MainWindow::onDependenciesProgress);
  if (logDock_) {
    connect(queryRunner_, &QueryDependenciesRunner::logMessage, logDock_,
            &LogDock::enqueue);
  }

  // Parallel query runner signals
  connect(parallelQueryRunner_,
          &QueryDependenciesParallelRunner::dependenciesReady, this,
          &MainWindow::onDependenciesReady);
  connect(parallelQueryRunner_,
          &QueryDependenciesParallelRunner::dependenciesReadyWithErrors, this,
          &MainWindow::onDependenciesReadyWithErrors);
  connect(parallelQueryRunner_, &QueryDependenciesParallelRunner::error, this,
          &MainWindow::onDependenciesError);
  connect(parallelQueryRunner_, &QueryDependenciesParallelRunner::progress,
          this, &MainWindow::onDependenciesProgress);
  if (logDock_) {
    connect(parallelQueryRunner_, &QueryDependenciesParallelRunner::logMessage,
            logDock_, &LogDock::enqueue);
  }

  // Make-ast runner signals
  connect(makeAstRunner_, &MakeAstRunner::astReady, this,
          &MainWindow::onAstReady);
  connect(makeAstRunner_, &MakeAstRunner::error, this, &MainWindow::onAstError);
  connect(makeAstRunner_, &MakeAstRunner::progress, this,
          &MainWindow::onAstProgress);
  connect(makeAstRunner_, &MakeAstRunner::logMessage, this,
          &MainWindow::onMakeAstLogMessage);
  if (logDock_) {
    connect(makeAstRunner_, &MakeAstRunner::logMessage, logDock_,
            &LogDock::enqueue);
  }

  // AST extractor signals (queued connections for cross-thread)
  connect(astExtractorRunner_, &AstExtractorRunner::finished, this,
          &MainWindow::onAstExtracted);
  connect(astExtractorRunner_, &AstExtractorRunner::error, this,
          &MainWindow::onAstError);
  connect(astExtractorRunner_, &AstExtractorRunner::progress, this,
          &MainWindow::onAstProgress);
  connect(astExtractorRunner_, &AstExtractorRunner::statsUpdated, this,
          &MainWindow::onAstStatsUpdated);
  connect(astExtractorRunner_, &AstExtractorRunner::started, this,
          &MainWindow::onAstProgress);
  if (logDock_) {
    connect(astExtractorRunner_, &AstExtractorRunner::logMessage, logDock_,
            &LogDock::enqueue, Qt::QueuedConnection);
  }
}

void MainWindow::loadCompilationDatabase(const QString &compilationDatabasePath,
                                         const QString &projectRoot) {
  MemoryProfiler::checkpoint("Before loading compilation database");

  // Normalize to absolute path to ensure it works regardless of current working
  // directory
  QFileInfo info(compilationDatabasePath);
  compilationDatabasePath_ = info.absoluteFilePath();
  const QString normalizedCompilationDatabasePath = compilationDatabasePath_;

  // Store user-specified project root (may be empty - model will compute from
  // source files) If model's computation fails, it will use
  // compilationDatabasePath's directory as fallback
  if (projectRoot.isEmpty()) {
    projectRoot_ = QString(); // Let model compute from source files
  } else {
    projectRoot_ = QFileInfo(projectRoot).absoluteFilePath();
  }

  // Get cache directory and output file path from config
  AppConfig &config = AppConfig::instance();
  QString outputFilePath =
      config.getDependenciesFilePath(normalizedCompilationDatabasePath);
  QString cacheDir =
      config.getCacheDirectory(normalizedCompilationDatabasePath);

  MemoryProfiler::checkpoint("After config setup");

  // Determine if we should use parallel processing
  std::string errorMsg;
  std::vector<std::string> sourceFiles =
      acav::getSourceFilesFromCompilationDatabase(
          normalizedCompilationDatabasePath.toStdString(), errorMsg);

  MemoryProfiler::checkpoint("After loading source files list");

  auto dedupSources = [](const std::vector<std::string> &paths) {
    std::vector<std::string> unique;
    unique.reserve(paths.size());
    std::unordered_set<std::string> seen;
    for (const std::string &path : paths) {
      if (seen.insert(path).second) {
        unique.push_back(path);
      }
    }
    return unique;
  };
  sourceFiles = dedupSources(sourceFiles);

  if (sourceFiles.empty()) {
    QMessageBox::critical(this, tr("Error"),
                          tr("Failed to load source files: %1")
                              .arg(QString::fromStdString(errorMsg)));
    return;
  }

  bool useParallel =
      (static_cast<int>(sourceFiles.size()) >= kParallelThreshold);

  if (useParallel) {
    logStatus(LogLevel::Info,
              QString("Starting parallel dependency analysis (%1 files)...")
                  .arg(sourceFiles.size()),
              QStringLiteral("query-dependencies"));
    parallelQueryRunner_->run(normalizedCompilationDatabasePath,
                              outputFilePath);
  } else {
    logStatus(LogLevel::Info,
              QString("Loading compilation database: %1\nCache directory: %2")
                  .arg(normalizedCompilationDatabasePath)
                  .arg(cacheDir),
              QStringLiteral("query-dependencies"));
    queryRunner_->run(normalizedCompilationDatabasePath, outputFilePath);
  }
}

void MainWindow::onOpenCompilationDatabase() {
  if (isAstExportInProgress_) {
    logStatus(LogLevel::Info, tr("AST export in progress, please wait..."));
    return;
  }

  OpenProjectDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  QString dbPath = dialog.compilationDatabasePath();
  QString projectRoot = dialog.projectRootPath();

  if (dbPath.isEmpty()) {
    return;
  }

  loadCompilationDatabase(dbPath, projectRoot);
}

void MainWindow::onExit() { close(); }

void MainWindow::onTranslationUnitClicked(const QModelIndex &index) {
  if (!index.isValid()) {
    return;
  }

  QString filePath = tuModel_->getSourceFilePathFromIndex(index);
  if (filePath.isEmpty()) {
    return;
  }

  if (filePath == sourceView_->currentFilePath()) {
    return;
  }

  logStatus(LogLevel::Info, QString("Loading file: %1").arg(filePath));

  if (sourceView_->loadFile(filePath)) {
    // Register file with FileManager and set FileID in SourceCodeView
    FileID fileId = fileManager_.registerFile(filePath.toStdString());
    sourceView_->setCurrentFileId(fileId);
    updateSourceSubtitle(filePath);
    // Don't call highlightTuFile here - user's click already set the selection
    logStatus(LogLevel::Info, QString("Loaded: %1").arg(filePath));

    // Clear AST Tree and Declaration Context to maintain UI consistency.
    // User should not see AST from file A while viewing source code from file
    // B.
    bool shouldClearAst = false;
    if (isSourceFile(filePath)) {
      // Clicking a different source file means a different TU
      shouldClearAst = (filePath != currentSourceFilePath_);
    } else if (astContext_) {
      // For header files, check if it's part of the current AST
      const auto &index = astContext_->getLocationIndex();
      shouldClearAst = !index.hasFile(fileId);
    }

    if (shouldClearAst) {
      astModel_->clear();
      declContextView_->clear();
      astHasCompilationErrors_ = false;
      setAstCompilationWarningVisible(false);
    }
  } else {
    sourceView_->setCurrentFileId(FileManager::InvalidFileID);
    logStatus(LogLevel::Error, QString("Failed to load: %1").arg(filePath));
    return;
  }
}

void MainWindow::onTranslationUnitSelected(const QModelIndex &index) {
  if (!index.isValid()) {
    return;
  }

  QString filePath = tuModel_->getSourceFilePathFromIndex(index);
  if (filePath.isEmpty()) {
    return;
  }

  MemoryProfiler::checkpoint("File double-clicked - before checks");

  // Check if this is already the current file with AST loaded
  if (filePath == currentSourceFilePath_ && astModel_->hasNodes()) {
    logStatus(LogLevel::Info,
              QString("AST already loaded for: %1").arg(filePath));
    return;
  }

  // Check if source file (not header)
  if (!isSourceFile(filePath)) {
    logStatus(LogLevel::Warning, tr("Cannot load AST for header files"));
    // Still load the source code for viewing
    if (sourceView_->loadFile(filePath)) {
      FileID fileId = fileManager_.registerFile(filePath.toStdString());
      sourceView_->setCurrentFileId(fileId);
      updateSourceSubtitle(filePath);
      highlightTuFile(fileId);
    }
    return;
  }

  // Check if AST extraction is already in progress
  if (isAstExtractionInProgress_) {
    // Simply show message and ignore the request
    QFileInfo currentInfo(pendingSourceFilePath_);
    logStatus(
        LogLevel::Info,
        QString("AST extraction already in progress for %1. Please wait...")
            .arg(currentInfo.fileName()));
    return;
  }

  if (isAstExportInProgress_) {
    logStatus(LogLevel::Info, tr("AST export in progress, please wait..."));
    return;
  }

  MemoryProfiler::checkpoint("Before clearing old AST");

  // Clear old AST and create new context for new TU
  astModel_->clear();
  astHasCompilationErrors_ = false;
  setAstCompilationWarningVisible(false);

  MemoryProfiler::checkpoint("After clearing old AST model");

  clearHistory();

  MemoryProfiler::checkpoint("After clearHistory()");

  // Safely clean up the old extractor before destroying context
  // The extractor lives on the worker thread, so we must handle it carefully
  if (astExtractorRunner_) {
    // Disconnect all signals to prevent callbacks during destruction
    astExtractorRunner_->disconnect();
    // Move back to main thread for safe deletion
    astExtractorRunner_->moveToThread(QThread::currentThread());
    delete astExtractorRunner_;
    astExtractorRunner_ = nullptr;
  }

  astContext_.reset(); // Destroys old context (cleans up old TU nodes)

  MemoryProfiler::checkpoint("After destroying old AST context");

  astContext_ = std::make_unique<AstContext>();
  ++astVersion_;

  MemoryProfiler::checkpoint("After creating new AST context");

  // Create new extractor with new context
  astExtractorRunner_ = new AstExtractorRunner(astContext_.get(), fileManager_);
  astExtractorRunner_->setCommentExtractionEnabled(
      AppConfig::instance().getCommentExtractionEnabled());
  astExtractorRunner_->moveToThread(astWorkerThread_);

  // Reconnect signals
  connect(astExtractorRunner_, &AstExtractorRunner::finished, this,
          &MainWindow::onAstExtracted);
  connect(astExtractorRunner_, &AstExtractorRunner::error, this,
          &MainWindow::onAstError);
  connect(astExtractorRunner_, &AstExtractorRunner::progress, this,
          &MainWindow::onAstProgress);
  connect(astExtractorRunner_, &AstExtractorRunner::statsUpdated, this,
          &MainWindow::onAstStatsUpdated);
  connect(astExtractorRunner_, &AstExtractorRunner::started, this,
          &MainWindow::onAstProgress);

  logStatus(LogLevel::Info, QString("Loading file: %1").arg(filePath));

  if (sourceView_->loadFile(filePath)) {
    // Register file with FileManager and set FileID in SourceCodeView
    FileID fileId = fileManager_.registerFile(filePath.toStdString());
    sourceView_->setCurrentFileId(fileId);
    updateSourceSubtitle(filePath);
    logStatus(LogLevel::Info, QString("Loaded: %1").arg(filePath));
  } else {
    sourceView_->setCurrentFileId(FileManager::InvalidFileID);
    logStatus(LogLevel::Error, QString("Failed to load: %1").arg(filePath));
    return;
  }

  MemoryProfiler::checkpoint("After loading source file to view");

  // Get .ast cache file path
  AppConfig &config = AppConfig::instance();
  QString astFilePath =
      config.getAstFilePath(compilationDatabasePath_, filePath);

  QFileInfo astFileInfo(astFilePath);

  if (!astFileInfo.exists()) {
    // Generate .ast file using make-ast
    currentSourceFilePath_ = filePath;
    pendingSourceFilePath_ = filePath;
    isAstExtractionInProgress_ = true;
    astHasCompilationErrors_ = false;
    QFileInfo sourceInfo(filePath);
    logStatus(LogLevel::Info,
              "Generating AST for " + sourceInfo.fileName() + "...");
    onTimingMessage(QString("AST input files: %1 (source + headers)")
                        .arg(getFileListForSource(filePath).size()));
    MemoryProfiler::checkpoint("Before make-ast generation");
    makeAstRunner_->run(compilationDatabasePath_, filePath, astFilePath);
  } else {
    currentSourceFilePath_ = filePath;
    pendingSourceFilePath_ = filePath;
    isAstExtractionInProgress_ = true;
    astHasCompilationErrors_ = loadAstCompilationErrorState(astFilePath);
    onTimingMessage(QString("AST input files: %1 (source + headers)")
                        .arg(getFileListForSource(filePath).size()));
    MemoryProfiler::checkpoint("Before AST extraction from cache");
    // .ast exists, extract directly
    extractAst(astFilePath, filePath);
  }
}

void MainWindow::onDependenciesReady(const QJsonObject &dependencies) {
  tuModel_->populateFromDependencies(dependencies, projectRoot_,
                                     compilationDatabasePath_);
  expandFileExplorerTopLevel();

  // Get file count from statistics
  QJsonObject stats = dependencies["statistics"].toObject();
  int fileCount = stats["successCount"].toInt();
  int totalHeaders = stats["totalHeaderCount"].toInt();

  logStatus(LogLevel::Info,
            QString("Loaded %1 translation units").arg(fileCount),
            QStringLiteral("query-dependencies"));
  onTimingMessage(QString("Dependencies summary: %1 sources, %2 headers")
                      .arg(fileCount)
                      .arg(totalHeaders));
}

void MainWindow::onDependenciesError(const QString &errorMessage) {
  QMessageBox::critical(this, tr("Error"), errorMessage);
  logStatus(LogLevel::Error, tr("Error loading dependencies"),
            QStringLiteral("query-dependencies"));
}

void MainWindow::onDependenciesProgress(const QString &message) {
  logStatus(LogLevel::Info, message, QStringLiteral("query-dependencies"));
}

void MainWindow::onAstProgress(const QString &message) {
  logStatus(LogLevel::Info, message, QStringLiteral("ast-extractor"));
}

void MainWindow::onAstStatsUpdated(const AstExtractionStats &stats) {
  logStatus(LogLevel::Info,
            QString("Comments found: %1").arg(stats.commentCount),
            QStringLiteral("ast-extractor"));
}

void MainWindow::onDependenciesReadyWithErrors(
    const QJsonObject &dependencies, const QStringList &errorMessages) {
  // Load successful dependencies normally
  tuModel_->populateFromDependencies(dependencies, projectRoot_,
                                     compilationDatabasePath_);
  expandFileExplorerTopLevel();

  QJsonObject stats = dependencies["statistics"].toObject();
  int successCount = stats["successCount"].toInt();
  int failureCount = stats["failureCount"].toInt();
  int totalHeaders = stats["totalHeaderCount"].toInt();

  logStatus(LogLevel::Warning,
            QString("Loaded %1 translation units (%2 failed)")
                .arg(successCount)
                .arg(failureCount),
            QStringLiteral("query-dependencies"));
  for (const QString &errorMessage : errorMessages) {
    logStatus(LogLevel::Error, errorMessage,
              QStringLiteral("query-dependencies"));
  }
  onTimingMessage(
      QString("Dependencies summary: %1 sources loaded, %2 failed, %3 headers")
          .arg(successCount)
          .arg(failureCount)
          .arg(totalHeaders));
}

void MainWindow::logStatus(LogLevel level, const QString &message,
                           const QString &source) {
  const QString trimmed = message.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }

  LogEntry entry;
  entry.level = level;
  entry.source = source.isEmpty() ? QStringLiteral("acav") : source;
  entry.message = trimmed;
  entry.timestamp = QDateTime::currentDateTime();

  if (logDock_) {
    QMetaObject::invokeMethod(logDock_, "enqueue", Qt::QueuedConnection,
                              Q_ARG(LogEntry, entry));
  }
}

void MainWindow::onAstReady(const QString &astFilePath) {
  MemoryProfiler::checkpoint("After make-ast generation complete");
  persistAstCompilationErrorState(astFilePath, astHasCompilationErrors_);
  // make-ast finished, now extract
  extractAst(astFilePath, currentSourceFilePath_);
}

void MainWindow::onAstExtracted(AstViewNode *root) {
  MemoryProfiler::checkpoint("AST extraction complete - before rendering");

  // Clear stale AST search state
  clearAstSearchState();
  if (astSearchInput_) {
    astSearchInput_->clear();
  }

  // Clear extraction in progress flag
  isAstExtractionInProgress_ = false;

  // Reload source view to ensure synchronization with AST
  // Handles case where user clicked a different file during processing
  if (!currentSourceFilePath_.isEmpty()) {
    // Check if source view is showing a different file
    if (sourceView_->currentFilePath() != currentSourceFilePath_) {
      // Load the file that matches the extracted AST
      if (sourceView_->loadFile(currentSourceFilePath_)) {
        // Register file with FileManager and set FileID
        FileID fileId =
            fileManager_.registerFile(currentSourceFilePath_.toStdString());
        sourceView_->setCurrentFileId(fileId);
        updateSourceSubtitle(currentSourceFilePath_);
      } else {
        // File load failed (deleted, moved, permission denied, etc.)
        sourceView_->setCurrentFileId(FileManager::InvalidFileID);
        logStatus(LogLevel::Warning, QString("Failed to reload source file: %1")
                                         .arg(currentSourceFilePath_));
        // Continue with AST rendering - AST is still useful without source view
      }
    }

    // Highlight the current file in the Translation Unit tree view
    // This provides visual feedback showing which file is currently active
    // Use FileID for consistent file identification throughout the application
    FileID currentFileId = sourceView_->currentFileId();
    if (currentFileId != FileManager::InvalidFileID) {
      QModelIndex tuIndex = tuModel_->findIndexByFileId(currentFileId);
      if (tuIndex.isValid()) {
        tuView_->setCurrentIndex(tuIndex);
        tuView_->scrollTo(tuIndex);
      }
    }
  }

  auto renderStart = std::chrono::steady_clock::now();

  MemoryProfiler::checkpoint("Before setting root node to model");

  std::size_t nodeCount = 0;
  if (astContext_) {
    nodeCount = astContext_->getAstViewNodeCount();
    astModel_->setTotalNodeCount(nodeCount);
  }

  // Update model (on main thread via queued signal)
  astModel_->setRootNode(root); // Takes ownership
  updateAstSubtitle(currentSourceFilePath_);

  MemoryProfiler::checkpoint("After setting root node to model");

  auto renderEnd = std::chrono::steady_clock::now();
  std::chrono::duration<double> renderElapsed = renderEnd - renderStart;
  onTimingMessage(QString("render AST: %1s")
                      .arg(QString::number(renderElapsed.count(), 'f', 2)));
  onTimingMessage(QString("AST nodes loaded: %1").arg(nodeCount));

  MemoryProfiler::checkpoint(
      QString("AST rendering complete (%1 nodes)").arg(nodeCount));

  const bool showCompilationWarning = astHasCompilationErrors_;
  setAstCompilationWarningVisible(showCompilationWarning);
  astHasCompilationErrors_ = false;

  logStatus(LogLevel::Info, tr("AST loaded (%1 nodes)").arg(nodeCount));
}

void MainWindow::onAstError(const QString &errorMessage) {
  // Clear extraction in progress flag
  isAstExtractionInProgress_ = false;
  astHasCompilationErrors_ = false;
  setAstCompilationWarningVisible(false);

  QString targetAstPath = lastAstFilePath_;
  if (targetAstPath.isEmpty() && !compilationDatabasePath_.isEmpty() &&
      !currentSourceFilePath_.isEmpty()) {
    targetAstPath = AppConfig::instance().getAstFilePath(
        compilationDatabasePath_, currentSourceFilePath_);
    lastAstFilePath_ = targetAstPath;
  }

  if (targetAstPath.isEmpty() || currentSourceFilePath_.isEmpty()) {
    logStatus(LogLevel::Error, "Error: " + errorMessage);
    QMessageBox::critical(this, "AST Error",
                          "Failed to generate or load AST:\n\n" + errorMessage);
    return;
  }

  const bool cacheLoadFailed =
      errorMessage.contains("Failed to load AST from file",
                            Qt::CaseInsensitive) ||
      errorMessage.contains("Failed to load AST", Qt::CaseInsensitive);
  if (!cacheLoadFailed) {
    logStatus(LogLevel::Error, "Error: " + errorMessage);
    QMessageBox::critical(this, "AST Error",
                          "Failed to generate or load AST:\n\n" + errorMessage);
    return;
  }

  logStatus(LogLevel::Warning,
            tr("Cached AST load failed; regenerating automatically."));
  logStatus(LogLevel::Info, errorMessage);

  if (!deleteCachedAst(targetAstPath)) {
    logStatus(LogLevel::Error,
              tr("Failed to delete cached AST file: %1").arg(targetAstPath));
    QMessageBox::critical(this, "AST Error",
                          "Failed to delete cached AST file:\n\n" +
                              targetAstPath);
    return;
  }

  logStatus(LogLevel::Warning, tr("Regenerating AST after load failure..."));
  astHasCompilationErrors_ = false;
  makeAstRunner_->run(compilationDatabasePath_, currentSourceFilePath_,
                      targetAstPath);
}

void MainWindow::onMakeAstLogMessage(const LogEntry &entry) {
  if (entry.source == QStringLiteral("make-ast") &&
      entry.level == LogLevel::Error) {
    astHasCompilationErrors_ = true;
  }
}

void MainWindow::onAstContextMenuRequested(const QPoint &pos) {
  QModelIndex index = astView_->indexAt(pos);
  if (!index.isValid()) {
    return;
  }

  auto *node = static_cast<AstViewNode *>(index.internalPointer());
  if (!node) {
    return;
  }

  // Root node is the translation unit
  const bool isTranslationUnit = (node->getParent() == nullptr);

  ::QMenu menu(astView_);

  SourceRange macroRange(SourceLocation(FileManager::InvalidFileID, 0, 0),
                         SourceLocation(FileManager::InvalidFileID, 0, 0));
  const bool hasMacroRange = getMacroSpellingRange(node, &macroRange);
  if (hasMacroRange) {
    QAction *macroAction = menu.addAction(tr("Go to Macro Definition"));
    connect(macroAction, &QAction::triggered, this, [this, node, macroRange]() {
      navigateToRange(macroRange, node, false);
    });
    menu.addSeparator();
  }

  // Expand/Collapse actions
  QAction *expandAllAction = menu.addAction(tr("Expand All"));
  QAction *collapseAllAction = menu.addAction(tr("Collapse All"));
  connect(expandAllAction, &QAction::triggered, this,
          &MainWindow::onExpandAllAstChildren);
  connect(collapseAllAction, &QAction::triggered, this,
          &MainWindow::onCollapseAllAstChildren);

  // View Details action
  QAction *viewDetailsAction = menu.addAction(tr("View Details..."));
  viewDetailsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
  connect(viewDetailsAction, &QAction::triggered, this,
          [this, node]() { onViewNodeDetails(node); });

  // Export action - disabled for translation unit (too large)
  menu.addSeparator();
  QAction *exportAction = menu.addAction(tr("Export Subtree to JSON..."));
  exportAction->setEnabled(!isTranslationUnit && astModel_ &&
                           astModel_->hasNodes());
  connect(exportAction, &QAction::triggered, this,
          [this, node]() { onExportAst(node); });

  menu.exec(astView_->viewport()->mapToGlobal(pos));
}

void MainWindow::onViewNodeDetails(AstViewNode *node) {
  if (!node) {
    return;
  }

  const AcavJson &props = node->getProperties();
  QString kind = tr("Node");
  QString name;

  if (props.contains("kind") && props.at("kind").is_string()) {
    kind = QString::fromStdString(props.at("kind").get<InternedString>().str());
  }
  if (props.contains("name") && props.at("name").is_string()) {
    name = QString::fromStdString(props.at("name").get<InternedString>().str());
  }

  QString title = tr("Node Details - %1").arg(kind);
  if (!name.isEmpty()) {
    title += QStringLiteral(" '%1'").arg(name);
  }

  AcavJson propertiesCopy = props;

  // Add source range information with file paths
  const SourceRange &range = node->getSourceRange();
  AcavJson sourceRangeJson = AcavJson::object();

  auto locationToJson = [this](const SourceLocation &loc) {
    AcavJson obj = AcavJson::object();
    obj["fileId"] = static_cast<uint64_t>(loc.fileID());
    obj["line"] = static_cast<uint64_t>(loc.line());
    obj["column"] = static_cast<uint64_t>(loc.column());
    std::string_view filePath = fileManager_.getFilePath(loc.fileID());
    if (!filePath.empty()) {
      obj["filePath"] = InternedString(std::string(filePath));
    }
    return obj;
  };

  sourceRangeJson["begin"] = locationToJson(range.begin());
  sourceRangeJson["end"] = locationToJson(range.end());
  propertiesCopy["sourceRange"] = std::move(sourceRangeJson);

  auto *dialog = new NodeDetailsDialog(std::move(propertiesCopy), title, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void MainWindow::onExportAst(AstViewNode *node) {
  if (!node) {
    return;
  }
  if (isAstExtractionInProgress_) {
    logStatus(LogLevel::Info, tr("AST extraction in progress, please wait..."));
    return;
  }
  if (isAstExportInProgress_) {
    logStatus(LogLevel::Info, tr("AST export already in progress..."));
    return;
  }

  QString defaultDir;
  if (!currentSourceFilePath_.isEmpty()) {
    QFileInfo info(currentSourceFilePath_);
    defaultDir = info.absolutePath();
  } else {
    defaultDir = QDir::homePath();
  }

  QString suggested =
      defaultDir + QDir::separator() + buildDefaultExportFileName(node);

  QString targetPath =
      QFileDialog::getSaveFileName(this, tr("Export AST Subtree"), suggested,
                                   tr("JSON Files (*.json);;All Files (*)"));
  if (targetPath.isEmpty()) {
    return;
  }

  auto confirm = QMessageBox::question(
      this, tr("Export AST"),
      tr("Exporting this subtree can take some time.\n\nDo you want to "
         "continue?"),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (confirm != QMessageBox::Yes) {
    return;
  }

  auto *progress = new QProgressDialog(this);
  progress->setAttribute(Qt::WA_DeleteOnClose);
  progress->setWindowTitle(tr("Exporting AST"));
  progress->setLabelText(tr("Exporting AST subtree to JSON in the background.\n"
                            "You can close this window and continue working.\n"
                            "You will be notified when the export finishes."));
  progress->setCancelButtonText(tr("Close"));
  progress->setRange(0, 0); // Busy indicator
  progress->setWindowModality(Qt::NonModal);
  progress->show();
  connect(progress, &QProgressDialog::canceled, progress,
          &QProgressDialog::close);

  isAstExportInProgress_ = true;
  QPointer<MainWindow> self(this);
  QPointer<QProgressDialog> progressPtr(progress);
  FileManager *fileManager = &fileManager_;
  AstViewNode *exportRoot = node;
  QString exportPath = targetPath;

  auto *thread = new QThread(this);
  astExportThread_ = thread;
  auto *worker = new QObject();
  worker->moveToThread(thread);

  connect(
      thread, &QThread::started, worker,
      [self, progressPtr, exportRoot, exportPath, fileManager, thread]() {
        QString errorMessage;
        try {
          AcavJson json = buildAstJsonTree(exportRoot, *fileManager);
          InternedString serialized = json.dump(2);
          const std::string &data = serialized.str();

          QFile outFile(exportPath);
          if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            errorMessage = QObject::tr("Unable to open file for writing:\n%1")
                               .arg(exportPath);
          } else if (outFile.write(data.c_str(),
                                   static_cast<qsizetype>(data.size())) == -1) {
            errorMessage = QObject::tr("Failed to write data to file:\n%1")
                               .arg(exportPath);
          } else {
            outFile.close();
          }
        } catch (const std::exception &ex) {
          errorMessage = QObject::tr("Failed to serialize AST subtree:\n%1")
                             .arg(ex.what());
        }

        if (self) {
          QMetaObject::invokeMethod(
              self,
              [self, progressPtr, exportPath, errorMessage]() {
                if (!self) {
                  return;
                }
                self->isAstExportInProgress_ = false;
                if (progressPtr) {
                  progressPtr->close();
                }
                auto *notice = new QMessageBox(self);
                notice->setAttribute(Qt::WA_DeleteOnClose);
                notice->setStandardButtons(QMessageBox::Ok);
                notice->setWindowModality(Qt::NonModal);
                if (!errorMessage.isEmpty()) {
                  notice->setIcon(QMessageBox::Warning);
                  notice->setWindowTitle(QObject::tr("Export Failed"));
                  notice->setText(errorMessage);
                } else {
                  notice->setIcon(QMessageBox::Information);
                  notice->setWindowTitle(QObject::tr("Export Complete"));
                  notice->setText(QObject::tr("Exported AST subtree to:\n%1")
                                      .arg(exportPath));
                  self->logStatus(LogLevel::Info,
                                  QObject::tr("Exported AST subtree to %1")
                                      .arg(exportPath));
                }
                notice->show();
              },
              Qt::QueuedConnection);
        }

        thread->quit();
      });

  connect(thread, &QThread::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  connect(thread, &QThread::finished, this,
          [this]() { astExportThread_ = nullptr; });
  thread->start();
}

QString MainWindow::buildDefaultExportFileName(AstViewNode *node) const {
  if (!node) {
    return QStringLiteral("ast_subtree.json");
  }

  const AcavJson &props = node->getProperties();
  auto getStr = [&](const char *key) -> QString {
    if (props.contains(key) && props.at(key).is_string()) {
      return QString::fromStdString(props.at(key).get<InternedString>().str());
    }
    return {};
  };

  QString kind = getStr("kind");

  // Try multiple property names for the node's name
  QString name;
  for (const char *key : {"name", "declName", "memberName"}) {
    name = getStr(key);
    if (!name.isEmpty()) {
      break;
    }
  }

  QString base = name.isEmpty() ? kind : QString("%1_%2").arg(kind, name);
  if (base.isEmpty()) {
    base = QStringLiteral("ast_subtree");
  }

  // Sanitize filename: keep alphanumeric, underscore, hyphen
  QString sanitized;
  sanitized.reserve(base.size());
  for (QChar ch : base) {
    if (ch.isLetterOrNumber() || ch == '_' || ch == '-') {
      sanitized.append(ch);
    } else if (!sanitized.endsWith('_')) {
      sanitized.append('_');
    }
  }

  if (sanitized.isEmpty()) {
    return QStringLiteral("ast_subtree.json");
  }

  return sanitized + ".json";
}

void MainWindow::extractAst(const QString &astFilePath,
                            const QString &sourceFilePath) {
  MemoryProfiler::checkpoint("Starting extractAst - queuing to worker");

  // Get file list from TU model
  QStringList fileList = getFileListForSource(sourceFilePath);
  lastAstFilePath_ = astFilePath;
  currentSourceFilePath_ = sourceFilePath;

  // Extract on worker thread
  // Pass compilation database path for C++20 module support
  QString compDbPath = compilationDatabasePath_;
  QMetaObject::invokeMethod(
      astExtractorRunner_,
      [this, astFilePath, fileList, compDbPath]() {
        astExtractorRunner_->run(astFilePath, fileList, compDbPath);
      },
      Qt::QueuedConnection);
}

QStringList
MainWindow::getFileListForSource(const QString &sourceFilePath) const {
  QStringList fileList;
  fileList.append(sourceFilePath); // Source file itself

  // Get all included headers from TU model
  QStringList headers = tuModel_->getIncludedHeadersForSource(sourceFilePath);
  fileList.append(headers);

  return fileList;
}

bool MainWindow::isSourceFile(const QString &filePath) const {
  static const QStringList sourceExtensions = {// C/C++
                                               ".cpp", ".cc", ".cxx", ".c",
                                               // Objective-C/C++
                                               ".m", ".mm"};
  for (const QString &ext : sourceExtensions) {
    if (filePath.endsWith(ext, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

bool MainWindow::validateSourceLookup(FileID fileId) {
  if (isAstExtractionInProgress_) {
    QFileInfo currentInfo(pendingSourceFilePath_);
    logStatus(LogLevel::Info,
              tr("AST extraction in progress for %1. Please wait...")
                  .arg(currentInfo.fileName()));
    return false;
  }
  if (!astModel_ || !astModel_->hasNodes()) {
    logStatus(LogLevel::Info,
              tr("No AST available (code not yet compiled). Double-click the "
                 "file in File Explorer to generate an AST."));
    return false;
  }
  if (!currentSourceFilePath_.isEmpty() && sourceView_ &&
      isSourceFile(sourceView_->currentFilePath())) {
    FileID currentAstFileId = FileManager::InvalidFileID;
    if (auto existing =
            fileManager_.tryGetFileId(currentSourceFilePath_.toStdString())) {
      currentAstFileId = *existing;
    }
    if (currentAstFileId != FileManager::InvalidFileID &&
        currentAstFileId != fileId) {
      logStatus(LogLevel::Info,
                tr("No AST available for this file yet. Double-click it in "
                   "File Explorer to generate an AST."));
      return false;
    }
  }
  return true;
}

void MainWindow::logNoNodeFound(FileID fileId, const QString &fallbackMessage) {
  const auto &index = astContext_->getLocationIndex();
  if (!index.hasFile(fileId)) {
    logStatus(LogLevel::Info,
              tr("No AST data for this file in the current translation unit. "
                 "If this is a header, it may not be included. Double-click a "
                 "source file in File Explorer to load its AST."));
  } else {
    logStatus(LogLevel::Info, fallbackMessage);
  }
}

bool MainWindow::deleteCachedAst(const QString &astFilePath) {
  QFile astFile(astFilePath);
  if (astFile.exists() && !astFile.remove()) {
    return false;
  }

  const QString statusPath = astCacheStatusFilePath(astFilePath);
  QFile statusFile(statusPath);
  if (statusFile.exists() && !statusFile.remove()) {
    logStatus(LogLevel::Warning,
              tr("Failed to delete AST cache status file: %1").arg(statusPath));
  }
  return true;
}

QString MainWindow::astCacheStatusFilePath(const QString &astFilePath) const {
  return astFilePath + ".status";
}

void MainWindow::persistAstCompilationErrorState(const QString &astFilePath,
                                                 bool hasCompilationErrors) {
  if (astFilePath.isEmpty()) {
    return;
  }

  const QString statusPath = astCacheStatusFilePath(astFilePath);
  QFile statusFile(statusPath);
  if (!statusFile.open(QIODevice::WriteOnly | QIODevice::Truncate |
                       QIODevice::Text)) {
    logStatus(LogLevel::Warning,
              tr("Failed to write AST cache status file: %1").arg(statusPath));
    return;
  }

  const QByteArray payload = hasCompilationErrors ? QByteArrayLiteral("1\n")
                                                  : QByteArrayLiteral("0\n");
  if (statusFile.write(payload) != payload.size()) {
    logStatus(LogLevel::Warning,
              tr("Failed to persist AST cache status: %1").arg(statusPath));
  }
}

bool MainWindow::loadAstCompilationErrorState(const QString &astFilePath) const {
  if (astFilePath.isEmpty()) {
    return false;
  }

  QFile statusFile(astCacheStatusFilePath(astFilePath));
  if (!statusFile.exists() ||
      !statusFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }

  const QByteArray value = statusFile.readAll().trimmed().toLower();
  return value == "1" || value == "true";
}

void MainWindow::clearHistory() {
  history_.clear();
  historyCursor_ = 0;
  updateNavActions();
}

void MainWindow::recordHistory(FileID fileId, unsigned line, unsigned column,
                               AstViewNode *node) {
  if (suppressHistory_ || fileId == FileManager::InvalidFileID) {
    return;
  }

  NavEntry entry{fileId, line, column, node, astVersion_};
  if (!history_.empty()) {
    const NavEntry &current = history_[historyCursor_];
    if (current.fileId == entry.fileId && current.line == entry.line &&
        current.column == entry.column && current.node == entry.node &&
        current.astVersion == entry.astVersion) {
      return; // No-op
    }
  }

  // Drop any forward history
  if (historyCursor_ + 1 < history_.size()) {
    history_.erase(history_.begin() + static_cast<long>(historyCursor_) + 1,
                   history_.end());
  }

  history_.push_back(entry);
  historyCursor_ = history_.size() - 1;

  static constexpr std::size_t kMaxHistory = 500;
  if (history_.size() > kMaxHistory) {
    std::size_t toRemove = history_.size() - kMaxHistory;
    history_.erase(history_.begin(),
                   history_.begin() + static_cast<long>(toRemove));
    historyCursor_ = history_.size() - 1;
  }

  updateNavActions();
}

void MainWindow::navigateHistory(int delta) {
  if (history_.empty()) {
    return;
  }
  int newIndex = static_cast<int>(historyCursor_) + delta;
  if (newIndex < 0 || newIndex >= static_cast<int>(history_.size())) {
    return;
  }
  historyCursor_ = static_cast<std::size_t>(newIndex);
  applyEntry(history_[historyCursor_]);
  updateNavActions();
}

void MainWindow::applyEntry(const NavEntry &entry) {
  if (entry.fileId == FileManager::InvalidFileID) {
    return;
  }

  std::string_view path = fileManager_.getFilePath(entry.fileId);
  if (path.empty()) {
    logStatus(LogLevel::Warning, tr("History target file is unavailable"));
    return;
  }

  suppressHistory_ = true;
  QString qPath = QString::fromStdString(std::string(path));

  if (sourceView_->currentFileId() != entry.fileId) {
    if (!sourceView_->loadFile(qPath)) {
      logStatus(LogLevel::Error, QString("Failed to load %1").arg(qPath));
      suppressHistory_ = false;
      return;
    }
    sourceView_->setCurrentFileId(entry.fileId);
    updateSourceSubtitle(qPath);
  }

  highlightTuFile(entry.fileId);

  SourceLocation loc(entry.fileId, entry.line, entry.column);
  SourceRange range(loc, loc);
  sourceView_->highlightRange(range);

  if (entry.astVersion == astVersion_ && entry.node) {
    QModelIndex modelIndex = astModel_->selectNode(entry.node);
    if (modelIndex.isValid()) {
      astView_->selectionModel()->setCurrentIndex(
          modelIndex, QItemSelectionModel::ClearAndSelect);
      astView_->scrollTo(modelIndex);
    }
  }

  suppressHistory_ = false;
}

void MainWindow::updateNavActions() {
  bool hasHistory = !history_.empty();
  if (navBackAction_) {
    navBackAction_->setEnabled(hasHistory && historyCursor_ > 0);
    if (hasHistory && historyCursor_ > 0) {
      const NavEntry &target = history_[historyCursor_ - 1];
      std::string_view path = fileManager_.getFilePath(target.fileId);
      navBackAction_->setToolTip(
          QString("Back to %1:%2")
              .arg(QString::fromStdString(std::string(path)))
              .arg(target.line));
    } else {
      navBackAction_->setToolTip(tr("Back"));
    }
  }
  if (navForwardAction_) {
    navForwardAction_->setEnabled(hasHistory &&
                                  (historyCursor_ + 1 < history_.size()));
    if (hasHistory && historyCursor_ + 1 < history_.size()) {
      const NavEntry &target = history_[historyCursor_ + 1];
      std::string_view path = fileManager_.getFilePath(target.fileId);
      navForwardAction_->setToolTip(
          QString("Forward to %1:%2")
              .arg(QString::fromStdString(std::string(path)))
              .arg(target.line));
    } else {
      navForwardAction_->setToolTip(tr("Forward"));
    }
  }
}

void MainWindow::onTimingMessage(const QString &message) {
  QString trimmed = message.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  // Skip legacy timing lines
  if (trimmed.startsWith(QStringLiteral("Timing "))) {
    return;
  }
  logStatus(LogLevel::Debug, trimmed, QStringLiteral("acav-timing"));
}

// Navigation implementation

void MainWindow::onAstNodeSelected(const QModelIndex &index) {
  if (!index.isValid() || !astContext_) {
    return;
  }

  AstViewNode *node = static_cast<AstViewNode *>(index.internalPointer());
  if (!node) {
    return;
  }

  if (suppressSourceHighlight_) {
    suppressSourceHighlight_ = false;
    astModel_->updateSelectionFromIndex(index);
    return;
  }

  // Update model's selected node to keep selection state in sync
  astModel_->updateSelectionFromIndex(index);

  const SourceRange &range = node->getSourceRange();
  if (goToMacroDefinitionAction_) {
    SourceRange macroRange(SourceLocation(FileManager::InvalidFileID, 0, 0),
                           SourceLocation(FileManager::InvalidFileID, 0, 0));
    bool hasMacroRange = getMacroSpellingRange(node, &macroRange);
    goToMacroDefinitionAction_->setEnabled(hasMacroRange);
  }
  bool skipCursorMove = suppressSourceCursorMove_;
  suppressSourceCursorMove_ = false;
  navigateToRange(range, node, skipCursorMove);
}

bool MainWindow::getMacroSpellingRange(const AstViewNode *node,
                                       SourceRange *outRange) const {
  if (!node || !outRange) {
    return false;
  }
  const auto &properties = node->getProperties();
  auto it = properties.find("macroSpellingRange");
  if (it == properties.end()) {
    return false;
  }
  return parseSourceRangeJson(*it, outRange);
}

bool MainWindow::navigateToRange(const SourceRange &range, AstViewNode *node,
                                 bool skipCursorMove) {
  FileID rangeFileId = range.begin().fileID();

  // Check for invalid location
  if (rangeFileId == FileManager::InvalidFileID) {
    // Node has no source location (e.g., TranslationUnitDecl)
    // Still highlight the main source file in file explorer for consistency
    if (!currentSourceFilePath_.isEmpty()) {
      if (auto fileId =
              fileManager_.tryGetFileId(currentSourceFilePath_.toStdString())) {
        highlightTuFile(*fileId);
      }
    }
    // Clear source highlight since there's no specific range
    sourceView_->clearHighlight();
    return false;
  }

  // Check if range is in a different file
  if (rangeFileId != sourceView_->currentFileId()) {
    // Get file path from FileManager
    std::string_view filePath = fileManager_.getFilePath(rangeFileId);
    if (filePath.empty()) {
      logStatus(LogLevel::Warning, tr("Cannot find file path for AST node"));
      return false;
    }

    // Load the file
    QString qFilePath = QString::fromStdString(std::string(filePath));
    if (!sourceView_->loadFile(qFilePath)) {
      logStatus(LogLevel::Error,
                QString("Failed to load file: %1").arg(qFilePath));
      return false;
    }

    // Set the new file ID
    sourceView_->setCurrentFileId(rangeFileId);
    updateSourceSubtitle(qFilePath);
  }

  // Highlight range in source view
  sourceView_->highlightRange(range, !skipCursorMove);
  highlightTuFile(rangeFileId);

  if (node) {
    recordHistory(rangeFileId, range.begin().line(), range.begin().column(),
                  node);
  }

  return true;
}

void MainWindow::onGoToMacroDefinition() {
  if (!astContext_ || !astView_) {
    return;
  }
  QModelIndex index = astView_->currentIndex();
  if (!index.isValid()) {
    return;
  }
  AstViewNode *node = static_cast<AstViewNode *>(index.internalPointer());
  if (!node) {
    return;
  }
  SourceRange macroRange(SourceLocation(FileManager::InvalidFileID, 0, 0),
                         SourceLocation(FileManager::InvalidFileID, 0, 0));
  if (!getMacroSpellingRange(node, &macroRange)) {
    logStatus(LogLevel::Info, tr("No macro definition for this AST node"));
    return;
  }
  navigateToRange(macroRange, node, false);
}

void MainWindow::onSourcePositionClicked(FileID fileId, unsigned line,
                                         unsigned column) {
  if (!astContext_ || fileId == FileManager::InvalidFileID) {
    return;
  }
  if (!validateSourceLookup(fileId)) {
    return;
  }

  const auto &index = astContext_->getLocationIndex();
  auto matches = index.getNodesAt(fileId, line, column);

  if (matches.empty()) {
    logNoNodeFound(fileId, tr("No AST node at this position"));
    return;
  }

  // Filter to keep only the most specific nodes (smallest source ranges)
  // Step 1: Find the minimum range size among all matches
  struct RangeSize {
    unsigned lines;
    unsigned columns;
  };

  auto getRangeSize = [](const SourceRange &range) -> RangeSize {
    unsigned lines = range.end().line() - range.begin().line();
    unsigned columns = 0;
    if (lines == 0) {
      // Same line - just column difference
      columns = range.end().column() - range.begin().column();
    } else {
      // Multi-line - use line count as primary measure
      columns = 0;
    }
    return {lines, columns};
  };

  auto compareRangeSize = [](const RangeSize &a, const RangeSize &b) -> int {
    if (a.lines != b.lines)
      return a.lines < b.lines ? -1 : 1;
    if (a.columns != b.columns)
      return a.columns < b.columns ? -1 : 1;
    return 0; // Equal
  };

  // Find the minimum range size
  RangeSize minSize = getRangeSize(matches[0]->getSourceRange());
  for (auto *node : matches) {
    RangeSize size = getRangeSize(node->getSourceRange());
    if (compareRangeSize(size, minSize) < 0) {
      minSize = size;
    }
  }

  // Step 2: Keep only nodes with the minimum range size
  std::vector<AstViewNode *> mostSpecific;
  for (auto *node : matches) {
    RangeSize size = getRangeSize(node->getSourceRange());
    if (compareRangeSize(size, minSize) == 0) {
      mostSpecific.push_back(node);
    }
  }

  // Pick the first (deepest) match from most-specific nodes
  AstViewNode *selected = mostSpecific.front();

  QModelIndex modelIndex = astModel_->selectNode(selected);
  if (modelIndex.isValid()) {
    suppressSourceCursorMove_ = true;
    astView_->selectionModel()->setCurrentIndex(
        modelIndex, QItemSelectionModel::ClearAndSelect);
    astView_->scrollTo(modelIndex);
    suppressSourceCursorMove_ = false;
    const SourceRange &range = selected->getSourceRange();
    recordHistory(range.begin().fileID(), range.begin().line(),
                  range.begin().column(), selected);
  }
}

void MainWindow::onSourceRangeSelected(FileID fileId, unsigned startLine,
                                       unsigned startColumn, unsigned endLine,
                                       unsigned endColumn) {
  if (!astContext_ || fileId == FileManager::InvalidFileID) {
    return;
  }
  if (!validateSourceLookup(fileId)) {
    return;
  }

  const auto &index = astContext_->getLocationIndex();
  AstViewNode *match = index.getFirstNodeContainedInRange(
      fileId, startLine, startColumn, endLine, endColumn);

  if (!match) {
    logNoNodeFound(fileId, tr("No AST node within selection"));
    return;
  }

  QModelIndex modelIndex = astModel_->selectNode(match);
  if (modelIndex.isValid()) {
    suppressSourceHighlight_ = true;
    astView_->selectionModel()->setCurrentIndex(
        modelIndex, QItemSelectionModel::ClearAndSelect);
    astView_->scrollTo(modelIndex);
    const SourceRange &range = match->getSourceRange();
    recordHistory(range.begin().fileID(), range.begin().line(),
                  range.begin().column(), match);
  }
}

void MainWindow::highlightTuFile(FileID fileId) {
  if (!tuModel_ || !tuView_) {
    return;
  }

  QModelIndex tuIndex;

  if (fileId != FileManager::InvalidFileID) {
    tuIndex = tuModel_->findIndexByFileId(fileId);
  }

  if (!tuIndex.isValid() && fileId != FileManager::InvalidFileID) {
    if (!currentSourceFilePath_.isEmpty()) {
      std::string_view path = fileManager_.getFilePath(fileId);
      if (!path.empty()) {
        QString qPath = QString::fromStdString(std::string(path));
        QModelIndex sourceRoot =
            tuModel_->findIndexByFilePath(currentSourceFilePath_);
        if (sourceRoot.isValid()) {
          if (tuModel_->canFetchMore(sourceRoot)) {
            tuModel_->fetchMore(sourceRoot);
          }
          tuIndex = tuModel_->findIndexByAnyFilePathUnder(qPath, sourceRoot);
        }
      }
    }
  }

  if (!tuIndex.isValid()) {
    return;
  }

  // Ensure the path is visible by expanding ancestors
  QModelIndex parent = tuIndex.parent();
  while (parent.isValid()) {
    tuView_->expand(parent);
    parent = parent.parent();
  }

  // Use selectionModel to set both current and selected state
  tuView_->selectionModel()->setCurrentIndex(
      tuIndex, QItemSelectionModel::ClearAndSelect);
  tuView_->scrollTo(tuIndex, QAbstractItemView::PositionAtCenter);

  QRect rect = tuView_->visualRect(tuIndex);
  if (rect.isValid()) {
    int depth = 0;
    QModelIndex p = tuIndex.parent();
    while (p.isValid()) {
      ++depth;
      p = p.parent();
    }

    QString text = tuIndex.data(Qt::DisplayRole).toString();
    QFontMetrics fm(tuView_->font());
    int textWidth = fm.horizontalAdvance(text);

    int iconWidth = 0;
    QVariant iconVar = tuIndex.data(Qt::DecorationRole);
    if (iconVar.canConvert<QIcon>()) {
      QSize iconSize = tuView_->iconSize();
      if (iconSize.isValid()) {
        iconWidth = iconSize.width() + 4;
      }
    }

    int indent = tuView_->indentation() * depth;
    int padding = 8;
    int textLeft = rect.left() + indent + iconWidth + padding;
    int textRight = textLeft + textWidth;

    QScrollBar *hBar = tuView_->horizontalScrollBar();
    int viewWidth = tuView_->viewport()->width();
    if (textLeft < 0) {
      hBar->setValue(hBar->value() + textLeft);
    } else if (textRight > viewWidth) {
      hBar->setValue(hBar->value() + (textRight - viewWidth));
    }
  }
}

void MainWindow::onCycleNodeSelected(AstViewNode *node) {
  if (!node) {
    return;
  }

  QModelIndex modelIndex = astModel_->selectNode(node);
  if (modelIndex.isValid()) {
    astView_->selectionModel()->setCurrentIndex(
        modelIndex, QItemSelectionModel::ClearAndSelect);
    astView_->scrollTo(modelIndex);

    // Also highlight in source
    const SourceRange &range = node->getSourceRange();
    highlightTuFile(range.begin().fileID());
    if (range.begin().fileID() == sourceView_->currentFileId()) {
      sourceView_->highlightRange(range);
    }
    recordHistory(range.begin().fileID(), range.begin().line(),
                  range.begin().column(), node);
  }
}

void MainWindow::onCycleWidgetClosed() {
  // Widget is closed, no special action needed
}

void MainWindow::onExpandAllAstChildren() { expandAllChildren(astView_); }

void MainWindow::onCollapseAllAstChildren() { collapseAllChildren(astView_); }

namespace {

bool isTuSourceNode(const QModelIndex &index) {
  return index.data(Qt::UserRole + 3).toBool();
}

/// Check if index is a source file node or a descendant of one (e.g., header)
bool isSourceFileOrDescendant(const QModelIndex &index) {
  QModelIndex current = index;
  while (current.isValid()) {
    if (isTuSourceNode(current)) {
      return true;
    }
    current = current.parent();
  }
  return false;
}

} // namespace

void MainWindow::onTuContextMenuRequested(const QPoint &pos) {
  QModelIndex index = tuView_->indexAt(pos);
  if (!index.isValid()) {
    return;
  }

  // Select the right-clicked item so actions operate on it
  tuView_->setCurrentIndex(index);

  ::QMenu menu(tuView_);

  // Context-aware expand/collapse:
  // - Directory nodes: expand/collapse directories only (down to source files)
  // - Source file or descendants: expand/collapse all descendants (incl.
  // headers)
  QAction *expandAction = menu.addAction(tr("Expand All"));
  QAction *collapseAction = menu.addAction(tr("Collapse All"));

  connect(expandAction, &QAction::triggered, this,
          &MainWindow::onExpandAllTuChildren);
  connect(collapseAction, &QAction::triggered, this,
          &MainWindow::onCollapseAllTuChildren);

  menu.exec(tuView_->viewport()->mapToGlobal(pos));
}

void MainWindow::onExpandAllTuChildren() {
  QModelIndex currentIndex = tuView_->currentIndex();
  if (!currentIndex.isValid()) {
    return;
  }

  // Context-aware expand:
  // - Inside source file subtree: expand all descendants (including headers)
  // - Directory level: expand directories only (down to source files)
  if (isSourceFileOrDescendant(currentIndex)) {
    expandSubtree(tuView_);
  } else {
    expandAllChildren(tuView_);
  }
}

void MainWindow::onCollapseAllTuChildren() {
  QModelIndex currentIndex = tuView_->currentIndex();
  if (!currentIndex.isValid()) {
    return;
  }

  // Context-aware collapse:
  // - Inside source file subtree: collapse all descendants
  // - Directory level: collapse directories only (down to source files)
  if (isSourceFileOrDescendant(currentIndex)) {
    collapseAllChildren(tuView_);
  } else {
    collapseTuDirectories(tuView_);
  }
}

void MainWindow::expandAllChildren(QTreeView *view) {
  QModelIndex currentIndex = view->currentIndex();
  if (!currentIndex.isValid()) {
    return;
  }

  // Temporarily disable ResizeToContents to avoid O(n) width recalculation
  QHeaderView::ResizeMode prevResizeMode = view->header()->sectionResizeMode(0);
  view->header()->setSectionResizeMode(QHeaderView::Fixed);
  view->setUpdatesEnabled(false);

  // Block selection signals during batch expand to prevent
  // onAstNodeSelected firing for every intermediate expand.
  if (view->selectionModel()) {
    view->selectionModel()->blockSignals(true);
  }

  if (view == tuView_) {
    // TU view: expand directory structure down to source-file leaf nodes,
    // without expanding source nodes (which would populate header subtrees).
    std::vector<QModelIndex> stack;
    stack.reserve(256);
    stack.push_back(currentIndex);

    QAbstractItemModel *model = view->model();
    while (!stack.empty()) {
      QModelIndex idx = stack.back();
      stack.pop_back();
      if (!idx.isValid()) {
        continue;
      }

      if (isTuSourceNode(idx)) {
        continue;
      }

      int childCount = model->rowCount(idx);
      if (childCount <= 0) {
        continue;
      }

      if (!view->isExpanded(idx)) {
        view->expand(idx);
      }

      for (int row = 0; row < childCount; ++row) {
        stack.push_back(model->index(row, 0, idx));
      }
    }
  } else {
    // Expand subtree only (not the entire view).
    view->expandRecursively(currentIndex);
  }

  if (view->selectionModel()) {
    view->selectionModel()->blockSignals(false);
  }
  view->setUpdatesEnabled(true);
  view->header()->setSectionResizeMode(prevResizeMode);
}

void MainWindow::expandSubtree(QTreeView *view) {
  if (!view) {
    return;
  }

  QModelIndex currentIndex = view->currentIndex();
  if (!currentIndex.isValid()) {
    return;
  }

  QHeaderView::ResizeMode prevResizeMode = view->header()->sectionResizeMode(0);
  view->header()->setSectionResizeMode(QHeaderView::Fixed);
  view->setUpdatesEnabled(false);
  if (view->selectionModel()) {
    view->selectionModel()->blockSignals(true);
  }

  QAbstractItemModel *model = view->model();
  std::vector<QModelIndex> stack;
  stack.reserve(256);
  stack.push_back(currentIndex);

  while (!stack.empty()) {
    QModelIndex idx = stack.back();
    stack.pop_back();
    if (!idx.isValid()) {
      continue;
    }

    if (model->canFetchMore(idx)) {
      model->fetchMore(idx);
    }

    int childCount = model->rowCount(idx);
    if (childCount > 0 && !view->isExpanded(idx)) {
      view->expand(idx);
    }

    for (int row = 0; row < childCount; ++row) {
      stack.push_back(model->index(row, 0, idx));
    }
  }

  if (view->selectionModel()) {
    view->selectionModel()->blockSignals(false);
  }
  view->setUpdatesEnabled(true);
  view->header()->setSectionResizeMode(prevResizeMode);
}

void MainWindow::collapseAllChildren(QTreeView *view) {
  QModelIndex currentIndex = view->currentIndex();
  if (!currentIndex.isValid()) {
    return;
  }

  QHeaderView::ResizeMode prevResizeMode = view->header()->sectionResizeMode(0);
  view->header()->setSectionResizeMode(QHeaderView::Fixed);
  view->setUpdatesEnabled(false);

  // Block selection signals during batch collapse to prevent
  // onAstNodeSelected + navigateToRange firing for every intermediate collapse.
  if (view->selectionModel()) {
    view->selectionModel()->blockSignals(true);
  }

  if (!currentIndex.parent().isValid()) {
    // Root node: use Qt's built-in collapseAll() which is O(1) internally,
    // avoiding the O(N) individual collapse calls that hang on large ASTs.
    view->collapseAll();
  } else {
    // Subtree: collapse recursively from the selected node.
    collapseRecursively(currentIndex, view);
  }

  if (view->selectionModel()) {
    view->selectionModel()->blockSignals(false);
  }
  view->setUpdatesEnabled(true);
  view->header()->setSectionResizeMode(prevResizeMode);
}

void MainWindow::collapseTuDirectories(QTreeView *view) {
  if (!view) {
    return;
  }

  QModelIndex currentIndex = view->currentIndex();
  if (!currentIndex.isValid()) {
    return;
  }

  QHeaderView::ResizeMode prevResizeMode = view->header()->sectionResizeMode(0);
  view->header()->setSectionResizeMode(QHeaderView::Fixed);
  view->setUpdatesEnabled(false);
  if (view->selectionModel()) {
    view->selectionModel()->blockSignals(true);
  }

  QAbstractItemModel *model = view->model();
  std::vector<QModelIndex> stack;
  std::vector<QModelIndex> postOrder;
  stack.reserve(256);
  postOrder.reserve(256);
  stack.push_back(currentIndex);

  while (!stack.empty()) {
    QModelIndex idx = stack.back();
    stack.pop_back();
    if (!idx.isValid()) {
      continue;
    }

    postOrder.push_back(idx);

    int rowCount = model->rowCount(idx);
    for (int row = 0; row < rowCount; ++row) {
      QModelIndex child = model->index(row, 0, idx);
      // Traverse ALL expanded children including source files.
      // This ensures headers inside source files get collapsed too.
      if (view->isExpanded(child)) {
        stack.push_back(child);
      }
    }
  }

  for (std::size_t i = postOrder.size(); i-- > 0;) {
    view->collapse(postOrder[i]);
  }

  if (view->selectionModel()) {
    view->selectionModel()->blockSignals(false);
  }

  view->setUpdatesEnabled(true);
  view->header()->setSectionResizeMode(prevResizeMode);
}

void MainWindow::collapseRecursively(const QModelIndex &index,
                                     QTreeView *view) {
  if (!index.isValid() || !view) {
    return;
  }

  QAbstractItemModel *model = view->model();
  std::vector<QModelIndex> stack;
  std::vector<QModelIndex> postOrder;
  stack.reserve(256);
  postOrder.reserve(256);
  stack.push_back(index);

  while (!stack.empty()) {
    QModelIndex idx = stack.back();
    stack.pop_back();
    if (!idx.isValid()) {
      continue;
    }

    postOrder.push_back(idx);

    int rowCount = model->rowCount(idx);
    for (int row = 0; row < rowCount; ++row) {
      QModelIndex child = model->index(row, 0, idx);
      if (view->isExpanded(child)) {
        stack.push_back(child);
      }
    }
  }

  for (std::size_t i = postOrder.size(); i-- > 0;) {
    view->collapse(postOrder[i]);
  }
}

void MainWindow::updateSourceSubtitle(const QString &filePath) {
  if (!sourceTitleBar_ || filePath.isEmpty()) {
    if (sourceTitleBar_) {
      sourceTitleBar_->setSubtitle(QString());
    }
    return;
  }

  QString projectRoot = tuModel_ ? tuModel_->projectRoot() : QString();
  QString subtitle;

  if (!projectRoot.isEmpty() && filePath.startsWith(projectRoot + "/")) {
    // File is inside project - show relative path with [project] prefix
    QString relativePath = filePath.mid(projectRoot.length() + 1);
    subtitle = QStringLiteral("[project] %1").arg(relativePath);
  } else {
    // File is external - show absolute path with [external] prefix
    subtitle = QStringLiteral("[external] %1").arg(filePath);
  }

  sourceTitleBar_->setSubtitle(subtitle);
}

void MainWindow::updateAstSubtitle(const QString &mainSourcePath) {
  if (!astTitleBar_ || mainSourcePath.isEmpty()) {
    if (astTitleBar_) {
      astTitleBar_->setSubtitle(QString());
    }
    return;
  }

  QString projectRoot = tuModel_ ? tuModel_->projectRoot() : QString();
  QString subtitle;

  if (!projectRoot.isEmpty() && mainSourcePath.startsWith(projectRoot + "/")) {
    // File is inside project - show relative path with [project] prefix
    QString relativePath = mainSourcePath.mid(projectRoot.length() + 1);
    subtitle = QStringLiteral("[project] %1").arg(relativePath);
  } else {
    // File is external - show absolute path with [external] prefix
    subtitle = QStringLiteral("[external] %1").arg(mainSourcePath);
  }

  astTitleBar_->setSubtitle(subtitle);
}

} // namespace acav
