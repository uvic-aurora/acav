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

/// \file MainWindow.h
/// \brief Main application window with multi-panel layout.
#pragma once

#include "common/FileManager.h"
#include "core/AstExtractorRunner.h"
#include "core/MakeAstRunner.h"
#include "core/QueryDependenciesParallelRunner.h"
#include "core/QueryDependenciesRunner.h"
#include "ui/AstModel.h"
#include "ui/DeclContextView.h"
#include "ui/DockTitleBar.h"
#include "ui/LogDock.h"
#include "ui/NodeCycleWidget.h"
#include "ui/SourceCodeView.h"
#include "ui/TranslationUnitModel.h"
#include <QAction>
#include <QByteArray>
#include <QCheckBox>
#include <QCompleter>
#include <QDockWidget>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPoint>
#include <QSet>
#include <QSpinBox>
#include <QStringList>
#include <QStringListModel>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <memory>
#include <vector>

namespace acav {

class AstContext; // Forward declaration
class MainWindowTestAccess;

/// \brief Main application window with multi-panel dock layout
///
/// The MainWindow provides the primary user interface for ACAV.
/// It consists of dockable panels arranged horizontally:
/// - Translation Unit View: List of source files from compilation database
/// - Source Code View: Display of selected source file
/// - AST Tree View: AST hierarchy
///
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  /// \brief Load compilation database and populate translation units
  /// \param compilationDatabasePath Path to compile_commands.json
  /// \param projectRoot Optional project root directory (auto-detected if
  /// empty)
  void loadCompilationDatabase(const QString &compilationDatabasePath,
                               const QString &projectRoot = QString());

private slots:
  // Menu actions
  void onOpenCompilationDatabase();
  void onExit();

  // View interactions
  void onTranslationUnitSelected(const QModelIndex &index);
  void onTranslationUnitClicked(const QModelIndex &index);

  // Query-dependencies integration
  void onDependenciesReady(const QJsonObject &dependencies);
  void onDependenciesReadyWithErrors(const QJsonObject &dependencies,
                                     const QStringList &errorMessages);
  void onDependenciesError(const QString &errorMessage);
  void onDependenciesProgress(const QString &message);

  // AST extraction
  void onAstReady(const QString &astFilePath);
  void onAstExtracted(AstViewNode *root);
  void onAstError(const QString &errorMessage);
  void onMakeAstLogMessage(const LogEntry &entry);
  void onAstProgress(const QString &message);
  void onAstStatsUpdated(const AstExtractionStats &stats);
  void onTimingMessage(const QString &message);
  void onAstContextMenuRequested(const QPoint &pos);
  void onViewNodeDetails(AstViewNode *node);
  void onExportAst(AstViewNode *node);
  void onExpandAllAstChildren();
  void onCollapseAllAstChildren();
  void onGoToMacroDefinition();

  // Source Files tree context menu
  void onTuContextMenuRequested(const QPoint &pos);
  void onExpandAllTuChildren();
  void onCollapseAllTuChildren();

  // Navigation
  void onAstNodeSelected(const QModelIndex &index);
  void onSourcePositionClicked(FileID fileId, unsigned line, unsigned column);
  void onSourceRangeSelected(FileID fileId, unsigned startLine,
                             unsigned startColumn, unsigned endLine,
                             unsigned endColumn);
  void onCycleNodeSelected(AstViewNode *node);
  void onCycleWidgetClosed();
  void onSourceSearchTextChanged(const QString &text);
  void onSourceSearchDebounced();
  void onSourceSearchFindNext();
  void onSourceSearchFindPrevious();
  void onAstSearchTextChanged(const QString &text);
  void onAstSearchDebounced();
  void onAstSearchFindNext();
  void onAstSearchFindPrevious();
  void onShowAbout();
  void onResetLayout();
  void onFocusChanged(QWidget *old, QWidget *now);

protected:
  void resizeEvent(QResizeEvent *event) override;
  void moveEvent(QMoveEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  friend class MainWindowTestAccess;

  // UI setup
  void setupUI();
  void setupMenuBar();
  void setupDockWidgets();
  void setupModels();
  void setupTuSearch();
  void setupSourceSearchPanel(QWidget *container);
  void setupAstSearchPanel(QWidget *container);
  void setupSettingsAction();
  void applyUnifiedSelectionPalette();
  void setupViewMenuDockActions();
  void applyFontSize(int size);
  void adjustFontSize(int delta);
  void expandFileExplorerTopLevel();
  void connectSignals();

  // Status bar helper
  void logStatus(LogLevel level, const QString &message,
                 const QString &source = QString());

  // AST helper methods
  void extractAst(const QString &astFilePath, const QString &sourceFilePath);
  QStringList getFileListForSource(const QString &sourceFilePath) const;
  bool isSourceFile(const QString &filePath) const;
  bool deleteCachedAst(const QString &astFilePath);
  QString astCacheStatusFilePath(const QString &astFilePath) const;
  void persistAstCompilationErrorState(const QString &astFilePath,
                                       bool hasCompilationErrors);
  bool loadAstCompilationErrorState(const QString &astFilePath) const;

  // Navigation history
  struct NavEntry {
    FileID fileId;
    unsigned line;
    unsigned column;
    AstViewNode *node;
    std::size_t astVersion;
  };

  void recordHistory(FileID fileId, unsigned line, unsigned column,
                     AstViewNode *node);
  void clearHistory();
  void navigateHistory(int delta);
  void applyEntry(const NavEntry &entry);
  void updateNavActions();
  void onOpenConfigFile();
  void onReloadConfig();
  QString buildDefaultExportFileName(AstViewNode *node) const;
  void triggerSourceSearch(bool forward);
  void triggerAstSearch(bool forward);
  void collectAstSearchMatches(const QString &expression);
  void navigateToAstMatch(int index);
  void clearAstSearchState();
  void setAstSearchStatus(const QString &text, bool isError);
  void showAstSearchPopup(bool selectAll = false);
  void syncAstSearchPopupGeometry();
  void rememberAstSearchQuery(const QString &query);
  void setAstCompilationWarningVisible(bool visible);
  void expandAllChildren(QTreeView *view);
  void expandSubtree(QTreeView *view);
  void collapseAllChildren(QTreeView *view);
  void collapseTuDirectories(QTreeView *view);
  void collapseRecursively(const QModelIndex &index, QTreeView *view);
  void highlightTuFile(FileID fileId);
  bool navigateToRange(const SourceRange &range, AstViewNode *node,
                       bool skipCursorMove);
  bool getMacroSpellingRange(const AstViewNode *node,
                             SourceRange *outRange) const;

  /// \brief Common validation for source position/range lookup handlers.
  /// \return true if validation passed and lookup can proceed, false otherwise.
  bool validateSourceLookup(FileID fileId);

  /// \brief Log appropriate message when no AST node is found.
  void logNoNodeFound(FileID fileId, const QString &fallbackMessage);

  /// \brief Update subtitle for Source Code title bar with current file path.
  void updateSourceSubtitle(const QString &filePath);

  /// \brief Update subtitle for AST title bar with main source file path.
  void updateAstSubtitle(const QString &mainSourcePath);

  // Menu bar
  ::QMenu *fileMenu_;
  ::QMenu *viewMenu_;
  QAction *openAction_;
  QAction *exitAction_;
  QAction *settingsAction_;
  QAction *reloadConfigAction_;
  QAction *navBackAction_;
  QAction *navForwardAction_;
  QAction *goToMacroDefinitionAction_;
  QAction *zoomInAction_;
  QAction *zoomOutAction_;
  QAction *zoomResetAction_;
  QToolBar *navToolBar_;
  QAction *resetLayoutAction_;
  QLineEdit *tuSearch_;
  QLineEdit *sourceSearchInput_;
  QToolButton *sourceSearchPrevButton_;
  QToolButton *sourceSearchNextButton_;
  QLabel *sourceSearchStatus_;
  ::QTimer *sourceSearchDebounce_;

  // AST search
  QDialog *astSearchPopup_ = nullptr;
  QLineEdit *astSearchQuickInput_ = nullptr;
  QLineEdit *astSearchInput_ = nullptr;
  QToolButton *astSearchPrevButton_ = nullptr;
  QToolButton *astSearchNextButton_ = nullptr;
  QLabel *astSearchStatus_ = nullptr;
  QLabel *astCompilationWarningLabel_ = nullptr;
  QCheckBox *astSearchProjectFilter_ = nullptr;
  QCompleter *astSearchCompleter_ = nullptr;
  QStringListModel *astSearchHistoryModel_ = nullptr;
  QStringList astSearchHistory_;
  std::vector<AstViewNode *> astSearchMatches_;
  int astSearchCurrentIndex_ = -1;

  // Dock widgets
  QDockWidget *tuDock_;
  QDockWidget *sourceDock_;
  QDockWidget *astDock_;
  QDockWidget *declContextDock_;
  LogDock *logDock_;

  // Views
  QTreeView *tuView_;
  SourceCodeView *sourceView_;
  QTreeView *astView_;
  DeclContextView *declContextView_;

  // Models
  TranslationUnitModel *tuModel_;
  AstModel *astModel_;

  // Core components
  QueryDependenciesRunner *queryRunner_;
  QueryDependenciesParallelRunner *parallelQueryRunner_;
  MakeAstRunner *makeAstRunner_;
  AstExtractorRunner *astExtractorRunner_;
  QThread *astWorkerThread_;
  QThread *astExportThread_ = nullptr;
  FileManager fileManager_;
  std::unique_ptr<AstContext> astContext_; // One context per TU

  // Configuration
  static constexpr int kParallelThreshold = 10; // Use parallel if >= 10 files
  static constexpr int kMinFontSize = 8;
  static constexpr int kMaxFontSize = 32;

  // Current state
  QString compilationDatabasePath_;
  QString projectRoot_; // User-specified or auto-detected project root
  QString currentSourceFilePath_; // Track which file AST is being generated for
  QString lastAstFilePath_; // Track last AST cache path used for extraction
  bool isAstExtractionInProgress_;     // Track if AST extraction is ongoing
  bool isAstExportInProgress_ = false; // Track if AST export is ongoing
  QString pendingSourceFilePath_;      // File path that is being processed
  bool astHasCompilationErrors_ = false;
  std::size_t astVersion_ = 0;
  int currentFontSize_ = 11;
  QString currentFontFamily_;

  // Per-subwindow font sizes
  int tuFontSize_ = 11;
  int sourceFontSize_ = 11;
  int astFontSize_ = 11;
  int declContextFontSize_ = 11;
  int logFontSize_ = 11;

  QByteArray defaultDockState_;

  // Navigation
  NodeCycleWidget *nodeCycleWidget_;
  std::vector<NavEntry> history_;
  std::size_t historyCursor_ = 0;
  bool suppressHistory_ = false;
  bool suppressSourceCursorMove_ = false;
  bool suppressSourceHighlight_ = false;
  bool isNavigatingFromDeclContext_ = false;

  // Focus tracking with custom title bars (fast, no setStyleSheet)
  QDockWidget *focusedDock_ = nullptr;
  DockTitleBar *tuTitleBar_ = nullptr;
  DockTitleBar *sourceTitleBar_ = nullptr;
  DockTitleBar *astTitleBar_ = nullptr;
  DockTitleBar *declContextTitleBar_ = nullptr;
  DockTitleBar *logTitleBar_ = nullptr;
};

} // namespace acav
