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

#include "common/InternedString.h"

#include <iomanip>
#include <iostream>

namespace acav {

// Initialize static members
std::unordered_set<InternedString::StringData *, InternedString::Hash,
                   InternedString::Equal>
    InternedString::pool_;
std::mutex InternedString::poolMutex_;

// StringData constructors
InternedString::StringData::StringData(const std::string &v) : value_(v) {}

InternedString::StringData::StringData(std::string &&v)
    : value_(std::move(v)) {}

// Hash functors
std::size_t
InternedString::Hash::operator()(const StringData *s) const noexcept {
  return std::hash<std::string>()(s->value_);
}

std::size_t
InternedString::Hash::operator()(const std::string &s) const noexcept {
  return std::hash<std::string>()(s);
}

std::size_t
InternedString::Hash::operator()(std::string_view s) const noexcept {
  return std::hash<std::string_view>()(s);
}

// Equal functors
bool InternedString::Equal::operator()(const StringData *a,
                                       const StringData *b) const noexcept {
  return a->value_ == b->value_;
}

bool InternedString::Equal::operator()(const StringData *a,
                                       const std::string &b) const noexcept {
  return a->value_ == b;
}

bool InternedString::Equal::operator()(const std::string &a,
                                       const StringData *b) const noexcept {
  return a == b->value_;
}

bool InternedString::Equal::operator()(const StringData *a,
                                       std::string_view b) const noexcept {
  return a->value_ == b;
}

bool InternedString::Equal::operator()(std::string_view a,
                                       const StringData *b) const noexcept {
  return a == b->value_;
}

// Constructors
InternedString::InternedString(const std::string &v) {
  data_ = internString(v);
}

InternedString::InternedString(std::string &&v) {
  data_ = internString(std::move(v));
}

InternedString::InternedString(const char *v)
    : InternedString(std::string_view(v ? v : "")) {}

InternedString::InternedString(std::string_view v) { data_ = internString(v); }

InternedString::InternedString(std::size_t n, char c)
    : InternedString(std::string(n, c)) {}

InternedString::InternedString(const InternedString &other)
    : data_(other.data_) {
  if (data_) {
    std::lock_guard<std::mutex> lock(poolMutex_);
    ++data_->refCount_;
  }
}

InternedString &InternedString::operator=(const InternedString &other) {
  if (this != &other) {
    release();
    data_ = other.data_;
    if (data_) {
      std::lock_guard<std::mutex> lock(poolMutex_);
      ++data_->refCount_;
    }
  }
  return *this;
}

InternedString::InternedString(InternedString &&other) noexcept
    : data_(other.data_) {
  other.data_ = nullptr;
}

InternedString &InternedString::operator=(InternedString &&other) noexcept {
  if (this != &other) {
    release();
    data_ = other.data_;
    other.data_ = nullptr;
  }
  return *this;
}

InternedString::~InternedString() { release(); }

// Accessors
const std::string &InternedString::str() const {
  static const std::string empty;
  return data_ ? data_->value_ : empty;
}

bool InternedString::empty() const noexcept { return data_ == nullptr; }

std::size_t InternedString::size() const noexcept {
  return data_ ? data_->value_.size() : 0;
}

InternedString::const_iterator InternedString::begin() const noexcept {
  return str().begin();
}

InternedString::const_iterator InternedString::end() const noexcept {
  return str().end();
}

const char &InternedString::operator[](std::size_t pos) const {
  return str()[pos];
}

char &InternedString::operator[](std::size_t pos) {
  // Non-const access modifies the underlying string directly
  // This is required for JSON library compatibility
  // The string will be re-interned when necessary
  if (data_ && pos < data_->value_.size()) {
    return data_->value_[pos];
  }
  // Return reference to static dummy for out-of-range access
  static char dummy = '\0';
  return dummy;
}

const char &InternedString::back() const { return str().back(); }

const char *InternedString::c_str() const { return str().c_str(); }

const char *InternedString::data() const { return c_str(); }

void InternedString::clear() { release(); }

// Mutating operations for JSON compatibility
void InternedString::push_back(char c) {
  std::string temp = str();
  temp.push_back(c);
  *this = InternedString(std::move(temp));
}

InternedString &InternedString::append(const char *s, std::size_t n) {
  std::string temp = str();
  temp.append(s, n);
  *this = InternedString(std::move(temp));
  return *this;
}

InternedString &InternedString::operator+=(const InternedString &other) {
  if (other.empty()) {
    return *this;
  }
  if (empty()) {
    *this = other;
    return *this;
  }
  std::string combined = str() + other.str();
  *this = InternedString(std::move(combined));
  return *this;
}

void InternedString::resize(std::size_t n, char c) {
  std::string temp = str();
  temp.resize(n, c);
  *this = InternedString(std::move(temp));
}

// Comparison operators
bool InternedString::operator==(const InternedString &other) const noexcept {
  // Fast pointer comparison (strings are interned)
  return data_ == other.data_;
}

bool InternedString::operator!=(const InternedString &other) const noexcept {
  return !(*this == other);
}

bool operator<(const InternedString &lhs, const InternedString &rhs) noexcept {
  if (lhs.data_ == rhs.data_)
    return false;
  if (!lhs.data_)
    return rhs.data_ != nullptr;
  if (!rhs.data_)
    return false;
  return lhs.data_->value_ < rhs.data_->value_;
}

// Static methods
std::size_t InternedString::poolSize() {
  std::lock_guard<std::mutex> lock(poolMutex_);
  return pool_.size();
}

std::size_t InternedString::refCount() const noexcept {
  return data_ ? data_->refCount_ : 0;
}

void InternedString::displayPool() {
  std::lock_guard<std::mutex> lock(poolMutex_);
  for (const auto &v : pool_) {
    std::cout << v->value_ << " (refCount: " << v->refCount_ << ")\n";
  }
}

// Private methods
void InternedString::release() {
  if (data_) {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (--data_->refCount_ == 0) {
      pool_.erase(data_);
      delete data_;
    }
    data_ = nullptr;
  }
}

InternedString::StringData *InternedString::internString(const std::string &v) {
  std::lock_guard<std::mutex> lock(poolMutex_);
  auto it = pool_.find(v);
  if (it != pool_.end()) {
    ++(*it)->refCount_;
    return *it;
  }
  // Not found, create new
  auto *newData = new StringData(v);
  newData->refCount_ = 1;
  pool_.insert(newData);
  return newData;
}

InternedString::StringData *InternedString::internString(std::string &&v) {
  std::lock_guard<std::mutex> lock(poolMutex_);
  auto it = pool_.find(v);
  if (it != pool_.end()) {
    ++(*it)->refCount_;
    return *it;
  }
  // Not found, create new with move
  auto *newData = new StringData(std::move(v));
  newData->refCount_ = 1;
  pool_.insert(newData);
  return newData;
}

InternedString::StringData *InternedString::internString(std::string_view v) {
  std::lock_guard<std::mutex> lock(poolMutex_);
  auto it = pool_.find(v);
  if (it != pool_.end()) {
    ++(*it)->refCount_;
    return *it;
  }
  auto *newData = new StringData(std::string(v));
  newData->refCount_ = 1;
  pool_.insert(newData);
  return newData;
}

#ifdef ACAV_ENABLE_STRING_STATS

StringInterningStats InternedString::getStats() {
  std::lock_guard<std::mutex> lock(poolMutex_);

  StringInterningStats stats{};
  stats.uniqueStrings = pool_.size();

  for (const auto *data : pool_) {
    std::size_t strBytes = data->value_.size();
    std::size_t strCapacity = data->value_.capacity();

    // Memory used by the string data (actual allocation)
    stats.poolMemoryBytes += strCapacity;

    // Overhead per StringData: refCount_ + string object overhead
    stats.poolOverheadBytes += sizeof(StringData);

    // Total references
    stats.totalReferences += data->refCount_;

    // Without interning: each reference would have its own copy
    stats.withoutInterningBytes += data->refCount_ * (strCapacity + sizeof(std::string));
  }

  // Add hash table overhead (approximate)
  stats.poolOverheadBytes += pool_.bucket_count() * sizeof(void *);

  // Calculate savings
  std::size_t withInterning = stats.poolMemoryBytes + stats.poolOverheadBytes;
  if (stats.withoutInterningBytes > withInterning) {
    stats.savedBytes = stats.withoutInterningBytes - withInterning;
    stats.savingsPercent =
        100.0 * static_cast<double>(stats.savedBytes) /
        static_cast<double>(stats.withoutInterningBytes);
  } else {
    stats.savedBytes = 0;
    stats.savingsPercent = 0.0;
  }

  return stats;
}

void InternedString::printStats(const char *label) {
  auto stats = getStats();

  std::cerr << "\n";
  std::cerr << "========== String Interning Statistics";
  if (label) {
    std::cerr << " [" << label << "]";
  }
  std::cerr << " ==========\n";
  std::cerr << std::fixed << std::setprecision(2);
  std::cerr << "  Unique strings in pool:    " << stats.uniqueStrings << "\n";
  std::cerr << "  Total string references:   " << stats.totalReferences << "\n";
  std::cerr << "  Pool string data:          " << stats.poolMemoryBytes / 1024.0 << " KB\n";
  std::cerr << "  Pool overhead:             " << stats.poolOverheadBytes / 1024.0 << " KB\n";
  std::cerr << "  Total with interning:      "
            << (stats.poolMemoryBytes + stats.poolOverheadBytes) / 1024.0 << " KB\n";
  std::cerr << "  Without interning:         " << stats.withoutInterningBytes / 1024.0 << " KB\n";
  std::cerr << "  Memory saved:              " << stats.savedBytes / 1024.0 << " KB ("
            << stats.savingsPercent << "%)\n";
  std::cerr << "  Deduplication ratio:       ";
  if (stats.uniqueStrings > 0) {
    std::cerr << static_cast<double>(stats.totalReferences) / stats.uniqueStrings << "x\n";
  } else {
    std::cerr << "N/A\n";
  }
  std::cerr << "=======================================================\n\n";
}

void InternedString::resetStats() {
  // Currently no additional counters to reset beyond what's in the pool
  // This is a placeholder for future expansion (e.g., hit/miss counters)
}

#endif // ACAV_ENABLE_STRING_STATS

} // namespace acav
