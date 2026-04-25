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

/// \file AppConfig.h
/// \brief Application configuration and settings.
#pragma once

#include <QDir>
#include <QSettings>
#include <QString>

namespace acav {

/// \brief Application configuration manager
///
/// Manages application settings including cache directories for storing
/// query-dependencies output and other cached data. Uses an INI configuration
/// file that users can edit directly.
///
/// Default config file: ~/.acav
/// Default cache directory: ~/.cache/acav/
///
class AppConfig {
public:
  /// \brief Initialize the singleton with a custom config file path
  /// \param configFilePath Path to INI config file (empty = use default)
  /// Must be called before instance() - typically in main()
  static void initialize(const QString &configFilePath = QString());

  /// \brief Get the singleton instance
  /// \return Reference to AppConfig singleton
  /// \note initialize() must be called first
  static AppConfig &instance();

  /// \brief Get the path to the currently loaded configuration file
  /// \return Absolute path to the INI config file
  QString getConfigFilePath() const;

  /// \brief Reload settings from the config file on disk.
  /// \return true if the config file exists after reload.
  bool reload();

  /// \brief Get the cache directory for a compilation database
  /// \param compilationDatabasePath Path to compile_commands.json
  /// \return Directory path where dependencies.json should be stored
  QString getCacheDirectory(const QString &compilationDatabasePath) const;

  /// \brief Get the dependencies JSON file path for a compilation database
  /// \param compilationDatabasePath Path to compile_commands.json
  /// \return Full path to dependencies.json file
  QString getDependenciesFilePath(const QString &compilationDatabasePath) const;

  /// \brief Get the AST cache directory for a compilation database
  /// \param compilationDatabasePath Path to compile_commands.json
  /// \return Directory path where .ast files should be stored (creates if
  /// doesn't exist)
  QString getAstCacheDirectory(const QString &compilationDatabasePath) const;

  /// \brief Get the AST cache file path for a source file
  /// \param compilationDatabasePath Path to compile_commands.json
  /// \param sourceFilePath Path to source file
  /// \return Full path to .ast file (e.g.,
  /// ~/.cache/acav/<hash>/ast/file.cpp.ast)
  QString getAstFilePath(const QString &compilationDatabasePath,
                         const QString &sourceFilePath) const;

  /// \brief Get the metadata JSON file path for a compilation database
  /// \param compilationDatabasePath Path to compile_commands.json
  /// \return Full path to metadata.json file
  QString getMetadataFilePath(const QString &compilationDatabasePath) const;

  /// \brief Get the build directory from cached metadata
  /// \param compilationDatabasePath Path to compile_commands.json
  /// \return Build directory path, or empty string if not found
  QString getBuildDirectory(const QString &compilationDatabasePath) const;

  /// \brief Whether to extract and display comments in AST (default: false)
  bool getCommentExtractionEnabled() const;

  /// \brief Whether memory profiling checkpoints are enabled (default: false)
  bool getMemoryProfilingEnabled() const;

  /// \brief Custom cache root directory (default: ~/.cache/acav/)
  QString getCacheRoot() const;

  /// \brief Editor/UI font size (default: 11)
  int getFontSize() const;

  /// \brief Editor/UI font family (default: empty = system default)
  QString getFontFamily() const;

  /// \brief Number of parallel processors for query-dependencies (default: 0 = auto)
  /// 0 means use all available CPU cores, otherwise use the specified count
  int getParallelProcessorCount() const;

private:
  AppConfig(const QString &configFilePath);
  ~AppConfig() = default;

  // Prevent copying
  AppConfig(const AppConfig &) = delete;
  AppConfig &operator=(const AppConfig &) = delete;

  /// \brief Create default config file with comments
  static void createDefaultConfigFile(const QString &path);

  /// \brief Get default config file path
  static QString getDefaultConfigPath();

  static AppConfig *instance_;
  
  QString configFilePath_;
  QSettings settings_;
  QString cacheRoot_;
  bool commentExtractionEnabled_ = true;
  bool memoryProfilingEnabled_ = false;
  int fontSize_ = 11;
  QString fontFamily_;
  int parallelProcessorCount_ = 0;  // 0 = auto-detect

  void loadSettings();
  QString hashPath(const QString &path) const;
  void ensureCacheDirectory(const QString &compilationDatabasePath) const;
};

} // namespace acav
