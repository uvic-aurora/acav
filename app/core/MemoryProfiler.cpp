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

#include "core/MemoryProfiler.h"
#include "core/AppConfig.h"
#include <QDebug>
#include <QMutex>

// Cross-platform memory profiling
#ifdef _WIN32
  #include <windows.h>
  #include <psapi.h>
#else
  #include <sys/resource.h>
  #ifdef __APPLE__
    #include <mach/mach.h>
  #else
    #include <unistd.h>
    #include <fstream>
  #endif
#endif

namespace acav {

namespace {

QMutex gLogMutex;
MemoryProfiler::LogCallback gLogCallback;

MemoryProfiler::LogCallback getLogCallback() {
  QMutexLocker locker(&gLogMutex);
  return gLogCallback;
}

} // namespace

void MemoryProfiler::setLogCallback(LogCallback callback) {
  QMutexLocker locker(&gLogMutex);
  gLogCallback = std::move(callback);
}

bool MemoryProfiler::isEnabled() {
  return AppConfig::instance().getMemoryProfilingEnabled();
}

long MemoryProfiler::getPeakMemoryMB() {
#ifdef _WIN32
  // Windows: Use GetProcessMemoryInfo
  PROCESS_MEMORY_COUNTERS_EX pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), 
                          (PROCESS_MEMORY_COUNTERS*)&pmc, 
                          sizeof(pmc))) {
    return static_cast<long>(pmc.PeakWorkingSetSize / (1024 * 1024));
  }
  return -1;
#else
  // Unix/macOS: Use getrusage
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
    // macOS: ru_maxrss is in bytes
    return usage.ru_maxrss / (1024 * 1024);
#else
    // Linux: ru_maxrss is in kilobytes
    return usage.ru_maxrss / 1024;
#endif
  }
  return -1;
#endif
}

long MemoryProfiler::getCurrentMemoryMB() {
#ifdef _WIN32
  // Windows: Use GetProcessMemoryInfo for current working set
  PROCESS_MEMORY_COUNTERS_EX pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), 
                          (PROCESS_MEMORY_COUNTERS*)&pmc, 
                          sizeof(pmc))) {
    return static_cast<long>(pmc.WorkingSetSize / (1024 * 1024));
  }
  return -1;
#elif defined(__APPLE__)
  // macOS: Use mach task info for current memory
  struct mach_task_basic_info info;
  mach_msg_type_number_t size = MACH_TASK_BASIC_INFO_COUNT;
  kern_return_t kerr = task_info(mach_task_self(),
                                 MACH_TASK_BASIC_INFO,
                                 (task_info_t)&info,
                                 &size);
  if (kerr == KERN_SUCCESS) {
    return static_cast<long>(info.resident_size / (1024 * 1024));
  }
  return -1;
#else
  // Linux: Read from /proc/self/status
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.substr(0, 6) == "VmRSS:") {
      long rss_kb = 0;
      sscanf(line.c_str(), "VmRSS: %ld kB", &rss_kb);
      return rss_kb / 1024;
    }
  }
  return -1;
#endif
}

void MemoryProfiler::checkpoint(const QString &label) {
  if (!isEnabled()) {
    return;
  }

  long currentMB = getCurrentMemoryMB();
  long peakMB = getPeakMemoryMB();
  
  const LogCallback callback = getLogCallback();
  if (currentMB >= 0 && peakMB >= 0) {
    const QString message = QString("[MEM] %1: %2 MB (peak: %3 MB)")
                                .arg(label, -40)
                                .arg(currentMB)
                                .arg(peakMB);
    if (callback) {
      callback(message);
    } else {
      qDebug().noquote() << message;
    }
  } else {
    const QString message =
        QString("[MEM] Failed to retrieve memory usage for: %1").arg(label);
    if (callback) {
      callback(message);
    } else {
      qWarning().noquote() << message;
    }
  }
}

} // namespace acav
