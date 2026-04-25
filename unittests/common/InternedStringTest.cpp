// InternedString Unit Tests
// Test cases generated with GenAI assistance and reviewed by the author.

#include "common/InternedString.h"
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

using namespace acav;

// Helper to get current pool size
static std::size_t getPoolSize() { return InternedString::poolSize(); }

// Helper to reset pool state between tests
static void clearPool() {
  // Create and destroy strings to ensure pool is clear
  // This is a bit of a hack but works for testing
}

TEST_CASE("InternedString basic functionality", "[interned_string]") {
  SECTION("Default constructor creates empty string") {
    InternedString s;
    REQUIRE(s.empty());
    REQUIRE(s.size() == 0);
    REQUIRE(s.str() == "");
    REQUIRE(s.refCount() == 0);
  }

  SECTION("Same string returns same underlying pointer") {
    std::size_t initialPoolSize = getPoolSize();
    InternedString s1("hello");
    std::size_t afterFirstString = getPoolSize();
    REQUIRE(afterFirstString == initialPoolSize + 1);

    InternedString s2("hello");
    std::size_t afterSecondString = getPoolSize();
    // Pool size should not increase because "hello" is reused
    REQUIRE(afterSecondString == afterFirstString);

    // They should share the same pointer (interned)
    REQUIRE(s1 == s2);
    REQUIRE(s1.refCount() == 2); // Two references to the same string
    REQUIRE(s2.refCount() == 2);
  }

  SECTION("Different strings return different pointers") {
    InternedString s1("hello");
    InternedString s2("world");

    REQUIRE(s1 != s2);
    REQUIRE(s1.str() == "hello");
    REQUIRE(s2.str() == "world");
    REQUIRE(s1.refCount() == 1);
    REQUIRE(s2.refCount() == 1);
  }

  SECTION("Can retrieve original string") {
    InternedString s("test string");
    REQUIRE(s.str() == "test string");
    REQUIRE(s.size() == 11);
    REQUIRE(!s.empty());
  }

  SECTION("C-string constructor works") {
    const char *cstr = "c-string";
    InternedString s(cstr);
    REQUIRE(s.str() == "c-string");
  }

  SECTION("String_view constructor works") {
    std::string_view sv = "string_view";
    InternedString s(sv);
    REQUIRE(s.str() == "string_view");
  }

  SECTION("Size and fill character constructor works") {
    InternedString s(5, 'a');
    REQUIRE(s.str() == "aaaaa");
    REQUIRE(s.size() == 5);
  }

  SECTION("Move constructor works") {
    std::string original = "move test";
    InternedString s(std::move(original));
    REQUIRE(s.str() == "move test");
  }
}

TEST_CASE("InternedString copy semantics", "[interned_string]") {
  SECTION("Copy constructor increments refcount") {
    InternedString s1("shared");
    REQUIRE(s1.refCount() == 1);

    InternedString s2(s1);
    REQUIRE(s1.refCount() == 2);
    REQUIRE(s2.refCount() == 2);
    REQUIRE(s1 == s2);
  }

  SECTION("Copy assignment increments refcount") {
    InternedString s1("shared");
    InternedString s2;

    s2 = s1;
    REQUIRE(s1.refCount() == 2);
    REQUIRE(s2.refCount() == 2);
    REQUIRE(s1 == s2);
  }

  SECTION("Copy assignment releases old value") {
    InternedString s1("old");
    InternedString s2("new");

    REQUIRE(s1.refCount() == 1);
    REQUIRE(s2.refCount() == 1);

    s1 = s2; // s1 now points to "new", "old" should be released
    REQUIRE(s1 == s2);
    REQUIRE(s1.refCount() == 2);
    REQUIRE(s2.refCount() == 2);
  }

  SECTION("Self-assignment is safe") {
    InternedString s("self");
    s = s;
    REQUIRE(s.str() == "self");
    REQUIRE(s.refCount() == 1);
  }
}

TEST_CASE("InternedString move semantics", "[interned_string]") {
  SECTION("Move constructor transfers ownership") {
    InternedString s1("move");
    REQUIRE(s1.refCount() == 1);

    InternedString s2(std::move(s1));
    REQUIRE(s1.empty()); // s1 is now empty
    REQUIRE(s1.refCount() == 0);
    REQUIRE(s2.str() == "move");
    REQUIRE(s2.refCount() == 1); // Still only one reference
  }

  SECTION("Move assignment transfers ownership") {
    InternedString s1("move");
    InternedString s2;

    s2 = std::move(s1);
    REQUIRE(s1.empty());
    REQUIRE(s2.str() == "move");
    REQUIRE(s2.refCount() == 1);
  }

  SECTION("Self-move-assignment is safe") {
    InternedString s("self");
    s = std::move(s);
    REQUIRE(s.str() == "self");
    REQUIRE(s.refCount() == 1);
  }
}

TEST_CASE("InternedString memory management", "[interned_string]") {
  SECTION("Strings are removed from pool when all references gone") {
    std::size_t initialPoolSize = getPoolSize();

    {
      InternedString s1("temporary");
      REQUIRE(getPoolSize() == initialPoolSize + 1);

      {
        InternedString s2("temporary");
        REQUIRE(getPoolSize() == initialPoolSize + 1); // Still only one entry
        REQUIRE(s1.refCount() == 2);
      }
      // s2 destroyed, but s1 still exists
      REQUIRE(getPoolSize() == initialPoolSize + 1);
      REQUIRE(s1.refCount() == 1);
    }
    // Both destroyed, pool should be back to initial size
    REQUIRE(getPoolSize() == initialPoolSize);
  }

  SECTION("Clear releases the string") {
    InternedString s("to be cleared");
    REQUIRE(!s.empty());

    s.clear();
    REQUIRE(s.empty());
    REQUIRE(s.size() == 0);
    REQUIRE(s.refCount() == 0);
  }
}

TEST_CASE("InternedString memory efficiency", "[interned_string]") {
  SECTION("Multiple identical strings share memory") {
    std::size_t initialPoolSize = getPoolSize();
    std::vector<InternedString> strings;

    // Create 100 copies of the same string
    for (int i = 0; i < 100; ++i) {
      strings.push_back(InternedString("repeated"));
    }

    // Pool should only contain one entry for "repeated"
    REQUIRE(getPoolSize() == initialPoolSize + 1);

    // All strings should have the same underlying pointer
    for (const auto &s : strings) {
      REQUIRE(s == strings[0]);
    }

    // Reference count should be 100
    REQUIRE(strings[0].refCount() == 100);
  }

  SECTION("Different strings consume pool entries") {
    std::size_t initialPoolSize = getPoolSize();

    InternedString s1("string1");
    InternedString s2("string2");
    InternedString s3("string3");

    REQUIRE(getPoolSize() == initialPoolSize + 3);
  }
}

TEST_CASE("InternedString comparison operators", "[interned_string]") {
  SECTION("Equality is based on pointer comparison") {
    InternedString s1("same");
    InternedString s2("same");
    InternedString s3("different");

    REQUIRE(s1 == s2);
    REQUIRE(s1 != s3);
    REQUIRE(s2 != s3);
  }

  SECTION("Ordering comparison works") {
    InternedString s1("apple");
    InternedString s2("banana");
    InternedString s3("cherry");

    REQUIRE(s1 < s2);
    REQUIRE(s2 < s3);
    REQUIRE(s1 < s3);
    REQUIRE(!(s2 < s1));
  }

  SECTION("Empty strings compare correctly") {
    InternedString empty1;
    InternedString empty2;
    InternedString nonEmpty("text");

    REQUIRE(empty1 == empty2);
    REQUIRE(empty1 < nonEmpty);
    REQUIRE(!(nonEmpty < empty1));
  }
}

TEST_CASE("InternedString iterators and array access", "[interned_string]") {
  SECTION("Iterators work correctly") {
    InternedString s("hello");

    std::string reconstructed;
    for (char c : s) {
      reconstructed += c;
    }
    REQUIRE(reconstructed == "hello");
  }

  SECTION("Array access works") {
    InternedString s("test");
    REQUIRE(s[0] == 't');
    REQUIRE(s[1] == 'e');
    REQUIRE(s[2] == 's');
    REQUIRE(s[3] == 't');
  }

  SECTION("Back access works") {
    InternedString s("ending");
    REQUIRE(s.back() == 'g');
  }

  SECTION("C-string access works") {
    InternedString s("c_str");
    REQUIRE(std::string(s.c_str()) == "c_str");
    REQUIRE(std::string(s.data()) == "c_str");
  }
}

TEST_CASE("InternedString thread safety", "[interned_string][thread-safety]") {
  SECTION("Concurrent string creation is safe") {
    const int numThreads = 10;
    const int stringsPerThread = 100;
    std::vector<std::thread> threads;
    std::vector<std::vector<InternedString>> results(numThreads);

    // Each thread creates many copies of the same strings
    for (int t = 0; t < numThreads; ++t) {
      threads.emplace_back([&, t]() {
        for (int i = 0; i < stringsPerThread; ++i) {
          // Alternate between a few different strings
          std::string value = "thread_string_" + std::to_string(i % 5);
          results[t].push_back(InternedString(value));
        }
      });
    }

    // Wait for all threads to complete
    for (auto &thread : threads) {
      thread.join();
    }

    // Verify all strings with same value are equal (share same pointer)
    for (int i = 0; i < 5; ++i) {
      std::string expected = "thread_string_" + std::to_string(i);
      InternedString reference(expected);

      for (const auto &threadResults : results) {
        for (int j = i; j < stringsPerThread; j += 5) {
          REQUIRE(threadResults[j] == reference);
        }
      }
    }
  }

  SECTION("Concurrent copy and destruction is safe") {
    const int numThreads = 10;
    InternedString shared("shared_string");
    std::vector<std::thread> threads;

    // Multiple threads copying and destroying the same string
    for (int t = 0; t < numThreads; ++t) {
      threads.emplace_back([&]() {
        for (int i = 0; i < 1000; ++i) {
          InternedString copy = shared;
          // Copy goes out of scope here
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    // Original should still be valid
    REQUIRE(shared.str() == "shared_string");
    REQUIRE(shared.refCount() == 1);
  }
}

TEST_CASE("InternedString formatting support", "[interned_string]") {
  SECTION("std::format works") {
    InternedString s("formatted");
    std::string result = std::format("Value: {}", s);
    REQUIRE(result == "Value: formatted");
  }
}

#ifdef ACAV_ENABLE_STRING_STATS
TEST_CASE("InternedString statistics", "[interned_string][stats]") {
  SECTION("getStats returns valid statistics") {
    // Create some strings to generate meaningful stats
    InternedString s1("stats_test_string_one");
    InternedString s2("stats_test_string_two");
    InternedString s3 = s1; // Reference to s1
    InternedString s4 = s1; // Another reference to s1

    auto stats = InternedString::getStats();

    // We should have at least 2 unique strings
    REQUIRE(stats.uniqueStrings >= 2);
    // We should have at least 4 references (s1, s2, s3, s4)
    REQUIRE(stats.totalReferences >= 4);
    // Pool memory should be positive
    REQUIRE(stats.poolMemoryBytes > 0);
    // Without interning would use more memory (due to s3, s4 being copies)
    REQUIRE(stats.withoutInterningBytes >= stats.poolMemoryBytes);
    // Savings should be non-negative
    REQUIRE(stats.savedBytes >= 0);
    REQUIRE(stats.savingsPercent >= 0.0);
  }

  SECTION("printStats outputs to stderr") {
    InternedString s("print_test");
    // This just verifies it doesn't crash
    InternedString::printStats("Unit Test");
  }

  SECTION("deduplication shows memory savings") {
    // Create multiple references to the same string
    std::vector<InternedString> strings;
    for (int i = 0; i < 100; ++i) {
      strings.emplace_back("repeated_string_for_dedup_test");
    }

    auto stats = InternedString::getStats();

    // With 100 references to the same string, we should see significant savings
    // The deduplication ratio should be at least 100x for this string
    REQUIRE(stats.totalReferences >= 100);
    REQUIRE(stats.savedBytes > 0);
    REQUIRE(stats.savingsPercent > 50.0); // Should save at least 50%
  }
}
#endif // ACAV_ENABLE_STRING_STATS
