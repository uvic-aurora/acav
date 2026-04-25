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

# Integration tests using LLVM Lit

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Find lit executable
find_program(LIT_EXECUTABLE
  NAMES lit lit.py
  PATHS
    ${LLVM_TOOLS_BINARY_DIR}
    ${Python3_SITELIB}/lit
  DOC "Path to lit test runner"
)

if(NOT LIT_EXECUTABLE)
  message(WARNING "lit not found. Integration tests will not be available.")
  message(WARNING "Install with: pip install lit")
  return()
endif()

message(STATUS "Found lit: ${LIT_EXECUTABLE}")

# Find FileCheck executable
find_program(FILECHECK_EXECUTABLE
  NAMES filecheck FileCheck
  PATHS
    ${LLVM_TOOLS_BINARY_DIR}
    /usr/bin
    /usr/local/bin
    /opt/homebrew/bin
  DOC "Path to FileCheck from LLVM"
)

if(FILECHECK_EXECUTABLE)
  get_filename_component(FILECHECK_DIR ${FILECHECK_EXECUTABLE} DIRECTORY)
  message(STATUS "Found FileCheck: ${FILECHECK_EXECUTABLE}")
  set(LLVM_TOOLS_BINARY_DIR ${FILECHECK_DIR})
else()
  message(WARNING "FileCheck not found. Tests may fail.")
  message(WARNING "Install filecheck to enable integration test")
endif()

# Get Clang resource directory
execute_process(
    COMMAND clang -print-resource-dir
    OUTPUT_VARIABLE CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get system SDK path (for macOS)
if(APPLE)
  execute_process(
      COMMAND xcrun --show-sdk-path
      OUTPUT_VARIABLE MACOS_SDK_PATH
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
  )
  if(MACOS_SDK_PATH)
    set(ISYSROOT_FLAG "-isysroot ${MACOS_SDK_PATH}")
  else()
    set(ISYSROOT_FLAG "")
  endif()
else()
  set(ISYSROOT_FLAG "")
endif()

# Configure the site configuration file
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  @ONLY
)

# Test dependencies (ensure tools are built first)
set(ACAV_TEST_DEPS
  query-dependencies
  # make_ast will be added when ready
)

# Add test targets
add_custom_target(check-acav
  COMMAND ${LIT_EXECUTABLE} -v ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${ACAV_TEST_DEPS}
  COMMENT "Running ACAV integration tests"
  USES_TERMINAL
)

# Add component-specific test targets
add_custom_target(check-acav-query-deps
  COMMAND ${LIT_EXECUTABLE} -v ${CMAKE_CURRENT_BINARY_DIR}/query-dependencies
  DEPENDS query-dependencies
  COMMENT "Running query_dependencies tests"
  USES_TERMINAL
)

add_custom_target(check-acav-make-ast
  COMMAND ${LIT_EXECUTABLE} -v ${CMAKE_CURRENT_BINARY_DIR}/make-ast
  DEPENDS make-ast
  COMMENT "Running make-ast tests"
  USES_TERMINAL
)

add_custom_target(check-acav-integration
  COMMAND ${LIT_EXECUTABLE} -v ${CMAKE_CURRENT_BINARY_DIR}/integration
  DEPENDS ${ACAV_TEST_DEPS}
  COMMENT "Running integration tests"
  USES_TERMINAL
)

# Integrate with CTest
add_test(
  NAME acav-lit-tests
  COMMAND ${LIT_EXECUTABLE} -v ${CMAKE_CURRENT_BINARY_DIR}
)

# Set test properties
set_tests_properties(acav-lit-tests PROPERTIES
  TIMEOUT 300
  LABELS "integration"
)
