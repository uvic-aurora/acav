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

/// \file InternedString.h
/// \brief Memory-efficient immutable string with automatic deduplication.
#pragma once

#include "Config.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace acav {

#ifdef ACAV_ENABLE_STRING_STATS
/// \brief Statistics about string interning memory usage.
struct StringInterningStats {
  std::size_t uniqueStrings;        ///< Number of unique strings in pool
  std::size_t totalReferences;      ///< Total number of InternedString instances
  std::size_t poolMemoryBytes;      ///< Actual memory used by pool (string data)
  std::size_t poolOverheadBytes;    ///< Pool structure overhead (StringData objects)
  std::size_t withoutInterningBytes;///< Memory that would be used without interning
  std::size_t savedBytes;           ///< Bytes saved by interning
  double savingsPercent;            ///< Percentage of memory saved
};
#endif

/// \brief Immutable string with automatic deduplication via global pool.
///
/// Provides memory-efficient string storage by sharing identical string values.
/// Multiple instances with the same content point to the same underlying memory.
/// Thread-safe, reference counted, and compatible with nlohmann::json.
class InternedString {
public:
  using value_type = std::string::value_type;

  InternedString() = default;
  /// \brief Construct from std::string.
  explicit InternedString(const std::string &v);
  /// \brief Construct from std::string (move).
  explicit InternedString(std::string &&v);
  /// \brief Construct from C-string literal.
  InternedString(const char *v);
  /// \brief Construct from string_view.
  explicit InternedString(std::string_view v);
  /// \brief Construct string of n copies of character c.
  InternedString(std::size_t n, char c);

  InternedString(const InternedString &other);
  InternedString &operator=(const InternedString &other);
  InternedString(InternedString &&other) noexcept;
  InternedString &operator=(InternedString &&other) noexcept;
  ~InternedString();

  /// \brief Get the underlying string value.
  const std::string &str() const;
  /// \brief Check if the string is empty.
  bool empty() const noexcept;
  /// \brief Get string length.
  std::size_t size() const noexcept;

  using const_iterator = std::string::const_iterator;
  const_iterator begin() const noexcept;
  const_iterator end() const noexcept;

  const char &operator[](std::size_t pos) const;
  char &operator[](std::size_t pos);
  const char &back() const;
  const char *c_str() const;
  const char *data() const;

  /// \brief Clear the string (sets to empty interned string).
  void clear();

  void push_back(char c);
  InternedString &append(const char *s, std::size_t n);
  InternedString &operator+=(const InternedString &other);
  void resize(std::size_t n, char c = '\0');

  bool operator==(const InternedString &other) const noexcept;
  bool operator!=(const InternedString &other) const noexcept;
  friend bool operator<(const InternedString &lhs,
                        const InternedString &rhs) noexcept;

  /// \brief Get current pool size (for debugging).
  static std::size_t poolSize();
  /// \brief Get reference count for this string.
  std::size_t refCount() const noexcept;
  /// \brief Display all strings in pool (for debugging).
  static void displayPool();

#ifdef ACAV_ENABLE_STRING_STATS
  /// \brief Get comprehensive memory statistics.
  static StringInterningStats getStats();
  /// \brief Print statistics to stderr.
  static void printStats(const char *label = nullptr);
  /// \brief Reset statistics counters (does not clear pool).
  static void resetStats();
#endif

private:
  // Internal storage: holds the actual string and reference count
  struct StringData {
    explicit StringData(const std::string &v);
    explicit StringData(std::string &&v);
    std::string value_;
    std::size_t refCount_ = 0;
  };

  // Hash functor for unordered_set with transparent lookup
  struct Hash {
    using is_transparent = void;
    std::size_t operator()(const StringData *s) const noexcept;
    std::size_t operator()(const std::string &s) const noexcept;
    std::size_t operator()(std::string_view s) const noexcept;
  };

  // Equality functor for unordered_set with transparent lookup
  struct Equal {
    using is_transparent = void;
    bool operator()(const StringData *a, const StringData *b) const noexcept;
    bool operator()(const StringData *a, const std::string &b) const noexcept;
    bool operator()(const std::string &a, const StringData *b) const noexcept;
    bool operator()(const StringData *a, std::string_view b) const noexcept;
    bool operator()(std::string_view a, const StringData *b) const noexcept;
  };

  // Release current data (decrement refcount, delete if zero)
  void release();

  // Find or create string in pool (thread-safe)
  static StringData *internString(const std::string &v);
  static StringData *internString(std::string &&v);
  static StringData *internString(std::string_view v);

  // Global pool of all interned strings (thread-safe)
  static std::unordered_set<StringData *, Hash, Equal> pool_;
  static std::mutex poolMutex_;

  // Pointer to the shared string data
  StringData *data_ = nullptr;
};

} // namespace acav

// Support for std::format (C++20)
#include <format>
namespace std {
template <> struct formatter<acav::InternedString> : formatter<string_view> {
  auto format(const acav::InternedString &value, format_context &ctx) const {
    return formatter<string_view>::format(value.str(), ctx);
  }
};
} // namespace std
