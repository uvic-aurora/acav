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

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Collect all header files (needed for MOC)
set(APP_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/QueryDependenciesRunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/ParallelProcessRunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/QueryDependenciesParallelRunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/AppConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/MemoryProfiler.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/MakeAstRunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/LogEntry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/ProcessLogParser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/ProcessOutputUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/AstExtractorRunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/AcavAstBuilder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/AstNode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/SourceLocation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/SourceLocationIndex.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/MainWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/SourceCodeView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/CppSyntaxHighlighter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/LineNumberArea.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/TranslationUnitModel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/AstModel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/NodeCycleWidget.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/DeclContextView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/LogDock.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/OpenProjectDialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui/NodeDetailsDialog.h
    ${CMAKE_SOURCE_DIR}/include/common/DiagnosticLogFormat.h
)

# Collect all source files
set(APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/QueryDependenciesRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/ParallelProcessRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/QueryDependenciesParallelRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/AppConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MemoryProfiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MakeAstRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/AstExtractorRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/AcavAstBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/AstNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/SourceLocation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/SourceLocationIndex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/MainWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/SourceCodeView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/CppSyntaxHighlighter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/LineNumberArea.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/TranslationUnitModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/AstModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/NodeCycleWidget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/DeclContextView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/DockTitleBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/LogDock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/OpenProjectDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/NodeDetailsDialog.cpp
)

# Qt resource file for icons
set(APP_RESOURCES
    resources.qrc
)

acav_global_list_add(ACAV_LICENSABLE_CXX_FILES ${APP_HEADERS} ${APP_SOURCES})

# Create executable with both headers and sources
add_executable(acav ${APP_SOURCES} ${APP_HEADERS} ${APP_RESOURCES})

# Link libraries
target_link_libraries(acav
    PRIVATE
        common
        Qt6::Core
        Qt6::Widgets
        nlohmann_json::nlohmann_json
)

# Windows-specific library for memory profiling
if(WIN32)
    target_link_libraries(acav PRIVATE psapi)
endif()

# Set include directories
target_include_directories(acav
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include  # For common headers
        ${CMAKE_CURRENT_SOURCE_DIR}/include  # For app-local headers
)

# Set C++ standard
set_target_properties(acav PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
