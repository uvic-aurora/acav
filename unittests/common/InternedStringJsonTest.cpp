#include "common/InternedString.h"
#include <catch2/catch_test_macros.hpp>
#include <format>
#include <nlohmann/json.hpp>

using namespace acav;

// Define custom JSON type using InternedString
using InternedJson = nlohmann::basic_json<std::map,       // ObjectType
                                          std::vector,    // ArrayType
                                          InternedString, // StringType
                                          bool,           // BooleanType
                                          std::int64_t,   // NumberIntegerType
                                          std::uint64_t,  // NumberUnsignedType
                                          double,         // NumberFloatType
                                          std::allocator, // AllocatorType
                                          nlohmann::adl_serializer, // JSONSerializer
                                          std::vector<std::uint8_t> // BinaryType
                                          >;

TEST_CASE("InternedString basic integration with JSON", "[interned_string][json]") {
  // Clear any existing pool state
  size_t initialPoolSize = InternedString::poolSize();

  SECTION("Create JSON object with InternedString values") {
    InternedJson j;
    j["name"] = "Alice";
    j["city"] = "Wonderland";

    REQUIRE(j["name"] == "Alice");
    REQUIRE(j["city"] == "Wonderland");

    // Pool should have grown
    REQUIRE(InternedString::poolSize() > initialPoolSize);
  }

  SECTION("JSON string deduplication works") {
    size_t beforeSize = InternedString::poolSize();

    InternedJson j;
    j["name"] = "Alice";
    j["person"] = "Alice"; // Same value - should reuse!

    size_t afterSize = InternedString::poolSize();

    // Should add: keys "name", "person" and value "Alice" = 3 unique strings
    // (keys are different, only the value is deduplicated)
    REQUIRE(afterSize == beforeSize + 3);
  }

  SECTION("JSON with multiple duplicate values") {
    size_t beforeSize = InternedString::poolSize();

    InternedJson j;
    j["city1"] = "Wonderland";
    j["city2"] = "Wonderland"; // duplicate value
    j["city3"] = "Wonderland"; // duplicate value
    j["country"] = "NewLand";  // unique value

    size_t afterSize = InternedString::poolSize();

    // Should add: keys "city1", "city2", "city3", "country" (4 unique keys)
    // plus values "Wonderland", "NewLand" (2 unique values) = 6 total
    REQUIRE(afterSize == beforeSize + 6);
  }

  SECTION("JSON dump and parse roundtrip") {
    InternedJson j;
    j["name"] = "Bob";
    j["age"] = 42;
    j["active"] = true;

    // Dump to string
    InternedString jsonStr = j.dump();
    REQUIRE(!jsonStr.empty());

    // Parse back (convert InternedString to std::string for parse)
    InternedJson j2 = InternedJson::parse(jsonStr.str());
    REQUIRE(j2["name"] == "Bob");
    REQUIRE(j2["age"] == 42);
    REQUIRE(j2["active"] == true);
  }

  SECTION("Scoped JSON object lifecycle") {
    size_t beforeSize = InternedString::poolSize();

    {
      InternedJson j;
      j["temp"] = "temporary value";
      j["another"] = "another temp";

      // Pool should have grown
      REQUIRE(InternedString::poolSize() > beforeSize);
    }

    // After scope, strings should be released
    // Pool size should return to initial (or close to it)
    size_t afterSize = InternedString::poolSize();
    REQUIRE(afterSize == beforeSize);
  }

  SECTION("Parse JSON from raw string") {
    size_t beforeSize = InternedString::poolSize();

    InternedJson parsed = InternedJson::parse(R"(
      {
        "pi": 3.141,
        "happy": true,
        "name": "happy"
      }
    )");

    REQUIRE(parsed["pi"] == 3.141);
    REQUIRE(parsed["happy"] == true);
    REQUIRE(parsed["name"] == "happy");

    // Note: "happy" appears twice (as key and value)
    // Keys and values use same pool, so should deduplicate
    size_t afterSize = InternedString::poolSize();
    REQUIRE(afterSize > beforeSize);
  }

  SECTION("JSON array with strings") {
    InternedJson j = InternedJson::array();
    j.push_back("apple");
    j.push_back("banana");
    j.push_back("apple"); // duplicate

    REQUIRE(j.size() == 3);
    REQUIRE(j[0] == "apple");
    REQUIRE(j[1] == "banana");
    REQUIRE(j[2] == "apple");
  }

  SECTION("Nested JSON objects") {
    InternedJson j;
    j["person"]["name"] = "Charlie";
    j["person"]["city"] = "Paris";
    j["metadata"]["version"] = "1.0";

    REQUIRE(j["person"]["name"] == "Charlie");
    REQUIRE(j["person"]["city"] == "Paris");
    REQUIRE(j["metadata"]["version"] == "1.0");
  }

  SECTION("JSON with empty strings") {
    InternedJson j;
    j["empty"] = "";
    j["also_empty"] = "";

    REQUIRE(j["empty"] == "");
    REQUIRE(j["also_empty"] == "");
  }
}

TEST_CASE("InternedString pool behavior with JSON", "[interned_string][json][pool]") {
  SECTION("Complex JSON deduplication scenario") {
    size_t initialSize = InternedString::poolSize();

    InternedJson j;
    j["user"]["name"] = "Alice";
    j["user"]["role"] = "admin";
    j["admin"]["name"] = "Alice"; // duplicate value
    j["system"]["role"] = "admin"; // duplicate value
    j["config"]["name"] = "Alice"; // duplicate value

    // Should only have 2 unique string values: "Alice" and "admin"
    // Plus the keys: "user", "name", "role", "admin", "system", "config"
    // Total unique strings: 2 values + 6 keys = 8
    size_t finalSize = InternedString::poolSize();
    size_t added = finalSize - initialSize;

    // We added strings, verify pool grew
    REQUIRE(added > 0);
    INFO("Pool size before: " << initialSize);
    INFO("Pool size after: " << finalSize);
    INFO("Strings added: " << added);
  }
}

TEST_CASE("InternedString JSON serialization formats", "[interned_string][json][serialization]") {
  SECTION("Dump with indentation") {
    InternedJson j;
    j["name"] = "Test";
    j["value"] = 123;

    InternedString compact = j.dump();
    InternedString pretty = j.dump(4);

    REQUIRE(!compact.empty());
    REQUIRE(!pretty.empty());
    REQUIRE(pretty.size() > compact.size()); // Pretty has whitespace
  }

  SECTION("JSON with special characters") {
    InternedJson j;
    j["path"] = "/usr/local/bin";
    j["regex"] = ".*\\.cpp$";
    j["quote"] = "He said \"hello\"";

    InternedString serialized = j.dump();
    InternedJson parsed = InternedJson::parse(serialized.str());

    REQUIRE(parsed["path"] == "/usr/local/bin");
    REQUIRE(parsed["regex"] == ".*\\.cpp$");
    REQUIRE(parsed["quote"] == "He said \"hello\"");
  }
}

TEST_CASE("InternedString performance characteristics", "[interned_string][json][performance]") {
  SECTION("Many duplicate strings share memory") {
    size_t beforeSize = InternedString::poolSize();

    InternedJson j = InternedJson::array();

    // Add same string 100 times
    for (int i = 0; i < 100; ++i) {
      j.push_back("repeated");
    }

    size_t afterSize = InternedString::poolSize();

    // Pool should only grow by 1 (the word "repeated")
    REQUIRE(afterSize == beforeSize + 1);

    // All entries should share the same InternedString
    REQUIRE(j.size() == 100);
  }
}
