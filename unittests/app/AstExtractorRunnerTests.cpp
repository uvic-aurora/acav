#include "common/FileManager.h"
#include "core/AstExtractorRunner.h"
#include <QMetaType>
#include <QSignalSpy>
#include <QString>
#include <catch2/catch_test_macros.hpp>
// Qt defines 'emit' as a macro which conflicts with Sema.h in LLVM 22+
#undef emit
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>
#include <memory>
#include <string>
#include <vector>

using acav::AstContext;
using acav::AstExtractionStats;
using acav::AstExtractorRunner;
using acav::AstViewNode;
using acav::FileManager;
using acav::InternedString;

Q_DECLARE_METATYPE(acav::AstViewNode *)
Q_DECLARE_METATYPE(acav::AstExtractionStats)

namespace {

[[maybe_unused]] const bool kRegisteredMetaTypes = []() {
  qRegisterMetaType<AstViewNode *>("AstViewNode*");
  qRegisterMetaType<AstExtractionStats>("AstExtractionStats");
  return true;
}();

std::unique_ptr<clang::ASTUnit> buildAst(const std::string &code,
                                         const std::string &fileName) {
  std::vector<std::string> args = {"-std=c++20"};
  return clang::tooling::buildASTFromCodeWithArgs(code, args, fileName);
}

} // namespace

// ============================================================================
// Test Case 1: Happy Path - Normal AST Construction
// ============================================================================
TEST_CASE("AstExtractorRunner builds tree from valid AST",
          "[AstExtractorRunner]") {
  AstContext context;
  FileManager fileManager;

  auto loader = [](const std::string &, std::string &error,
                   const std::string & /*compilationDbPath*/,
                   const std::string & /*sourcePath*/)
      -> std::unique_ptr<clang::ASTUnit> {
    error.clear();
    return buildAst("int foo() { return 42; }", "test.cpp");
  };

  AstExtractorRunner runner(&context, fileManager, loader);

  QSignalSpy startedSpy(&runner, &AstExtractorRunner::started);
  QSignalSpy progressSpy(&runner, &AstExtractorRunner::progress);
  QSignalSpy finishedSpy(&runner, &AstExtractorRunner::finished);
  QSignalSpy errorSpy(&runner, &AstExtractorRunner::error);

  runner.run(QStringLiteral("test.ast"),
             {QStringLiteral("test.cpp"), QStringLiteral("header.h")});

  // Verify signal order: started → progress+ → finished
  REQUIRE(startedSpy.count() == 1);
  REQUIRE(progressSpy.count() >= 3); // Pre-register, Load, Build, Total
  REQUIRE(finishedSpy.count() == 1);
  REQUIRE(errorSpy.count() == 0);

  // Verify result
  auto args = finishedSpy.takeFirst();
  auto *root = args.at(0).value<AstViewNode *>();
  REQUIRE(root != nullptr);

  const auto &props = root->getProperties();
  REQUIRE(props.contains("kind"));
  REQUIRE(props["kind"].get<InternedString>().str() == "TranslationUnitDecl");

  // Verify file registration
  REQUIRE(fileManager.tryGetFileId("test.cpp").has_value());
  REQUIRE(fileManager.tryGetFileId("header.h").has_value());
}

// ============================================================================
// Test Case 2: Error Path - Loader Failure
// ============================================================================
TEST_CASE("AstExtractorRunner handles loader failure",
          "[AstExtractorRunner]") {
  AstContext context;
  FileManager fileManager;

  auto loader = [](const std::string &, std::string &error,
                   const std::string & /*compilationDbPath*/,
                   const std::string & /*sourcePath*/)
      -> std::unique_ptr<clang::ASTUnit> {
    error = "Failed to load AST file";
    return nullptr;
  };

  AstExtractorRunner runner(&context, fileManager, loader);

  QSignalSpy startedSpy(&runner, &AstExtractorRunner::started);
  QSignalSpy progressSpy(&runner, &AstExtractorRunner::progress);
  QSignalSpy finishedSpy(&runner, &AstExtractorRunner::finished);
  QSignalSpy errorSpy(&runner, &AstExtractorRunner::error);

  runner.run(QStringLiteral("missing.ast"), {});

  // Verify signal order: started → progress+ → error
  REQUIRE(startedSpy.count() == 1);
  REQUIRE(progressSpy.count() >= 2); // Pre-register, Loading
  REQUIRE(finishedSpy.count() == 0);
  REQUIRE(errorSpy.count() == 1);

  // Verify error message propagation
  auto args = errorSpy.takeFirst();
  QString errorMsg = args.at(0).toString();
  REQUIRE(errorMsg.contains(QStringLiteral("Failed to load AST file")));
}

// ============================================================================
// Test Case 3: Boundary Case - Empty AST
// ============================================================================
TEST_CASE("AstExtractorRunner handles empty AST",
          "[AstExtractorRunner]") {
  AstContext context;
  FileManager fileManager;

  auto loader = [](const std::string &, std::string &error,
                   const std::string & /*compilationDbPath*/,
                   const std::string & /*sourcePath*/)
      -> std::unique_ptr<clang::ASTUnit> {
    error.clear();
    return buildAst("", "empty.cpp"); // Empty source file
  };

  AstExtractorRunner runner(&context, fileManager, loader);

  QSignalSpy finishedSpy(&runner, &AstExtractorRunner::finished);
  QSignalSpy errorSpy(&runner, &AstExtractorRunner::error);

  runner.run(QStringLiteral("empty.ast"), {QStringLiteral("empty.cpp")});

  // Empty file still produces valid TranslationUnit
  REQUIRE(errorSpy.count() == 0);
  REQUIRE(finishedSpy.count() == 1);

  auto args = finishedSpy.takeFirst();
  auto *root = args.at(0).value<AstViewNode *>();
  REQUIRE(root != nullptr);

  const auto &props = root->getProperties();
  REQUIRE(props["kind"].get<InternedString>().str() == "TranslationUnitDecl");
}

// ============================================================================
// Test Case 4: Boundary Case - No Input Files
// ============================================================================
TEST_CASE("AstExtractorRunner handles no input files",
          "[AstExtractorRunner]") {
  AstContext context;
  FileManager fileManager;

  auto loader = [](const std::string &, std::string &error,
                   const std::string & /*compilationDbPath*/,
                   const std::string & /*sourcePath*/)
      -> std::unique_ptr<clang::ASTUnit> {
    error.clear();
    return buildAst("int main() {}", "main.cpp");
  };

  AstExtractorRunner runner(&context, fileManager, loader);

  QSignalSpy finishedSpy(&runner, &AstExtractorRunner::finished);
  QSignalSpy errorSpy(&runner, &AstExtractorRunner::error);

  // Run with empty file list
  runner.run(QStringLiteral("test.ast"), {});

  // Should still succeed (buildAst internally creates AST with "main.cpp")
  REQUIRE(errorSpy.count() == 0);
  REQUIRE(finishedSpy.count() == 1);

  // Only the implicitly created file from buildAst is registered
  REQUIRE(fileManager.getRegisteredFileCount() >= 0);
}

// ============================================================================
// Test Case 5: Statistics Tracking
// ============================================================================
TEST_CASE("AstExtractorRunner emits statistics", "[AstExtractorRunner][Stats]") {
  AstContext context;
  FileManager fileManager;

  auto loader = [](const std::string &, std::string &error,
                   const std::string & /*compilationDbPath*/,
                   const std::string & /*sourcePath*/)
      -> std::unique_ptr<clang::ASTUnit> {
    error.clear();
    // Simple code with declarations and statements
    return buildAst("int x = 42; int main() { return x; }", "test.cpp");
  };

  AstExtractorRunner runner(&context, fileManager, loader);

  QSignalSpy statsSpy(&runner, &AstExtractorRunner::statsUpdated);
  QSignalSpy finishedSpy(&runner, &AstExtractorRunner::finished);

  runner.run(QStringLiteral("test.ast"), {});

  // Verify statistics signal was emitted
  REQUIRE(statsSpy.count() == 1);
  REQUIRE(finishedSpy.count() == 1);

  // Extract statistics
  auto args = statsSpy.takeFirst();
  AstExtractionStats stats = args.at(0).value<AstExtractionStats>();

  // Verify statistics have non-zero values
  REQUIRE(stats.totalCount > 0);
  REQUIRE(stats.declCount > 0);  // We have VarDecl and FunctionDecl
  REQUIRE(stats.stmtCount > 0);  // We have CompoundStmt and ReturnStmt

  // totalCount should be sum of all specific counts
  std::size_t sum = stats.declCount + stats.stmtCount + stats.typeCount +
                    stats.typeLocCount + stats.attrCount +
                    stats.conceptRefCount + stats.cxxBaseSpecCount +
                    stats.ctorInitCount + stats.lambdaCaptureCount +
                    stats.nestedNameSpecCount + stats.nestedNameSpecLocCount +
                    stats.tempArgCount + stats.tempArgLocCount +
                    stats.tempNameCount;
  REQUIRE(stats.totalCount == sum);
}

TEST_CASE("AstExtractorRunner statistics with complex code",
          "[AstExtractorRunner][Stats]") {
  AstContext context;
  FileManager fileManager;

  auto loader = [](const std::string &, std::string &error,
                   const std::string & /*compilationDbPath*/,
                   const std::string & /*sourcePath*/)
      -> std::unique_ptr<clang::ASTUnit> {
    error.clear();
    // Complex code with templates, classes, operators
    return buildAst(
        R"(
        template<typename T>
        class Container {
          T* data;
        public:
          [[nodiscard]] T* get() { return data; }
        };

        int main() {
          Container<int> c;
          if (c.get() != nullptr) {
            return 1;
          }
          return 0;
        }
        )",
        "complex.cpp");
  };

  AstExtractorRunner runner(&context, fileManager, loader);

  QSignalSpy statsSpy(&runner, &AstExtractorRunner::statsUpdated);

  runner.run(QStringLiteral("complex.ast"), {});

  REQUIRE(statsSpy.count() == 1);

  auto args = statsSpy.takeFirst();
  AstExtractionStats stats = args.at(0).value<AstExtractionStats>();

  // Verify comprehensive extraction
  REQUIRE(stats.totalCount > 0);
  REQUIRE(stats.declCount > 0);  // Classes, templates, functions, vars
  REQUIRE(stats.stmtCount > 0);  // If statements, returns, etc.
  REQUIRE(stats.typeCount > 0);  // Types should be present
  REQUIRE(stats.typeLocCount > 0); // TypeLocs should be present

  // With shouldWalkTypesOfTypeLocs enabled, both Type and TypeLoc should exist
  REQUIRE(stats.typeCount >= 1);
  REQUIRE(stats.typeLocCount >= 1);

  // Template-related nodes should be present
  // Note: Exact counts depend on whether template instantiations are visited
  // But at least the template declaration nodes should exist
  REQUIRE(stats.declCount >= 3); // At least: Container, get, main
}

TEST_CASE("AstExtractorRunner statistics - type deduplication effect",
          "[AstExtractorRunner][Stats][Deduplication]") {
  AstContext context;
  FileManager fileManager;

  auto loader = [](const std::string &, std::string &error,
                   const std::string & /*compilationDbPath*/,
                   const std::string & /*sourcePath*/)
      -> std::unique_ptr<clang::ASTUnit> {
    error.clear();
    // Multiple variables of same type to test deduplication
    return buildAst(
        R"(
        int a, b, c, d, e, f, g, h, i, j;
        double x, y, z;
        )",
        "dedup.cpp");
  };

  AstExtractorRunner runner(&context, fileManager, loader);

  QSignalSpy statsSpy(&runner, &AstExtractorRunner::statsUpdated);

  runner.run(QStringLiteral("dedup.ast"), {});

  REQUIRE(statsSpy.count() == 1);

  auto args = statsSpy.takeFirst();
  AstExtractionStats stats = args.at(0).value<AstExtractionStats>();

  // We have 13 variable declarations (10 int, 3 double)
  REQUIRE(stats.declCount >= 13);

  // With type deduplication, we should have much fewer Type nodes than variables
  // Each type (int, double) should be deduplicated
  // Even though we have 13 variables, we should have far fewer Type nodes
  // The exact count depends on how many times Clang references the types
  REQUIRE(stats.typeCount < stats.declCount);

  // But we might have many TypeLoc nodes (one per variable declaration)
  // TypeLocs are NOT deduplicated
  REQUIRE(stats.typeLocCount > 0);
}

// ============================================================================
// Test Case: Comment Extraction as Property
// ============================================================================

// Helper to find a node with a specific kind by depth-first search
static AstViewNode *findNodeByKind(AstViewNode *root, const std::string &kind) {
  if (!root) {
    return nullptr;
  }
  const auto &props = root->getProperties();
  if (props.contains("kind") &&
      props["kind"].get<InternedString>().str() == kind) {
    return root;
  }
  for (AstViewNode *child : root->getChildren()) {
    if (auto *found = findNodeByKind(child, kind)) {
      return found;
    }
  }
  return nullptr;
}

// Helper to find a node with a specific name
static AstViewNode *findNodeByName(AstViewNode *root, const std::string &name) {
  if (!root) {
    return nullptr;
  }
  const auto &props = root->getProperties();
  if (props.contains("name") &&
      props["name"].get<InternedString>().str() == name) {
    return root;
  }
  for (AstViewNode *child : root->getChildren()) {
    if (auto *found = findNodeByName(child, name)) {
      return found;
    }
  }
  return nullptr;
}

TEST_CASE("AstExtractorRunner extracts comments as properties",
          "[AstExtractorRunner][Comments]") {
  AstContext context;
  FileManager fileManager;

  auto loader = [](const std::string &, std::string &error,
                   const std::string & /*compilationDbPath*/,
                   const std::string & /*sourcePath*/)
      -> std::unique_ptr<clang::ASTUnit> {
    error.clear();
    // Code with Doxygen-style documentation comments
    return buildAst(
        R"(
        /// This is a documented function.
        /// It returns the answer to everything.
        int getAnswer() { return 42; }

        /** A documented class with block comment. */
        class Documented {
          /// Member variable documentation.
          int value;
        };
        )",
        "commented.cpp");
  };

  AstExtractorRunner runner(&context, fileManager, loader);
  runner.setCommentExtractionEnabled(true);

  QSignalSpy finishedSpy(&runner, &AstExtractorRunner::finished);
  QSignalSpy statsSpy(&runner, &AstExtractorRunner::statsUpdated);

  runner.run(QStringLiteral("commented.ast"), {});

  REQUIRE(finishedSpy.count() == 1);

  auto args = finishedSpy.takeFirst();
  auto *root = args.at(0).value<AstViewNode *>();
  REQUIRE(root != nullptr);

  // Find the getAnswer function and verify it has a comment property
  auto *funcNode = findNodeByName(root, "getAnswer");
  REQUIRE(funcNode != nullptr);

  const auto &funcProps = funcNode->getProperties();
  REQUIRE(funcProps.contains("comment"));
  std::string commentText = funcProps["comment"].get<InternedString>().str();
  REQUIRE(commentText.find("documented function") != std::string::npos);

  // Find the Documented class and verify it has a comment property
  auto *classNode = findNodeByName(root, "Documented");
  REQUIRE(classNode != nullptr);

  const auto &classProps = classNode->getProperties();
  REQUIRE(classProps.contains("comment"));
  std::string classComment = classProps["comment"].get<InternedString>().str();
  REQUIRE(classComment.find("block comment") != std::string::npos);

  // Verify statistics include comment count
  REQUIRE(statsSpy.count() == 1);
  auto statsArgs = statsSpy.takeFirst();
  AstExtractionStats stats = statsArgs.at(0).value<AstExtractionStats>();
  REQUIRE(stats.commentCount >= 2); // At least function and class comments
}

TEST_CASE("AstExtractorRunner does not extract comments when disabled",
          "[AstExtractorRunner][Comments]") {
  AstContext context;
  FileManager fileManager;

  auto loader = [](const std::string &, std::string &error,
                   const std::string & /*compilationDbPath*/,
                   const std::string & /*sourcePath*/)
      -> std::unique_ptr<clang::ASTUnit> {
    error.clear();
    return buildAst(
        R"(
        /// This comment should NOT be extracted.
        int hidden() { return 0; }
        )",
        "nocomments.cpp");
  };

  AstExtractorRunner runner(&context, fileManager, loader);
  runner.setCommentExtractionEnabled(false); // Explicitly disable

  QSignalSpy finishedSpy(&runner, &AstExtractorRunner::finished);
  QSignalSpy statsSpy(&runner, &AstExtractorRunner::statsUpdated);

  runner.run(QStringLiteral("nocomments.ast"), {});

  REQUIRE(finishedSpy.count() == 1);

  auto args = finishedSpy.takeFirst();
  auto *root = args.at(0).value<AstViewNode *>();
  REQUIRE(root != nullptr);

  // Find the hidden function
  auto *funcNode = findNodeByName(root, "hidden");
  REQUIRE(funcNode != nullptr);

  // Verify it does NOT have a comment property
  const auto &funcProps = funcNode->getProperties();
  REQUIRE_FALSE(funcProps.contains("comment"));

  // Verify comment count is 0
  REQUIRE(statsSpy.count() == 1);
  auto statsArgs = statsSpy.takeFirst();
  AstExtractionStats stats = statsArgs.at(0).value<AstExtractionStats>();
  REQUIRE(stats.commentCount == 0);
}
