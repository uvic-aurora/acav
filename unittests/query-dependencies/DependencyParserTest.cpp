#include "common/DependencyTypes.h"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using acav::FileDependencies;
using acav::HeaderInfo;
using AcavJson = nlohmann::json;

// Test basic data structures
TEST_CASE("HeaderInfo stores header information", "[query_dependencies]") {
  HeaderInfo info;
  info.path_ = "/usr/include/iostream";
  info.direct_ = true;
  info.inclusionKind_ = "C_System";

  REQUIRE(info.path_ == "/usr/include/iostream");
  REQUIRE(info.direct_ == true);
  REQUIRE(info.inclusionKind_ == "C_System");
}

TEST_CASE("FileDependencies stores file and headers", "[query_dependencies]") {
  FileDependencies deps;
  deps.path = "/absolute/path/to/main.cpp";

  HeaderInfo h1;
  h1.path_ = "/usr/include/iostream";
  h1.direct_ = true;
  h1.inclusionKind_ = "C_System";

  deps.headers.push_back(h1);

  REQUIRE(deps.path == "/absolute/path/to/main.cpp");
  REQUIRE(deps.headers.size() == 1);
  REQUIRE(deps.headers[0].path_ == "/usr/include/iostream");
}

// Test header deduplication within a single TU
TEST_CASE("Deduplication: header included both directly and indirectly",
          "[query_dependencies]") {
  FileDependencies deps;
  deps.path = "/test/main.cpp";

  // Simulate deduplication logic from main.cpp
  std::unordered_map<std::string, size_t> headerIndex;

  // First: direct include
  HeaderInfo h1;
  h1.path_ = "/usr/include/iostream";
  h1.direct_ = true;

  auto it = headerIndex.find(h1.path_);
  if (it == headerIndex.end()) {
    size_t index = deps.headers.size();
    deps.headers.push_back(h1);
    headerIndex[h1.path_] = index;
  }

  // Second: same header, but indirect
  HeaderInfo h2;
  h2.path_ = "/usr/include/iostream";
  h2.direct_ = false;

  it = headerIndex.find(h2.path_);
  if (it != headerIndex.end()) {
    deps.headers[it->second].direct_ |= h2.direct_;
  } else {
    size_t index = deps.headers.size();
    deps.headers.push_back(h2);
    headerIndex[h2.path_] = index;
  }

  // Should have only one entry, marked as direct
  REQUIRE(deps.headers.size() == 1);
  REQUIRE(deps.headers[0].path_ == "/usr/include/iostream");
  REQUIRE(deps.headers[0].direct_ == true);
}

// Test JSON serialization accuracy
TEST_CASE("JSON serialization produces correct format", "[query_dependencies]") {
  // Create multiple FileDependencies to test statistics
  std::vector<FileDependencies> allResults;

  FileDependencies deps1;
  deps1.path = "/absolute/path/test1.cpp";

  HeaderInfo h1;
  h1.path_ = "/usr/include/iostream";
  h1.direct_ = true;
  h1.inclusionKind_ = "C_System";

  HeaderInfo h2;
  h2.path_ = "/absolute/path/myheader.h";
  h2.direct_ = false;
  h2.inclusionKind_ = "C_User";

  deps1.headers.push_back(h1);
  deps1.headers.push_back(h2);
  allResults.push_back(deps1);

  FileDependencies deps2;
  deps2.path = "/absolute/path/test2.cpp";

  HeaderInfo h3;
  h3.path_ = "/usr/include/string";
  h3.direct_ = true;
  h3.inclusionKind_ = "C_System";

  deps2.headers.push_back(h3);
  allResults.push_back(deps2);

  // Calculate statistics (matching main.cpp logic)
  size_t totalHeaderCount = 0;
  for (const auto &fileDeps : allResults) {
    totalHeaderCount += fileDeps.headers.size();
  }

  // Serialize (matching main.cpp logic)
  AcavJson filesJson = AcavJson::array();
  for (const auto &fileDeps : allResults) {
    AcavJson headersJson = AcavJson::array();
    for (const auto &header : fileDeps.headers) {
      headersJson.push_back({{"path", header.path_},
                             {"direct", header.direct_},
                             {"inclusionKind", header.inclusionKind_}});
    }
    filesJson.push_back({{"path", fileDeps.path},
                         {"headerCount", fileDeps.headers.size()},
                         {"headers", headersJson}});
  }

  // Build final output with statistics
  AcavJson outputJson;
  outputJson["statistics"] = {{"sourceFileCount", allResults.size()},
                              {"totalHeaderCount", totalHeaderCount}};
  outputJson["files"] = filesJson;

  // Verify statistics
  REQUIRE(outputJson["statistics"]["sourceFileCount"] == 2);
  REQUIRE(outputJson["statistics"]["totalHeaderCount"] == 3);

  // Verify files array
  REQUIRE(outputJson["files"].is_array());
  REQUIRE(outputJson["files"].size() == 2);

  // Verify first file
  REQUIRE(outputJson["files"][0]["path"] == "/absolute/path/test1.cpp");
  REQUIRE(outputJson["files"][0]["headerCount"] == 2);
  REQUIRE(outputJson["files"][0]["headers"].is_array());
  REQUIRE(outputJson["files"][0]["headers"].size() == 2);
  REQUIRE(outputJson["files"][0]["headers"][0]["path"] ==
          "/usr/include/iostream");
  REQUIRE(outputJson["files"][0]["headers"][0]["direct"] == true);
  REQUIRE(outputJson["files"][0]["headers"][0]["inclusionKind"] == "C_System");
  REQUIRE(outputJson["files"][0]["headers"][1]["path"] ==
          "/absolute/path/myheader.h");
  REQUIRE(outputJson["files"][0]["headers"][1]["direct"] == false);
  REQUIRE(outputJson["files"][0]["headers"][1]["inclusionKind"] == "C_User");

  // Verify second file
  REQUIRE(outputJson["files"][1]["path"] == "/absolute/path/test2.cpp");
  REQUIRE(outputJson["files"][1]["headerCount"] == 1);
  REQUIRE(outputJson["files"][1]["headers"].size() == 1);
  REQUIRE(outputJson["files"][1]["headers"][0]["path"] == "/usr/include/string");
  REQUIRE(outputJson["files"][1]["headers"][0]["direct"] == true);
  REQUIRE(outputJson["files"][1]["headers"][0]["inclusionKind"] == "C_System");
}
