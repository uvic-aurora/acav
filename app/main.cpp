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

#include "Version.h"
#include "core/AppConfig.h"
#include "core/LogEntry.h"
#include "core/MemoryProfiler.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QIcon>
#include <QStyleFactory>
#include <QTextStream>
#include <cstring>
#include <iostream>

namespace {
constexpr const char *kAppName = "ACAV - Clang AST Viewer";
constexpr const char *kAppDescription =
    "ACAV (Clang AST Viewer) - A tool for visualizing and exploring "
    "Clang Abstract Syntax Trees with support for C++20 modules.";

// Handle --help and --version before QApplication to avoid requiring a display
bool handleEarlyArgs(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") == 0 ||
        std::strcmp(argv[i], "-v") == 0) {
      std::cout << kAppName << " " << ACAV_VERSION_STRING << "\n";
      return true;
    }
    if (std::strcmp(argv[i], "--help") == 0 ||
        std::strcmp(argv[i], "-h") == 0) {
      std::cout << kAppDescription << "\n\n"
                << "Usage: acav [options]\n\n"
                << "Options:\n"
                << "  -h, --help                         Display this help\n"
                << "  -v, --version                      Display version\n"
                << "  --config <path>                    Config file (default: ~/.acav)\n"
                << "  -c, --compilation-database <path>  Path to compile_commands.json\n"
                << "  -p, --project-root <path>          Override project root\n";
      return true;
    }
  }
  return false;
}
} // namespace

int main(int argc, char *argv[]) {
  // Handle --help and --version early (before QApplication requires a display)
  if (handleEarlyArgs(argc, argv)) {
    return 0;
  }

  QApplication app(argc, argv);
  app.setApplicationName(kAppName);
  app.setApplicationVersion(ACAV_VERSION_STRING);

  // Use Windows style for native tree line support across all widgets
  // Fall back to Fusion if Windows style is not available
  if (auto *windowsStyle = QStyleFactory::create("Windows")) {
    app.setStyle(windowsStyle);
  } else if (auto *fusionStyle = QStyleFactory::create("Fusion")) {
    app.setStyle(fusionStyle);
  }

  // Load the unified application stylesheet from resources
  QFile styleFile(":/resources/style.qss");
  if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
    app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    styleFile.close();
  }

  // Register custom types for queued signal/slot connections
  // Register with both names: MOC uses "LogEntry" in signal signatures
  // (since signals are declared inside aurora namespace without prefix)
  qRegisterMetaType<acav::LogEntry>("acav::LogEntry");
  qRegisterMetaType<acav::LogEntry>("LogEntry");
  app.setOrganizationName("ACAV");
  app.setWindowIcon(QIcon(":/resources/aurora_icon.png"));

  // Set up command-line parser for remaining options
  QCommandLineParser parser;
  parser.setApplicationDescription(kAppDescription);

  // Add --config option
  QCommandLineOption configOption("config",
      "Path to configuration file (INI format). Default: ~/.acav",
      "path");
  parser.addOption(configOption);

  // Add --compilation-database option
  QCommandLineOption compilationDatabaseOption(
      QStringList() << "c" << "compilation-database",
      "Path to compilation database (compile_commands.json)",
      "path");
  parser.addOption(compilationDatabaseOption);

  // Add --project-root option
  QCommandLineOption projectRootOption(
      QStringList() << "p" << "project-root",
      "Override project root directory (default: auto-detect from source files)",
      "path");
  parser.addOption(projectRootOption);

  // Skip unknown options (--help/-h and --version/-v handled above)
  parser.addOptions({
      {{"h", "help"}, "Display help"},
      {{"v", "version"}, "Display version"}
  });

  if (!parser.parse(app.arguments())) {
    QTextStream err(stderr);
    err << parser.errorText() << "\n";
    return 1;
  }

  // Initialize AppConfig with custom or default path
  QString configPath = parser.value(configOption);
  acav::AppConfig::initialize(configPath);
  
  acav::MemoryProfiler::checkpoint("After AppConfig initialization");

  // Create and show main window
  acav::MainWindow mainWindow;
  mainWindow.show();
  
  acav::MemoryProfiler::checkpoint("After MainWindow creation");

  // If compilation database was provided, load it
  if (parser.isSet(compilationDatabaseOption)) {
    QString dbPath = parser.value(compilationDatabaseOption);
    QString projectRoot = parser.value(projectRootOption);
    mainWindow.loadCompilationDatabase(dbPath, projectRoot);
  }

  return app.exec();
}
