#include "ui/AstModel.h"
#include "core/AstNode.h"
#include "core/SourceLocation.h"
#include <catch2/catch_test_macros.hpp>

using namespace acav;

TEST_CASE("AstModel root node visibility", "[AstModel]") {
  AstModel model;
  AstContext context;

  SourceLocation loc(1, 10, 5);
  SourceRange range(loc, loc);

  AcavJson rootProps;
  rootProps["kind"] = InternedString("TranslationUnitDecl");
  AstNode *rootNode = context.createAstNode(rootProps, range);
  AstViewNode *root = context.createAstViewNode(rootNode);

  SECTION("Empty model has no rows") {
    // Before setting root
    REQUIRE(model.rowCount(QModelIndex()) == 1);
    QModelIndex placeholderIndex = model.index(0, 0, QModelIndex());
    REQUIRE(placeholderIndex.isValid());
    REQUIRE(model.data(placeholderIndex, Qt::DisplayRole).toString() ==
            "No AST available (code not yet compiled).");
  }

  SECTION("Root node is visible as top-level item") {
    model.setRootNode(root);

    // Root should be visible (rowCount returns 1)
    REQUIRE(model.rowCount(QModelIndex()) == 1);

    // Can get index for root node
    QModelIndex rootIndex = model.index(0, 0, QModelIndex());
    REQUIRE(rootIndex.isValid());
    REQUIRE(rootIndex.row() == 0);
    REQUIRE(rootIndex.column() == 0);
  }

  SECTION("Root node index points to correct node") {
    model.setRootNode(root);

    QModelIndex rootIndex = model.index(0, 0, QModelIndex());
    REQUIRE(rootIndex.isValid());

    // Verify it displays the correct kind
    QVariant data = model.data(rootIndex, Qt::DisplayRole);
    REQUIRE(data.isValid());
    QString displayText = data.toString();
    REQUIRE(displayText == "TranslationUnitDecl");
  }

  SECTION("Root node has no parent") {
    model.setRootNode(root);

    QModelIndex rootIndex = model.index(0, 0, QModelIndex());
    REQUIRE(rootIndex.isValid());

    // Root node's parent should be invalid
    QModelIndex parentIndex = model.parent(rootIndex);
    REQUIRE_FALSE(parentIndex.isValid());
  }
}

TEST_CASE("AstModel root node with children", "[AstModel]") {
  AstModel model;
  AstContext context;

  SourceLocation loc(1, 10, 5);
  SourceRange range(loc, loc);

  // Create root
  AcavJson rootProps;
  rootProps["kind"] = InternedString("TranslationUnitDecl");
  AstNode *rootNode = context.createAstNode(rootProps, range);
  AstViewNode *root = context.createAstViewNode(rootNode);

  // Create children
  AcavJson child1Props;
  child1Props["kind"] = InternedString("FunctionDecl");
  child1Props["name"] = InternedString("main");
  AstNode *child1Node = context.createAstNode(child1Props, range);
  AstViewNode *child1 = context.createAstViewNode(child1Node);
  child1->setParent(root);
  root->addChild(child1);

  AcavJson child2Props;
  child2Props["kind"] = InternedString("VarDecl");
  child2Props["name"] = InternedString("globalVar");
  AstNode *child2Node = context.createAstNode(child2Props, range);
  AstViewNode *child2 = context.createAstViewNode(child2Node);
  child2->setParent(root);
  root->addChild(child2);

  AcavJson child3Props;
  child3Props["kind"] = InternedString("FunctionDecl");
  child3Props["name"] = InternedString("foo");
  AstNode *child3Node = context.createAstNode(child3Props, range);
  AstViewNode *child3 = context.createAstViewNode(child3Node);
  child3->setParent(root);
  root->addChild(child3);

  model.setRootNode(root);

  SECTION("Root has correct number of children") {
    QModelIndex rootIndex = model.index(0, 0, QModelIndex());
    REQUIRE(rootIndex.isValid());

    // Root should have 3 children
    REQUIRE(model.rowCount(rootIndex) == 3);
  }

  SECTION("Can access children through root") {
    QModelIndex rootIndex = model.index(0, 0, QModelIndex());
    REQUIRE(rootIndex.isValid());

    // Get first child
    QModelIndex child1Index = model.index(0, 0, rootIndex);
    REQUIRE(child1Index.isValid());
    QString child1Display = model.data(child1Index, Qt::DisplayRole).toString();
    REQUIRE(child1Display == "FunctionDecl main");

    // Get second child
    QModelIndex child2Index = model.index(1, 0, rootIndex);
    REQUIRE(child2Index.isValid());
    QString child2Display = model.data(child2Index, Qt::DisplayRole).toString();
    REQUIRE(child2Display == "VarDecl globalVar");

    // Get third child
    QModelIndex child3Index = model.index(2, 0, rootIndex);
    REQUIRE(child3Index.isValid());
    QString child3Display = model.data(child3Index, Qt::DisplayRole).toString();
    REQUIRE(child3Display == "FunctionDecl foo");
  }

  SECTION("Children's parent is root") {
    QModelIndex rootIndex = model.index(0, 0, QModelIndex());
    QModelIndex child1Index = model.index(0, 0, rootIndex);
    REQUIRE(child1Index.isValid());

    // Child's parent should be root
    QModelIndex parentIndex = model.parent(child1Index);
    REQUIRE(parentIndex.isValid());
    REQUIRE(parentIndex == rootIndex);
  }
}

TEST_CASE("AstModel root with no children", "[AstModel]") {
  AstModel model;
  AstContext context;

  SourceLocation loc(1, 10, 5);
  SourceRange range(loc, loc);

  AcavJson rootProps;
  rootProps["kind"] = InternedString("TranslationUnitDecl");
  AstNode *rootNode = context.createAstNode(rootProps, range);
  AstViewNode *root = context.createAstViewNode(rootNode);

  model.setRootNode(root);

  SECTION("Root is visible but has no children") {
    // Root should still be visible
    REQUIRE(model.rowCount(QModelIndex()) == 1);

    QModelIndex rootIndex = model.index(0, 0, QModelIndex());
    REQUIRE(rootIndex.isValid());

    // Root should have 0 children
    REQUIRE(model.rowCount(rootIndex) == 0);
  }
}

TEST_CASE("AstModel clear and reset", "[AstModel]") {
  AstModel model;
  AstContext context;

  SourceLocation loc(1, 10, 5);
  SourceRange range(loc, loc);

  AcavJson rootProps;
  rootProps["kind"] = InternedString("TranslationUnitDecl");
  AstNode *rootNode = context.createAstNode(rootProps, range);
  AstViewNode *root = context.createAstViewNode(rootNode);

  model.setRootNode(root);
  REQUIRE(model.rowCount(QModelIndex()) == 1);

  SECTION("Clear removes root") {
    model.clear();
    REQUIRE(model.rowCount(QModelIndex()) == 1);
    QModelIndex placeholderIndex = model.index(0, 0, QModelIndex());
    REQUIRE(placeholderIndex.isValid());
    REQUIRE(model.data(placeholderIndex, Qt::DisplayRole).toString() ==
            "No AST available (code not yet compiled).");
  }

  SECTION("Can set new root after clear") {
    model.clear();

    AcavJson newRootProps;
    newRootProps["kind"] = InternedString("TranslationUnitDecl");
    AstNode *newRootNode = context.createAstNode(newRootProps, range);
    AstViewNode *newRoot = context.createAstViewNode(newRootNode);

    model.setRootNode(newRoot);
    REQUIRE(model.rowCount(QModelIndex()) == 1);

    QModelIndex newRootIndex = model.index(0, 0, QModelIndex());
    REQUIRE(newRootIndex.isValid());
  }
}
