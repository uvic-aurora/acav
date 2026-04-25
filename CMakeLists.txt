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

cmake_minimum_required(VERSION 3.20)
project(ACAV VERSION 1.0.0 LANGUAGES C CXX)

# =============================================================================
# Version Configuration
# =============================================================================
# Version can be provided via -DACAV_VERSION_STRING=...
# Falls back to git describe if not provided
if(NOT DEFINED ACAV_VERSION_STRING OR ACAV_VERSION_STRING STREQUAL "")
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE ACAV_VERSION_STRING
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    if(NOT GIT_DESCRIBE_RESULT EQUAL 0 OR ACAV_VERSION_STRING STREQUAL "")
      set(ACAV_VERSION_STRING "unknown")
    else()
      string(REGEX REPLACE "^(release/|refs/tags/)" "" ACAV_VERSION_STRING "${ACAV_VERSION_STRING}")
    endif()
  else()
    set(ACAV_VERSION_STRING "unknown")
  endif()
endif()

message(STATUS "ACAV version: ${ACAV_VERSION_STRING}")


# Generate Version.h from template
configure_file(
  ${CMAKE_SOURCE_DIR}/app/include/Version.h.in
  ${CMAKE_BINARY_DIR}/generated/Version.h
  @ONLY
)

# Make the generated header available to all targets
include_directories(${CMAKE_BINARY_DIR}/generated)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set runtime output directory for executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Add CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(AcavGlobalList)

# Find required packages - use ClangFoo module for better Clang integration
find_package(ClangFoo REQUIRED)

# LLVM version compatibility check (supported: 21-22)
set(ACAV_LLVM_MIN_VERSION 20)
set(ACAV_LLVM_MAX_VERSION 22)
if(LLVM_VERSION_MAJOR LESS ACAV_LLVM_MIN_VERSION OR
   LLVM_VERSION_MAJOR GREATER ACAV_LLVM_MAX_VERSION)
  message(FATAL_ERROR
    "ACAV requires LLVM ${ACAV_LLVM_MIN_VERSION}-${ACAV_LLVM_MAX_VERSION}, "
    "found LLVM ${LLVM_VERSION_MAJOR}")
endif()
message(STATUS "LLVM ${LLVM_VERSION_MAJOR} (supported: ${ACAV_LLVM_MIN_VERSION}-${ACAV_LLVM_MAX_VERSION})")

# Set up Qt
find_package(Qt6 COMPONENTS Core Widgets Gui REQUIRED)

# Set up Json lib using fetch content
include(FetchContent)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
DOWNLOAD_EXTRACT_TIMESTAMP true)
FetchContent_MakeAvailable(json)

# Testing configuration
option(ACAV_BUILD_TESTS "Build tests" ON)
# Sanitizer configuration
option(ACAV_ENABLE_ASAN "Enable AddressSanitizer and LeakSanitizer" OFF)
option(ACAV_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ACAV_ENABLE_TSAN "Enable ThreadSanitizer (mutually exclusive with ASAN)" OFF)
# Debug/profiling options
option(ACAV_ENABLE_STRING_STATS "Enable string interning statistics tracking" OFF)

# Generate Config.h from template
configure_file(
  ${CMAKE_SOURCE_DIR}/include/common/Config.h.in
  ${CMAKE_BINARY_DIR}/generated/Config.h
)

if(ACAV_ENABLE_ASAN AND ACAV_ENABLE_TSAN)
  message(FATAL_ERROR "ACAV_ENABLE_ASAN and ACAV_ENABLE_TSAN cannot be enabled at the same time")
endif()

function(add_sanitizer_flags)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(${ARGV} -fno-omit-frame-pointer)
    add_link_options(${ARGV})
  else()
    message(WARNING "Sanitizers requested but the compiler is not Clang or GCC; skipping flags.")
  endif()
endfunction()

if(ACAV_ENABLE_ASAN)
  if(APPLE)
    # LeakSanitizer is not supported on macOS Apple Silicon separately, 
    # and often not needed as ASAN includes basic leak detection.
    add_sanitizer_flags("-fsanitize=address")
  else()
    add_sanitizer_flags("-fsanitize=address" "-fsanitize=leak")
  endif()
  message(STATUS "AddressSanitizer enabled")
endif()

if(ACAV_ENABLE_UBSAN)
  add_sanitizer_flags("-fsanitize=undefined")
  message(STATUS "UndefinedBehaviorSanitizer enabled")
endif()

if(ACAV_ENABLE_TSAN)
  add_sanitizer_flags("-fsanitize=thread")
  message(STATUS "ThreadSanitizer enabled")
endif()

if(ACAV_BUILD_TESTS)
  # Set up Catch2 v3 only when tests are enabled
  find_package(Catch2 3 REQUIRED)

  # Enable CTest support
  enable_testing()

  # Add Catch2 CMake module for automatic test discovery
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)

  # Add test directories
  add_subdirectory(unittests)
  add_subdirectory(test)
endif()

# Documentation target (optional - requires Doxygen)
option(ACAV_BUILD_DOCS "Build documentation" OFF)
if(ACAV_BUILD_DOCS)
  find_package(Doxygen REQUIRED)

  # Check for Graphviz dot
  if(DOXYGEN_DOT_EXECUTABLE)
    set(DOXYGEN_HAVE_DOT "YES")
  else()
    set(DOXYGEN_HAVE_DOT "NO")
  endif()

  set(DOXYGEN_OUTPUT_DIR ${CMAKE_BINARY_DIR}/docs)
  configure_file(
    ${CMAKE_SOURCE_DIR}/docs/Doxyfile.in
    ${CMAKE_BINARY_DIR}/Doxyfile
    @ONLY
  )

  add_custom_target(docs
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${DOXYGEN_OUTPUT_DIR}/html
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DOXYGEN_OUTPUT_DIR}/html/images
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/docs/manual/images
            ${DOXYGEN_OUTPUT_DIR}/html/images
    COMMAND ${CMAKE_COMMAND} -E make_directory
            ${DOXYGEN_OUTPUT_DIR}/html/docs/manual/images
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/docs/manual/images
            ${DOXYGEN_OUTPUT_DIR}/html/docs/manual/images
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/LICENSE.txt
            ${DOXYGEN_OUTPUT_DIR}/html/LICENSE.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/NOTICE.txt
            ${DOXYGEN_OUTPUT_DIR}/html/NOTICE.txt
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
  )
endif()

# Add common library
add_subdirectory(common)

# Set the target
add_subdirectory(query-dependencies)
add_subdirectory(make-ast)
add_subdirectory(app)

################################################################################
# The following is only for maintanence of license header notices in the
# various source files.
################################################################################

# Licensed files with Bash/hash-style comments.
acav_global_list_add(ACAV_LICENSABLE_SCRIPT_FILES
  ${CMAKE_SOURCE_DIR}/cmake/AcavGlobalList.cmake
  ${CMAKE_SOURCE_DIR}/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/app/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/common/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/make-ast/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/query-dependencies/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/test/advancedcpp/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/test/advancedcpp/crtp/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/test/advancedcpp/modules/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/test/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/test/data/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/test/macro_namespace/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/unittests/app/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/unittests/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/unittests/common/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/unittests/make-ast/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/unittests/query-dependencies/CMakeLists.txt
  ${CMAKE_SOURCE_DIR}/Dockerfile.release
  ${CMAKE_SOURCE_DIR}/scripts/run_acav_container.sh
  ${CMAKE_SOURCE_DIR}/scripts/package-release.sh
  ${CMAKE_SOURCE_DIR}/scripts/build.sh
  ${CMAKE_SOURCE_DIR}/test/lit.site.cfg.py.in
  ${CMAKE_SOURCE_DIR}/scripts/run_demo
  ${CMAKE_SOURCE_DIR}/scripts/print_desktop_options
  ${CMAKE_SOURCE_DIR}/.github/workflows/deploy-manual.yml
  ${CMAKE_SOURCE_DIR}/.github/workflows/ci.yml
  ${CMAKE_SOURCE_DIR}/.dockerignore
  ${CMAKE_SOURCE_DIR}/test/lit.cfg.py
)

# Licensed files with C++-style comments not captured elsewhere.
acav_global_list_add(ACAV_LICENSABLE_CXX_FILES
  ${CMAKE_SOURCE_DIR}/include/common/Config.h.in
  ${CMAKE_SOURCE_DIR}/app/include/Version.h.in
  ${CMAKE_SOURCE_DIR}/include/common/ClangUtils.h
  ${CMAKE_SOURCE_DIR}/include/common/Config.h.in
  ${CMAKE_SOURCE_DIR}/include/common/DependencyTypes.h
  ${CMAKE_SOURCE_DIR}/include/common/DiagnosticLogFormat.h
  ${CMAKE_SOURCE_DIR}/include/common/FileManager.h
  ${CMAKE_SOURCE_DIR}/include/common/InternedString.h
  ${CMAKE_SOURCE_DIR}/app/include/ui/DockTitleBar.h
  ${CMAKE_SOURCE_DIR}/app/resources/style.qss
)

# Licensed files with HTML-style comments.
acav_global_list_add(ACAV_LICENSABLE_HTML_FILES
  ${CMAKE_SOURCE_DIR}/app/resources.qrc
)

# License header maintenance targets.
# These targets are for use by copyright holders only and require
# ari_licentia to be installed from the private repository.
acav_global_list_get(ACAV_LICENSABLE_CXX_FILES CXX_FILES)
acav_global_list_get(ACAV_LICENSABLE_SCRIPT_FILES SCRIPT_FILES)
acav_global_list_get(ACAV_LICENSABLE_HTML_FILES HTML_FILES)

set(ADD_HEADERS_CXX_COMMANDS)
foreach(FILE ${CXX_FILES})
  list(APPEND ADD_HEADERS_CXX_COMMANDS
    COMMAND ari-licentia add-header
      --no-create
      --header-file ${CMAKE_SOURCE_DIR}/NOTICE.txt
      --kind c++
      ${FILE}
  )
endforeach()

set(ADD_HEADERS_SCRIPT_COMMANDS)
foreach(FILE ${SCRIPT_FILES})
  list(APPEND ADD_HEADERS_SCRIPT_COMMANDS
    COMMAND ari-licentia add-header
      --no-create
      --header-file ${CMAKE_SOURCE_DIR}/NOTICE.txt
      --kind bash
      ${FILE}
  )
endforeach()

set(ADD_HEADERS_HTML_COMMANDS)
foreach(FILE ${HTML_FILES})
  list(APPEND ADD_HEADERS_HTML_COMMANDS
    COMMAND ari-licentia add-header
      --no-create
      --header-file ${CMAKE_SOURCE_DIR}/NOTICE.txt
      # The start delimiter is "<!--$!{\n"
      --start-delim "\\x3c\\x21--\\x24\\x21\\x7b\\n"
      # The end delimiter is "\n}$!-->"
      --end-delim "\\x7d\\x24\\x21\\x2d\\x2d\\x3e\\n"
      --line-prefix "\\x20\\x20"
      ${FILE}
  )
endforeach()

add_custom_target(add-headers-cxx
  ${ADD_HEADERS_CXX_COMMANDS}
  COMMENT "Adding license headers to C/C++ source files"
  VERBATIM
)

add_custom_target(add-headers-scripts
  ${ADD_HEADERS_SCRIPT_COMMANDS}
  COMMENT "Adding license headers to script and build files"
  VERBATIM
)

add_custom_target(add-headers-html
  ${ADD_HEADERS_HTML_COMMANDS}
  COMMENT "Adding license headers to HTML files"
  VERBATIM
)

add_custom_target(add-headers
  DEPENDS add-headers-cxx add-headers-scripts add-headers-html
  COMMENT "Adding license headers to all files"
)
