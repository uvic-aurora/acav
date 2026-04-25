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

#include "core/AppConfig.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>

namespace acav {

AppConfig *AppConfig::instance_ = nullptr;

QString AppConfig::getDefaultConfigPath() {
  QString homeDir =
      QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  return homeDir + "/.acav";
}

void AppConfig::createDefaultConfigFile(const QString &path) {
  QFileInfo fileInfo(path);
  QDir dir = fileInfo.dir();
  if (!dir.exists()) {
    dir.mkpath(".");
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    qWarning("Failed to create default config file: %s",
             qPrintable(path));
    return;
  }

  QTextStream out(&file);
  out << "# ACAV (Clang AST Viewer) Configuration File\n";
  out << "# Location: ~/.acav\n";
  out << "# Edit this file to customize application behavior\n";
  out << "# Use File > Reload Configuration to apply changes\n";
  out << "\n";
  out << "[cache]\n";
  out << "# Directory for storing AST files and dependency analysis results\n";
  out << "# Use absolute path or leave empty to use ~/.cache/acav/\n";
  out << "# Default: (empty, uses ~/.cache/acav/)\n";
  out << "root=\n";
  out << "\n";
  out << "[ui]\n";
  out << "# Font family for UI (empty = system default)\n";
  out << "# Example: fontFamily=Helvetica Neue\n";
  out << "fontFamily=\n";
  out << "\n";
  out << "# Font size for tree views and source code editor\n";
  out << "# Valid range: 8-32\n";
  out << "# Default: 11\n";
  out << "fontSize=11\n";
  out << "\n";
  out << "[ast]\n";
  out << "# Extract and display comments as properties on Decl nodes\n";
  out << "# When enabled, documentation comments are stored in the 'comment' property\n";
  out << "# Default: true\n";
  out << "commentExtraction=true\n";
  out << "\n";
  out << "[debug]\n";
  out << "# Enable memory profiling checkpoints\n";
  out << "# Shows peak memory usage at key points during processing\n";
  out << "# Default: false\n";
  out << "enableMemoryProfiling=false\n";
  out << "\n";
  out << "[performance]\n";
  out << "# Number of CPU cores for parallel dependency analysis\n";
  out << "# 0 = auto-detect (use all available cores)\n";
  out << "# Default: 0\n";
  out << "parallelProcessorCount=0\n";

  file.close();
}

void AppConfig::initialize(const QString &configFilePath) {
  if (instance_) {
    return; // Already initialized
  }

  QString configPath = configFilePath.isEmpty() 
      ? getDefaultConfigPath() 
      : configFilePath;

  // Create default config file if it doesn't exist
  if (!QFile::exists(configPath)) {
    createDefaultConfigFile(configPath);
  }

  instance_ = new AppConfig(configPath);
}

AppConfig &AppConfig::instance() {
  if (!instance_) {
    qFatal("AppConfig::initialize() must be called before instance()");
  }
  return *instance_;
}

AppConfig::AppConfig(const QString &configFilePath)
    : configFilePath_(configFilePath),
      settings_(configFilePath, QSettings::IniFormat) {
  loadSettings();
}

QString AppConfig::getConfigFilePath() const {
  return configFilePath_;
}

bool AppConfig::reload() {
  if (!QFile::exists(configFilePath_)) {
    createDefaultConfigFile(configFilePath_);
  }

  settings_.sync();
  loadSettings();
  return QFile::exists(configFilePath_);
}

void AppConfig::loadSettings() {
  // Check for config file errors
  if (settings_.status() != QSettings::NoError) {
    qWarning("Error reading config file '%s' (status=%d), using defaults",
             qPrintable(configFilePath_), static_cast<int>(settings_.status()));
    // Continue with defaults - don't return, as default values are set below
  }

  // Load cache root from settings, or use default
  cacheRoot_ = settings_.value("cache/root", QString()).toString();
  commentExtractionEnabled_ =
      settings_.value("ast/commentExtraction", true).toBool();
  memoryProfilingEnabled_ =
      settings_.value("debug/enableMemoryProfiling", false).toBool();

  // Load and validate font family - check if it exists on the system
  QString requestedFontFamily = settings_.value("ui/fontFamily", QString()).toString();
  if (!requestedFontFamily.isEmpty()) {
    QStringList availableFamilies = QFontDatabase::families();
    if (availableFamilies.contains(requestedFontFamily, Qt::CaseInsensitive)) {
      fontFamily_ = requestedFontFamily;
    } else {
      qWarning("Font family '%s' not found on system, using system default. "
               "Available fonts can be listed with 'fc-list' command.",
               qPrintable(requestedFontFamily));
      fontFamily_.clear();
    }
  } else {
    fontFamily_.clear();
  }

  // Load and validate font size (valid range: 8-32)
  constexpr int kDefaultFontSize = 11;
  constexpr int kMinFontSize = 8;
  constexpr int kMaxFontSize = 32;
  fontSize_ = settings_.value("ui/fontSize", kDefaultFontSize).toInt();
  if (fontSize_ < kMinFontSize || fontSize_ > kMaxFontSize) {
    qWarning("Invalid fontSize=%d in config, using default %d (valid range: %d-%d)",
             fontSize_, kDefaultFontSize, kMinFontSize, kMaxFontSize);
    fontSize_ = kDefaultFontSize;
  }

  // Load and validate parallel processor count (must be >= 0)
  parallelProcessorCount_ = settings_.value("performance/parallelProcessorCount", 0).toInt();
  if (parallelProcessorCount_ < 0) {
    qWarning("Invalid parallelProcessorCount=%d in config, using 0 (auto-detect)",
             parallelProcessorCount_);
    parallelProcessorCount_ = 0;
  }

  // Validate cache root - if specified, check if it's usable
  QString defaultCacheRoot = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                             + "/.cache/acav";
  if (cacheRoot_.isEmpty()) {
    cacheRoot_ = defaultCacheRoot;
  } else {
    // User specified a custom cache root - verify it's usable
    QDir dir(cacheRoot_);
    if (!dir.exists()) {
      // Try to create it
      if (!dir.mkpath(".")) {
        qWarning("Cannot create cache directory '%s', using default '%s'",
                 qPrintable(cacheRoot_), qPrintable(defaultCacheRoot));
        cacheRoot_ = defaultCacheRoot;
      }
    } else {
      // Directory exists, check if writable by trying to create a test file
      QFile testFile(cacheRoot_ + "/.acav_write_test");
      if (testFile.open(QIODevice::WriteOnly)) {
        testFile.close();
        testFile.remove();
      } else {
        qWarning("Cache directory '%s' is not writable, using default '%s'",
                 qPrintable(cacheRoot_), qPrintable(defaultCacheRoot));
        cacheRoot_ = defaultCacheRoot;
      }
    }
  }
}

int AppConfig::getFontSize() const { return fontSize_; }

QString AppConfig::getFontFamily() const { return fontFamily_; }

int AppConfig::getParallelProcessorCount() const { 
  return parallelProcessorCount_; 
}

QString AppConfig::hashPath(const QString &path) const {
  // Create a hash of the absolute path to use as directory name
  QFileInfo fileInfo(path);
  QString absolutePath = fileInfo.absoluteFilePath();

  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(absolutePath.toUtf8());
  QString hashStr = QString::fromLatin1(hash.result().toHex());

  // Use first 16 characters of hash for reasonable directory name
  return hashStr.left(16);
}

QString
AppConfig::getCacheDirectory(const QString &compilationDatabasePath) const {
  // Create a subdirectory based on the compilation database path
  // Structure: <cache_root>/<hash>/
  QString pathHash = hashPath(compilationDatabasePath);
  QString cacheDir = cacheRoot_ + "/" + pathHash;

  // Ensure directory exists and write metadata
  ensureCacheDirectory(compilationDatabasePath);

  return cacheDir;
}

QString AppConfig::getDependenciesFilePath(
    const QString &compilationDatabasePath) const {
  QString cacheDir = getCacheDirectory(compilationDatabasePath);
  return cacheDir + "/dependencies.json";
}

QString AppConfig::getMetadataFilePath(
    const QString &compilationDatabasePath) const {
  QString cacheDir = getCacheDirectory(compilationDatabasePath);
  return cacheDir + "/metadata.json";
}

QString AppConfig::getBuildDirectory(
    const QString &compilationDatabasePath) const {
  QString metadataPath = getMetadataFilePath(compilationDatabasePath);
  QFile metadataFile(metadataPath);
  
  if (!metadataFile.exists() || 
      !metadataFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    // Fallback: derive from compilation database path
    QFileInfo info(compilationDatabasePath);
    return info.absolutePath();
  }
  
  QByteArray data = metadataFile.readAll();
  metadataFile.close();
  
  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (doc.isNull() || !doc.isObject()) {
    QFileInfo info(compilationDatabasePath);
    return info.absolutePath();
  }
  
  QJsonObject metadata = doc.object();
  return metadata["buildDirectory"].toString();
}

void AppConfig::ensureCacheDirectory(
    const QString &compilationDatabasePath) const {
  QString cacheDir = cacheRoot_ + "/" + hashPath(compilationDatabasePath);
  QDir dir;

  if (!dir.exists(cacheDir)) {
    if (!dir.mkpath(cacheDir)) {
      return;
    }
  }

  // Also create a metadata file to remember which compilation database this is
  QString metadataPath = cacheDir + "/metadata.json";
  QFile metadataFile(metadataPath);
  if (!metadataFile.exists()) {
    if (metadataFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QFileInfo info(compilationDatabasePath);
      QJsonObject metadata;
      metadata["compilationDatabasePath"] = info.absoluteFilePath();
      metadata["buildDirectory"] = info.absolutePath();
      metadata["createdAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
      
      QJsonDocument doc(metadata);
      metadataFile.write(doc.toJson(QJsonDocument::Indented));
      metadataFile.close();
    }
  }
}

QString
AppConfig::getAstCacheDirectory(const QString &compilationDatabasePath) const {
  QString cacheDir = getCacheDirectory(compilationDatabasePath);
  QString astDir = cacheDir + "/ast";

  // Create directory if doesn't exist
  QDir dir;
  if (!dir.exists(astDir)) {
    dir.mkpath(astDir);
  }

  return astDir;
}

QString AppConfig::getAstFilePath(const QString &compilationDatabasePath,
                                  const QString &sourceFilePath) const {
  QString astDir = getAstCacheDirectory(compilationDatabasePath);
  QFileInfo sourceInfo(sourceFilePath);
  QString fileName = sourceInfo.fileName(); // e.g., "main.cpp"

  return astDir + "/" + fileName + ".ast";
}

bool AppConfig::getCommentExtractionEnabled() const {
  return commentExtractionEnabled_;
}

bool AppConfig::getMemoryProfilingEnabled() const {
  return memoryProfilingEnabled_;
}

QString AppConfig::getCacheRoot() const { return cacheRoot_; }

} // namespace acav
