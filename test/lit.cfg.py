#! /usr/bin/env python3

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

import os
import platform

import lit.formats
import lit.util

# Test suite name
config.name = "ACAV AST Viewer"

# Test format (Shell Test)
config.test_format = lit.formats.ShTest(execute_external=True)

# Test file suffixes
config.suffixes = [".test"]

# Exclude directories
config.excludes = ["Inputs", "CMakeLists.txt"]

# Paths
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.acav_obj_root, "test")

# Tool substitutions
config.substitutions.append(
    ("%query-deps", os.path.join(config.acav_bin_dir, "query-dependencies"))
)
config.substitutions.append(
    ("%make-ast", os.path.join(config.acav_bin_dir, "make-ast"))
)

# FileCheck - try multiple locations
filecheck_path = None

# First, try the LLVM tools directory if provided
if hasattr(config, "llvm_tools_dir") and config.llvm_tools_dir:
    candidate = os.path.join(config.llvm_tools_dir, "filecheck")
    if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
        filecheck_path = candidate

# If not found, search in PATH
if not filecheck_path:
    import shutil

    filecheck_path = shutil.which("filecheck")

# Fall back to just 'FileCheck' (let the shell find it)
if not filecheck_path:
    filecheck_path = "filecheck"

config.substitutions.append(("%FileCheck", filecheck_path))

# Clang resource directory substitution
if hasattr(config, "clang_resource_dir"):
    config.substitutions.append(("%resource-dir", config.clang_resource_dir))

# System SDK flags (for macOS)
if hasattr(config, "isysroot_flag"):
    config.substitutions.append(("%isysroot-flag", config.isysroot_flag))
else:
    config.substitutions.append(("%isysroot-flag", ""))

# Platform features
if platform.system() != "Windows":
    config.available_features.add("unix")
if platform.system() == "Darwin":
    config.available_features.add("darwin")
if platform.system() == "Linux":
    config.available_features.add("linux")


# Check if tools are available
def is_tool_available(tool_name):
    tool_path = os.path.join(config.acav_bin_dir, tool_name)
    return os.path.exists(tool_path)


if is_tool_available("query-dependencies"):
    config.available_features.add("query-deps")
if is_tool_available("make-ast"):
    config.available_features.add("make-ast")
