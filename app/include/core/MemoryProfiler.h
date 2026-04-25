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

/// \file MemoryProfiler.h
/// \brief Cross-platform memory profiling utility.
#pragma once

#include <QString>
#include <functional>

namespace acav {

/// \brief Cross-platform memory profiling utility.
///
/// Provides lightweight memory usage tracking without external tools.
/// Uses getrusage() on Unix/macOS and GetProcessMemoryInfo() on Windows.
/// Enable via config: debug/enableMemoryProfiling=true
///
class MemoryProfiler {
public:
  using LogCallback = std::function<void(const QString &message)>;

  /// \brief Log current memory usage with a label
  /// \param label Descriptive label for this checkpoint
  static void checkpoint(const QString &label);

  /// \brief Get current peak memory usage in MB (maximum RSS since process start)
  /// \return Peak resident set size in megabytes, or -1 if unavailable
  static long getPeakMemoryMB();

  /// \brief Get current actual memory usage in MB (current RSS)
  /// \return Current resident set size in megabytes, or -1 if unavailable
  static long getCurrentMemoryMB();

  /// \brief Set a callback for memory log messages (GUI hook).
  static void setLogCallback(LogCallback callback);

private:
  static bool isEnabled();
};

} // namespace acav
