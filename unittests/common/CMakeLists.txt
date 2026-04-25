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

# Unit tests for common/shared components

add_acav_unittest(CommonUnitTests
  InternedStringTest.cpp
  InternedStringJsonTest.cpp
  FileManagerTest.cpp
  ClangUtilsToolchainTest.cpp
)

# Link against required libraries
target_link_libraries(CommonUnitTests
  PRIVATE
    common
    nlohmann_json::nlohmann_json
)

# Include paths for shared headers
target_include_directories(CommonUnitTests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
