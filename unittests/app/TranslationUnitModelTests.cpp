#include "ui/TranslationUnitModel.h"
#include "common/FileManager.h"
#include <QJsonArray>
#include <QJsonObject>
#include <catch2/catch_test_macros.hpp>

using acav::FileManager;
using acav::TranslationUnitModel;

static QJsonObject makeFileEntry(const QString &path) {
  QJsonObject obj;
  obj["path"] = path;
  obj["headerCount"] = 0;
  obj["headers"] = QJsonArray();
  return obj;
}

TEST_CASE("TranslationUnitModel deduplicates source files",
          "[TranslationUnitModel]") {
  FileManager fileManager;
  TranslationUnitModel model(fileManager);

  QJsonArray files;
  files.append(makeFileEntry(QStringLiteral("/tmp/AstNode.cpp")));
  files.append(makeFileEntry(QStringLiteral("/tmp/AstNode.cpp"))); // duplicate
  files.append(makeFileEntry(QStringLiteral("/tmp/Other.cpp")));

  QJsonObject deps;
  deps["files"] = files;

  model.populateFromDependencies(deps);

  // Model creates: "Project Files (tmp)" with two deduplicated files
  REQUIRE(model.rowCount() == 1); // Project Files root
  QStandardItem *projectRoot = model.item(0);
  REQUIRE(projectRoot != nullptr);
  REQUIRE(projectRoot->text().startsWith("Project Files"));
  REQUIRE(projectRoot->rowCount() == 2); // Two deduplicated files
  REQUIRE(projectRoot->child(0)->data(Qt::UserRole + 1).toString() ==
          QStringLiteral("/tmp/AstNode.cpp"));
  REQUIRE(projectRoot->child(1)->data(Qt::UserRole + 1).toString() ==
          QStringLiteral("/tmp/Other.cpp"));
}

TEST_CASE("TranslationUnitModel separates project and external files",
          "[TranslationUnitModel]") {
  FileManager fileManager;
  TranslationUnitModel model(fileManager);

  // Files in /home/user/project/src/ and /tmp/generated/
  // Project root is /home/user/project (simulating compile_commands.json location)
  // So src files are project files, /tmp file is external
  QJsonArray files;
  files.append(makeFileEntry(QStringLiteral("/home/user/project/src/main.cpp")));
  files.append(makeFileEntry(QStringLiteral("/home/user/project/src/utils.cpp")));
  files.append(makeFileEntry(QStringLiteral("/tmp/generated/auto.cpp")));

  QJsonObject deps;
  deps["files"] = files;

  // Project root = directory of compile_commands.json
  model.populateFromDependencies(deps, QStringLiteral("/home/user/project"));

  // Should have 2 root nodes: Project Files and External Files
  REQUIRE(model.rowCount() == 2);

  QStandardItem *projectRoot = model.item(0);
  QStandardItem *externalRoot = model.item(1);

  REQUIRE(projectRoot != nullptr);
  REQUIRE(externalRoot != nullptr);

  REQUIRE(projectRoot->text().startsWith("Project Files"));
  REQUIRE(externalRoot->text() == "External Files");

  // Project Files should have 1 directory (src) containing 2 files
  REQUIRE(projectRoot->rowCount() == 1); // "src" directory

  // External Files should have the /tmp path
  REQUIRE(externalRoot->rowCount() >= 1);
}

TEST_CASE("TranslationUnitModel user-specified project root overrides inference",
          "[TranslationUnitModel]") {
  FileManager fileManager;
  TranslationUnitModel model(fileManager);

  // All files are in /home/user/project/src/
  // Without override, project root would be /home/user/project/src
  // With override of /home/user, all files should still be project files
  QJsonArray files;
  files.append(makeFileEntry(QStringLiteral("/home/user/project/src/main.cpp")));
  files.append(makeFileEntry(QStringLiteral("/home/user/project/src/utils.cpp")));

  QJsonObject deps;
  deps["files"] = files;

  // Override project root to /home/user
  model.populateFromDependencies(deps, QStringLiteral("/home/user"));

  // Should have 1 root node: Project Files (user)
  REQUIRE(model.rowCount() == 1);
  QStandardItem *projectRoot = model.item(0);
  REQUIRE(projectRoot != nullptr);
  REQUIRE(projectRoot->text() == "Project Files (user)");
}

TEST_CASE("TranslationUnitModel picks majority cluster when files diverge",
          "[TranslationUnitModel]") {
  FileManager fileManager;
  TranslationUnitModel model(fileManager);

  // Files from different paths - /home has 2 files, /tmp has 1
  // Algorithm should pick /home/user as project root (majority)
  QJsonArray files;
  files.append(makeFileEntry(QStringLiteral("/home/user/a.cpp")));
  files.append(makeFileEntry(QStringLiteral("/home/user/b.cpp")));
  files.append(makeFileEntry(QStringLiteral("/tmp/c.cpp")));

  QJsonObject deps;
  deps["files"] = files;

  model.populateFromDependencies(deps);

  // Should have 2 roots: Project Files (/home/user) and External Files (/tmp)
  REQUIRE(model.rowCount() == 2);
  QStandardItem *projectRoot = model.item(0);
  QStandardItem *externalRoot = model.item(1);
  REQUIRE(projectRoot != nullptr);
  REQUIRE(externalRoot != nullptr);
  REQUIRE(projectRoot->text().startsWith("Project Files"));
  REQUIRE(externalRoot->text() == "External Files");
}

TEST_CASE("TranslationUnitModel tie-breaks using compilation db path",
          "[TranslationUnitModel]") {
  FileManager fileManager;
  TranslationUnitModel model(fileManager);

  // Equal files in /home and /tmp - tied (no common ancestor)
  // compile_commands.json is in /home/user/project/build/
  // So /home should be chosen as project root
  QJsonArray files;
  files.append(makeFileEntry(QStringLiteral("/home/user/project/src/a.cpp")));
  files.append(makeFileEntry(QStringLiteral("/tmp/generated/b.cpp")));

  QJsonObject deps;
  deps["files"] = files;

  // compile_commands.json in /home/user/project/build/
  model.populateFromDependencies(
      deps, QString(),
      QStringLiteral("/home/user/project/build/compile_commands.json"));

  // Should pick /home/user/project/src as project root (parent of compile db)
  // /tmp file should be external
  REQUIRE(model.rowCount() == 2);
  QStandardItem *projectRoot = model.item(0);
  QStandardItem *externalRoot = model.item(1);
  REQUIRE(projectRoot != nullptr);
  REQUIRE(externalRoot != nullptr);
  REQUIRE(projectRoot->text().contains("Project Files"));
  REQUIRE(externalRoot->text() == "External Files");
}
