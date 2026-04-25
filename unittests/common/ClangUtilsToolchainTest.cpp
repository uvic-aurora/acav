#include "common/ClangUtils.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace {

bool contains(const std::vector<std::string> &values,
              const std::string &value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

std::size_t countValue(const std::vector<std::string> &values,
                       const std::string &value) {
  return static_cast<std::size_t>(
      std::count(values.begin(), values.end(), value));
}

} // namespace

TEST_CASE("Toolchain command adjustment forces ACAV resource dir",
          "[clang_utils][toolchain]") {
  std::string diagnostic;
  const std::vector<std::string> adjusted =
      acav::buildToolchainAdjustedCommandLine(
          {"/definitely/missing/custom++", "-resource-dir=/old/clang",
           "-resource-dir", "/older/clang", "-c", "sample.cpp"},
          "/new/clang", diagnostic);

  REQUIRE(diagnostic.empty());
  REQUIRE(contains(adjusted, "/definitely/missing/custom++"));
  REQUIRE(!contains(adjusted, "-resource-dir=/old/clang"));
  REQUIRE(!contains(adjusted, "/older/clang"));
  REQUIRE(countValue(adjusted, "-resource-dir") == 1);
  REQUIRE(contains(adjusted, "/new/clang"));
}

TEST_CASE("Toolchain command adjustment preserves explicit sysroot",
          "[clang_utils][toolchain]") {
  std::string diagnostic;
  const std::vector<std::string> adjusted =
      acav::buildToolchainAdjustedCommandLine(
          {"/usr/bin/c++", "-isysroot", "/explicit/sdk", "-c", "sample.cpp"},
          "/new/clang", diagnostic);

  REQUIRE(diagnostic.empty());
  REQUIRE(countValue(adjusted, "-isysroot") == 1);
  REQUIRE(contains(adjusted, "/explicit/sdk"));
  REQUIRE(contains(adjusted, "-resource-dir"));
  REQUIRE(contains(adjusted, "/new/clang"));
}

TEST_CASE("Toolchain command adjustment is platform scoped",
          "[clang_utils][toolchain]") {
  std::string diagnostic;
  const std::vector<std::string> adjusted =
      acav::buildToolchainAdjustedCommandLine(
          {"clang++", "-std=c++20", "-c", "sample.cpp"}, "/new/clang",
          diagnostic);

#ifdef __APPLE__
  if (diagnostic.empty()) {
    REQUIRE(contains(adjusted, "-isysroot"));
  }
#else
  REQUIRE(diagnostic.empty());
  REQUIRE(!contains(adjusted, "-isysroot"));
#endif
  REQUIRE(contains(adjusted, "-resource-dir"));
  REQUIRE(contains(adjusted, "/new/clang"));
}

TEST_CASE(
    "Toolchain command adjustment does not inject macOS SDK for iOS target",
    "[clang_utils][toolchain]") {
  std::string diagnostic;
  const std::vector<std::string> adjusted =
      acav::buildToolchainAdjustedCommandLine({"clang++", "-target",
                                               "arm64-apple-ios", "-std=c++20",
                                               "-c", "sample.cpp"},
                                              "/new/clang", diagnostic);

  REQUIRE(diagnostic.empty());
  REQUIRE(!contains(adjusted, "-isysroot"));
  REQUIRE(contains(adjusted, "-resource-dir"));
  REQUIRE(contains(adjusted, "/new/clang"));
}

TEST_CASE("Toolchain command adjustment does not inject macOS SDK for "
          "non-Apple target",
          "[clang_utils][toolchain]") {
  std::string diagnostic;
  const std::vector<std::string> adjusted =
      acav::buildToolchainAdjustedCommandLine(
          {"clang++", "--target=x86_64-linux-gnu", "-std=c++20", "-c",
           "sample.cpp"},
          "/new/clang", diagnostic);

  REQUIRE(diagnostic.empty());
  REQUIRE(!contains(adjusted, "-isysroot"));
  REQUIRE(contains(adjusted, "-resource-dir"));
  REQUIRE(contains(adjusted, "/new/clang"));
}
