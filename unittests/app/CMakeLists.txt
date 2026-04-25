#$!{
# Aurora Clang AST Viewer (ACAV)
# 
# Copyright (c) 2026 Min Liu
# Copyright (c) 2026 Michael David Adams
# 
# SPDX-License-Identifier: GPL-2.0-or-later
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>.
#}$!

# Qt dependencies
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)

# App unit tests

# Enable Qt MOC for Qt classes
set(CMAKE_AUTOMOC ON)

# Add app source and header files needed by tests
set(APP_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/app/core/SourceLocation.cpp
    ${CMAKE_SOURCE_DIR}/app/core/AstNode.cpp
    ${CMAKE_SOURCE_DIR}/app/core/MemoryProfiler.cpp
    ${CMAKE_SOURCE_DIR}/app/include/core/MemoryProfiler.h
    ${CMAKE_SOURCE_DIR}/app/core/AppConfig.cpp
    ${CMAKE_SOURCE_DIR}/app/include/core/AppConfig.h
    ${CMAKE_SOURCE_DIR}/app/core/ParallelProcessRunner.cpp
    ${CMAKE_SOURCE_DIR}/app/include/core/ParallelProcessRunner.h
    ${CMAKE_SOURCE_DIR}/app/core/QueryDependenciesRunner.cpp
    ${CMAKE_SOURCE_DIR}/app/include/core/QueryDependenciesRunner.h
    ${CMAKE_SOURCE_DIR}/app/core/QueryDependenciesParallelRunner.cpp
    ${CMAKE_SOURCE_DIR}/app/include/core/QueryDependenciesParallelRunner.h
    ${CMAKE_SOURCE_DIR}/app/core/MakeAstRunner.cpp
    ${CMAKE_SOURCE_DIR}/app/include/core/MakeAstRunner.h
    ${CMAKE_SOURCE_DIR}/app/core/SourceLocationIndex.cpp
    ${CMAKE_SOURCE_DIR}/app/include/core/SourceLocationIndex.h
    ${CMAKE_SOURCE_DIR}/app/core/AstExtractorRunner.cpp
    ${CMAKE_SOURCE_DIR}/app/include/core/AstExtractorRunner.h
    ${CMAKE_SOURCE_DIR}/app/core/AcavAstBuilder.cpp
    ${CMAKE_SOURCE_DIR}/app/include/core/AcavAstBuilder.h
    ${CMAKE_SOURCE_DIR}/app/ui/TranslationUnitModel.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/TranslationUnitModel.h
    ${CMAKE_SOURCE_DIR}/app/ui/SourceCodeView.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/SourceCodeView.h
    ${CMAKE_SOURCE_DIR}/app/ui/CppSyntaxHighlighter.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/CppSyntaxHighlighter.h
    ${CMAKE_SOURCE_DIR}/app/ui/LineNumberArea.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/LineNumberArea.h
    ${CMAKE_SOURCE_DIR}/app/ui/AstModel.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/AstModel.h
    ${CMAKE_SOURCE_DIR}/app/ui/DockTitleBar.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/DockTitleBar.h
    ${CMAKE_SOURCE_DIR}/app/ui/LogDock.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/LogDock.h
    ${CMAKE_SOURCE_DIR}/app/ui/NodeCycleWidget.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/NodeCycleWidget.h
    ${CMAKE_SOURCE_DIR}/app/ui/DeclContextView.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/DeclContextView.h
    ${CMAKE_SOURCE_DIR}/app/ui/OpenProjectDialog.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/OpenProjectDialog.h
    ${CMAKE_SOURCE_DIR}/app/ui/NodeDetailsDialog.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/NodeDetailsDialog.h
    ${CMAKE_SOURCE_DIR}/app/ui/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/app/include/ui/MainWindow.h
)

add_executable(AppUnitTests
    TestMain.cpp
    AstNodeTests.cpp
    AstExtractorRunnerTests.cpp
    PropertyExtractionTests.cpp
    TranslationUnitModelTests.cpp
    SourceCodeViewTests.cpp
    AstModelTests.cpp
    UIUXTests.cpp
    MainWindowAstCacheRecoveryTests.cpp
    ${APP_TEST_SOURCES}
)

target_link_libraries(AppUnitTests
    PRIVATE
        common
        Catch2::Catch2
        Qt6::Core
        Qt6::Widgets
        Qt6::Test
        nlohmann_json::nlohmann_json
)

# Note: MemoryProfiler uses AppConfig, so we need to provide it
# Adding AppConfig source to APP_TEST_SOURCES would be complex due to its dependencies
# For now, AppConfig is already available through the common library or app includes

# Include directories
target_include_directories(AppUnitTests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/app/include
)

# Add test
add_test(NAME AppUnitTests COMMAND AppUnitTests)
set_tests_properties(AppUnitTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Set C++ standard
set_target_properties(AppUnitTests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
