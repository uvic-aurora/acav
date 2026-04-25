#include "common/FileManager.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

using namespace acav;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("FileManager basic functionality", "[file_manager][basic]") {
  FileManager mgr;

  SECTION("Initial state") {
    REQUIRE(mgr.getRegisteredFileCount() == 0);
    REQUIRE(!mgr.isValidFileId(FileManager::InvalidFileID));
    REQUIRE(!mgr.isValidFileId(1)); // No files registered yet
  }

  SECTION("Register a single file") {
    FileID id = mgr.registerFile("/tmp/test.cpp");

    REQUIRE(id != FileManager::InvalidFileID);
    REQUIRE(id == 1); // First file gets ID 1
    REQUIRE(mgr.isValidFileId(id));
    REQUIRE(mgr.getRegisteredFileCount() == 1);
    REQUIRE(mgr.getRefCount(id) == 1);

    // Path should be normalized (absolute)
    std::string_view path = mgr.getFilePath(id);
    REQUIRE(!path.empty());
    REQUIRE_THAT(std::string(path), ContainsSubstring("test.cpp"));
  }

  SECTION("Register multiple files") {
    FileID id1 = mgr.registerFile("/tmp/file1.cpp");
    FileID id2 = mgr.registerFile("/tmp/file2.cpp");
    FileID id3 = mgr.registerFile("/tmp/file3.h");

    REQUIRE(id1 == 1);
    REQUIRE(id2 == 2);
    REQUIRE(id3 == 3);
    REQUIRE(mgr.getRegisteredFileCount() == 3);

    REQUIRE(mgr.isValidFileId(id1));
    REQUIRE(mgr.isValidFileId(id2));
    REQUIRE(mgr.isValidFileId(id3));

    // All should have refCount of 1
    REQUIRE(mgr.getRefCount(id1) == 1);
    REQUIRE(mgr.getRefCount(id2) == 1);
    REQUIRE(mgr.getRefCount(id3) == 1);
  }

  SECTION("Register same file multiple times") {
    FileID id1 = mgr.registerFile("/tmp/duplicate.cpp");
    FileID id2 = mgr.registerFile("/tmp/duplicate.cpp");
    FileID id3 = mgr.registerFile("/tmp/duplicate.cpp");

    // Should return same ID
    REQUIRE(id1 == id2);
    REQUIRE(id2 == id3);

    // Only one file registered
    REQUIRE(mgr.getRegisteredFileCount() == 1);

    // Reference count should be 3
    REQUIRE(mgr.getRefCount(id1) == 3);
  }

  SECTION("Invalid FileID returns empty path") {
    std::string_view path = mgr.getFilePath(FileManager::InvalidFileID);
    REQUIRE(path.empty());
  }

  SECTION("Out of range FileID returns empty path") {
    std::string_view path = mgr.getFilePath(999);
    REQUIRE(path.empty());
  }

  SECTION("Invalid FileID has zero ref count") {
    REQUIRE(mgr.getRefCount(FileManager::InvalidFileID) == 0);
    REQUIRE(mgr.getRefCount(999) == 0);
  }
}

TEST_CASE("FileManager path normalization", "[file_manager][normalization]") {
  FileManager mgr;

  SECTION("Relative paths are normalized to absolute") {
    FileID id = mgr.registerFile("test.cpp");
    std::string_view path = mgr.getFilePath(id);

    REQUIRE(!path.empty());
    // Should be absolute path
    REQUIRE(path[0] == '/'); // Unix/macOS style
  }

  SECTION("Paths with . and .. are normalized") {
    FileID id1 = mgr.registerFile("/tmp/./test.cpp");
    FileID id2 = mgr.registerFile("/tmp/foo/../test.cpp");

    // Both should normalize to the same file
    std::string_view path1 = mgr.getFilePath(id1);
    std::string_view path2 = mgr.getFilePath(id2);

    // Should not contain . or ..
    REQUIRE(path1.find("/.") == std::string_view::npos);
    REQUIRE(path2.find("/.") == std::string_view::npos);
    REQUIRE(path2.find("/..") == std::string_view::npos);
  }

  SECTION("Different representations of same file get same ID") {
    // Create a test file
    std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    std::filesystem::path testFile = tempDir / "filemanager_test.txt";

    // Create the file
    {
      std::ofstream ofs(testFile);
      ofs << "test content";
    }

    // Register with different path representations
    FileID id1 = mgr.registerFile(testFile.string());

    // Change to temp directory and use relative path
    std::filesystem::path originalCwd = std::filesystem::current_path();
    std::filesystem::current_path(tempDir);

    FileID id2 = mgr.registerFile("filemanager_test.txt");

    // Restore original directory
    std::filesystem::current_path(originalCwd);

    // Should get same ID
    REQUIRE(id1 == id2);
    REQUIRE(mgr.getRegisteredFileCount() == 1);

    // Clean up
    std::filesystem::remove(testFile);
  }
}

TEST_CASE("FileManager tryGetFileId", "[file_manager][lookup]") {
  FileManager mgr;

  SECTION("Returns nullopt for unregistered file") {
    auto result = mgr.tryGetFileId("/tmp/not_registered.cpp");
    REQUIRE(!result.has_value());
  }

  SECTION("Returns FileID for registered file") {
    FileID registeredId = mgr.registerFile("/tmp/registered.cpp");

    auto result = mgr.tryGetFileId("/tmp/registered.cpp");
    REQUIRE(result.has_value());
    REQUIRE(result.value() == registeredId);
  }

  SECTION("Normalizes path before lookup") {
    FileID id = mgr.registerFile("/tmp/test.cpp");

    // Try with different path representations
    auto result1 = mgr.tryGetFileId("/tmp/./test.cpp");
    auto result2 = mgr.tryGetFileId("/tmp/foo/../test.cpp");

    REQUIRE(result1.has_value());
    REQUIRE(result2.has_value());
    REQUIRE(result1.value() == id);
    REQUIRE(result2.value() == id);
  }

  SECTION("Does not increment ref count") {
    FileID id = mgr.registerFile("/tmp/test.cpp");
    REQUIRE(mgr.getRefCount(id) == 1);

    // Multiple lookups shouldn't change ref count
    mgr.tryGetFileId("/tmp/test.cpp");
    mgr.tryGetFileId("/tmp/test.cpp");
    mgr.tryGetFileId("/tmp/test.cpp");

    REQUIRE(mgr.getRefCount(id) == 1);
  }
}

TEST_CASE("FileManager thread safety", "[file_manager][threading]") {
  FileManager mgr;

  SECTION("Concurrent registration of different files") {
    constexpr int NumThreads = 10;
    std::vector<std::thread> threads;
    std::vector<FileID> ids(NumThreads);

    for (int i = 0; i < NumThreads; ++i) {
      threads.emplace_back([&mgr, &ids, i]() {
        std::string path = "/tmp/thread_test_" + std::to_string(i) + ".cpp";
        ids[i] = mgr.registerFile(path);
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    // Should have NumThreads unique files
    REQUIRE(mgr.getRegisteredFileCount() == NumThreads);

    // All IDs should be unique and valid
    std::set<FileID> uniqueIds(ids.begin(), ids.end());
    REQUIRE(uniqueIds.size() == NumThreads);

    for (FileID id : ids) {
      REQUIRE(mgr.isValidFileId(id));
      REQUIRE(mgr.getRefCount(id) == 1);
    }
  }

  SECTION("Concurrent registration of same file") {
    constexpr int NumThreads = 100;
    std::vector<std::thread> threads;
    std::vector<FileID> ids(NumThreads);

    const std::string sharedPath = "/tmp/shared_file.cpp";

    for (int i = 0; i < NumThreads; ++i) {
      threads.emplace_back([&mgr, &ids, i, &sharedPath]() {
        ids[i] = mgr.registerFile(sharedPath);
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    // Should have only 1 file
    REQUIRE(mgr.getRegisteredFileCount() == 1);

    // All threads should get same ID
    FileID firstId = ids[0];
    for (FileID id : ids) {
      REQUIRE(id == firstId);
    }

    // Reference count should be NumThreads
    REQUIRE(mgr.getRefCount(firstId) == NumThreads);
  }
}

TEST_CASE("FileManager edge cases", "[file_manager][edge_cases]") {
  FileManager mgr;

  SECTION("Empty path") {
    FileID id = mgr.registerFile("");
    REQUIRE(mgr.isValidFileId(id));
    REQUIRE(mgr.getRegisteredFileCount() == 1);
  }

  SECTION("Path with spaces") {
    FileID id = mgr.registerFile("/tmp/file with spaces.cpp");
    REQUIRE(mgr.isValidFileId(id));

    std::string_view path = mgr.getFilePath(id);
    REQUIRE_THAT(std::string(path), ContainsSubstring("file with spaces.cpp"));
  }

  SECTION("Very long path") {
    std::string longPath = "/tmp/";
    for (int i = 0; i < 100; ++i) {
      longPath += "very_long_directory_name_";
    }
    longPath += "file.cpp";

    FileID id = mgr.registerFile(longPath);
    REQUIRE(mgr.isValidFileId(id));
  }

  SECTION("Path with special characters") {
    FileID id = mgr.registerFile("/tmp/file!@#$%^&*().cpp");
    REQUIRE(mgr.isValidFileId(id));

    std::string_view path = mgr.getFilePath(id);
    REQUIRE(!path.empty());
  }
}

TEST_CASE("FileManager FileID 0 is reserved", "[file_manager][invalid_id]") {
  FileManager mgr;

  SECTION("FileID 0 is invalid") {
    REQUIRE(FileManager::InvalidFileID == 0);
    REQUIRE(!mgr.isValidFileId(0));
  }

  SECTION("First registered file gets ID 1") {
    FileID id = mgr.registerFile("/tmp/first.cpp");
    REQUIRE(id == 1);
    REQUIRE(id != FileManager::InvalidFileID);
  }

  SECTION("Operations on InvalidFileID") {
    REQUIRE(mgr.getFilePath(FileManager::InvalidFileID).empty());
    REQUIRE(mgr.getRefCount(FileManager::InvalidFileID) == 0);
    REQUIRE(!mgr.isValidFileId(FileManager::InvalidFileID));
  }
}

TEST_CASE("FileManager path lifetime", "[file_manager][lifetime]") {
  FileManager mgr;

  SECTION("Returned string_view remains valid") {
    FileID id = mgr.registerFile("/tmp/lifetime_test.cpp");

    std::string_view path1 = mgr.getFilePath(id);
    std::string copy1(path1); // Make a copy

    // Register more files
    for (int i = 0; i < 100; ++i) {
      mgr.registerFile("/tmp/file_" + std::to_string(i) + ".cpp");
    }

    std::string_view path2 = mgr.getFilePath(id);
    std::string copy2(path2);

    // Original path should still be the same
    REQUIRE(copy1 == copy2);
  }
}
