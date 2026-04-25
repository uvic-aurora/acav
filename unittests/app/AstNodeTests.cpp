#include "common/FileManager.h"
#include "core/AstNode.h"
#include "core/SourceLocation.h"
#include <catch2/catch_test_macros.hpp>
#include <clang/AST/ASTContext.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>

using namespace acav;

TEST_CASE("SourceLocation construction", "[SourceLocation]") {
  SECTION("Valid SourceLocation") {
    SourceLocation loc(1, 10, 5);
    REQUIRE(loc.fileID() == 1);
    REQUIRE(loc.line() == 10);
    REQUIRE(loc.column() == 5);
    REQUIRE(loc.isValid());
  }

  SECTION("Invalid SourceLocation (FileID 0)") {
    SourceLocation loc(FileManager::InvalidFileID, 0, 0);
    REQUIRE(loc.fileID() == FileManager::InvalidFileID);
    REQUIRE_FALSE(loc.isValid());
  }
}

TEST_CASE("SourceRange construction", "[SourceRange]") {
  SourceLocation begin(1, 10, 5);
  SourceLocation end(1, 10, 15);

  SourceRange range(begin, end);

  REQUIRE(range.begin().fileID() == 1);
  REQUIRE(range.begin().line() == 10);
  REQUIRE(range.begin().column() == 5);

  REQUIRE(range.end().fileID() == 1);
  REQUIRE(range.end().line() == 10);
  REQUIRE(range.end().column() == 15);
}

TEST_CASE("AstNode properties", "[AstNode]") {
  AstContext context;

  SourceLocation begin(1, 10, 5);
  SourceLocation end(1, 10, 15);
  SourceRange range(begin, end);

  AcavJson properties;
  properties["kind"] = InternedString("FunctionDecl");
  properties["name"] = InternedString("main");
  properties["type"] = InternedString("int ()");

  AstNode *node = context.createAstNode(properties, range);

  SECTION("Property access") {
    const AcavJson &props = node->getProperties();
    REQUIRE(props["kind"].get<InternedString>().str() == "FunctionDecl");
    REQUIRE(props["name"].get<InternedString>().str() == "main");
    REQUIRE(props["type"].get<InternedString>().str() == "int ()");
  }

  SECTION("Source range access") {
    const SourceRange &nodeRange = node->getSourceRange();
    REQUIRE(nodeRange.begin().line() == 10);
    REQUIRE(nodeRange.begin().column() == 5);
    REQUIRE(nodeRange.end().column() == 15);
  }

  SECTION("Reference counting") {
    REQUIRE(node->getUseCount() == 0);
    node->hold();
    REQUIRE(node->getUseCount() == 1);
    node->hold();
    REQUIRE(node->getUseCount() == 2);
    node->release();
    REQUIRE(node->getUseCount() == 1);
    node->release();
    REQUIRE(node->getUseCount() == 0);
  }

  // context destructor will clean up node
}

TEST_CASE("AstViewNode tree structure", "[AstViewNode]") {
  AstContext context;

  SourceLocation loc(1, 10, 5);
  SourceRange range(loc, loc);

  AcavJson props1;
  props1["kind"] = InternedString("TranslationUnitDecl");
  AstNode *node1 = context.createAstNode(props1, range);

  AcavJson props2;
  props2["kind"] = InternedString("FunctionDecl");
  props2["name"] = InternedString("main");
  AstNode *node2 = context.createAstNode(props2, range);

  AcavJson props3;
  props3["kind"] = InternedString("CompoundStmt");
  AstNode *node3 = context.createAstNode(props3, range);

  SECTION("Tree construction") {
    AstViewNode *root = context.createAstViewNode(node1);
    AstViewNode *child1 = context.createAstViewNode(node2);
    AstViewNode *child2 = context.createAstViewNode(node3);

    REQUIRE(root->getChildren().empty());
    REQUIRE(root->getParent() == nullptr);

    root->addChild(child1);
    REQUIRE(root->getChildren().size() == 1);
    REQUIRE(child1->getParent() == root);

    child1->addChild(child2);
    REQUIRE(child1->getChildren().size() == 1);
    REQUIRE(child2->getParent() == child1);

    // No manual cleanup - context destructor handles it
  }

  SECTION("Convenience accessors") {
    AstViewNode *viewNode = context.createAstViewNode(node2);

    const AcavJson &props = viewNode->getProperties();
    REQUIRE(props["kind"].get<InternedString>().str() == "FunctionDecl");
    REQUIRE(props["name"].get<InternedString>().str() == "main");

    const SourceRange &viewRange = viewNode->getSourceRange();
    REQUIRE(viewRange.begin().line() == 10);

    // No manual delete - context handles it
  }

  // context destructor will clean up all nodes
}

TEST_CASE("AstViewNode reference counting", "[AstViewNode]") {
  AstContext context;

  SourceLocation loc(1, 10, 5);
  SourceRange range(loc, loc);

  AcavJson props;
  props["kind"] = InternedString("FunctionDecl");
  AstNode *astNode = context.createAstNode(props, range);

  SECTION("Single reference") {
    AstViewNode *viewNode = context.createAstViewNode(astNode);
    REQUIRE(astNode->getUseCount() == 1);
    // context will clean up when test ends
  }

  SECTION("Multiple references") {
    AstViewNode *viewNode1 = context.createAstViewNode(astNode);
    REQUIRE(astNode->getUseCount() == 1);

    AstViewNode *viewNode2 = context.createAstViewNode(astNode);
    REQUIRE(astNode->getUseCount() == 2);

    // context will clean up all nodes
  }

  // context destructor handles cleanup
}

TEST_CASE("InternedString in JSON", "[AstNode]") {
  AcavJson json;
  json["kind"] = InternedString("FunctionDecl");
  json["type"] = InternedString("int");

  // String deduplication
  InternedString str1("int");
  InternedString str2("int");
  REQUIRE(str1 == str2);  // Should be same pool entry

  SECTION("JSON retrieval") {
    InternedString kind = json["kind"].get<InternedString>();
    REQUIRE(kind.str() == "FunctionDecl");

    InternedString type = json["type"].get<InternedString>();
    REQUIRE(type.str() == "int");
  }
}

TEST_CASE("Type deduplication with same type pointer", "[AstContext][Deduplication]") {
  AstContext context;
  SourceLocation loc(1, 10, 5);
  SourceRange range(loc, loc);

  // Simulate canonical type pointers (using different addresses)
  const void *intTypePtr = reinterpret_cast<const void *>(0x1000);
  const void *doubleTypePtr = reinterpret_cast<const void *>(0x2000);

  SECTION("Same type pointer returns same AstNode") {
    AcavJson props1;
    props1["kind"] = InternedString("BuiltinType");
    props1["typeName"] = InternedString("int");

    AstNode *node1 = context.getOrCreateTypeNode(intTypePtr, props1, range);
    REQUIRE(node1 != nullptr);
    REQUIRE(context.getAstNodeCount() == 1);

    AcavJson props2;
    props2["kind"] = InternedString("BuiltinType");
    props2["typeName"] = InternedString("int");

    AstNode *node2 = context.getOrCreateTypeNode(intTypePtr, props2, range);
    REQUIRE(node2 != nullptr);

    // Should return the SAME node instance
    REQUIRE(node1 == node2);

    // Node count should still be 1 (deduplicated)
    REQUIRE(context.getAstNodeCount() == 1);
  }

  SECTION("Different type pointers return different AstNodes") {
    AcavJson intProps;
    intProps["kind"] = InternedString("BuiltinType");
    intProps["typeName"] = InternedString("int");

    AstNode *intNode = context.getOrCreateTypeNode(intTypePtr, intProps, range);

    AcavJson doubleProps;
    doubleProps["kind"] = InternedString("BuiltinType");
    doubleProps["typeName"] = InternedString("double");

    AstNode *doubleNode =
        context.getOrCreateTypeNode(doubleTypePtr, doubleProps, range);

    // Should be different nodes
    REQUIRE(intNode != doubleNode);

    // Should have 2 distinct nodes
    REQUIRE(context.getAstNodeCount() == 2);
  }

  SECTION("Multiple references to same type") {
    AcavJson props;
    props["kind"] = InternedString("BuiltinType");
    props["typeName"] = InternedString("int");

    // Create 5 references to the same type
    AstNode *node1 = context.getOrCreateTypeNode(intTypePtr, props, range);
    AstNode *node2 = context.getOrCreateTypeNode(intTypePtr, props, range);
    AstNode *node3 = context.getOrCreateTypeNode(intTypePtr, props, range);
    AstNode *node4 = context.getOrCreateTypeNode(intTypePtr, props, range);
    AstNode *node5 = context.getOrCreateTypeNode(intTypePtr, props, range);

    // All should be the same
    REQUIRE(node1 == node2);
    REQUIRE(node2 == node3);
    REQUIRE(node3 == node4);
    REQUIRE(node4 == node5);

    // Only 1 AstNode should exist
    REQUIRE(context.getAstNodeCount() == 1);
  }
}

TEST_CASE("Type deduplication with AstViewNodes",
          "[AstContext][Deduplication]") {
  AstContext context;
  SourceLocation loc(1, 10, 5);
  SourceRange range(loc, loc);

  const void *intTypePtr = reinterpret_cast<const void *>(0x1000);

  AcavJson props;
  props["kind"] = InternedString("BuiltinType");
  props["typeName"] = InternedString("int");

  // Create deduplicated type node
  AstNode *typeNode = context.getOrCreateTypeNode(intTypePtr, props, range);

  SECTION("Multiple AstViewNodes can reference same deduplicated AstNode") {
    AstViewNode *view1 = context.createAstViewNode(typeNode);
    AstViewNode *view2 = context.createAstViewNode(typeNode);
    AstViewNode *view3 = context.createAstViewNode(typeNode);

    // All view nodes point to same AstNode
    REQUIRE(view1->getNode() == typeNode);
    REQUIRE(view2->getNode() == typeNode);
    REQUIRE(view3->getNode() == typeNode);

    // AstNode count is still 1 (deduplicated)
    REQUIRE(context.getAstNodeCount() == 1);

    // But we have 3 AstViewNodes
    REQUIRE(context.getAstViewNodeCount() == 3);

    // Reference count should be 3
    REQUIRE(typeNode->getUseCount() == 3);
  }
}

TEST_CASE("AstContext statistics", "[AstContext]") {
  AstContext context;
  SourceLocation loc(1, 10, 5);
  SourceRange range(loc, loc);

  SECTION("Initial counts") {
    REQUIRE(context.getAstNodeCount() == 0);
    REQUIRE(context.getAstViewNodeCount() == 0);
  }

  SECTION("Node creation increments counts") {
    AcavJson props;
    props["kind"] = InternedString("FunctionDecl");

    AstNode *node1 = context.createAstNode(props, range);
    REQUIRE(context.getAstNodeCount() == 1);

    AstNode *node2 = context.createAstNode(props, range);
    REQUIRE(context.getAstNodeCount() == 2);

    AstViewNode *view1 = context.createAstViewNode(node1);
    REQUIRE(context.getAstViewNodeCount() == 1);

    AstViewNode *view2 = context.createAstViewNode(node2);
    REQUIRE(context.getAstViewNodeCount() == 2);
  }

  SECTION("Deduplication affects AstNode count but not AstViewNode count") {
    const void *typePtr = reinterpret_cast<const void *>(0x1000);

    AcavJson props;
    props["kind"] = InternedString("BuiltinType");

    // Create deduplicated type node 3 times
    AstNode *type1 = context.getOrCreateTypeNode(typePtr, props, range);
    AstNode *type2 = context.getOrCreateTypeNode(typePtr, props, range);
    AstNode *type3 = context.getOrCreateTypeNode(typePtr, props, range);

    // Only 1 AstNode created (deduplicated)
    REQUIRE(context.getAstNodeCount() == 1);

    // Create 3 view nodes
    AstViewNode *view1 = context.createAstViewNode(type1);
    AstViewNode *view2 = context.createAstViewNode(type2);
    AstViewNode *view3 = context.createAstViewNode(type3);

    // 3 AstViewNodes created
    REQUIRE(context.getAstViewNodeCount() == 3);
  }
}
