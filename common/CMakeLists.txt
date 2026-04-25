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

set(CXX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ClangUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/InternedString.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileManager.cpp
)

# Common library
add_library(common STATIC
  ${CXX_SOURCES}
)

acav_global_list_add(ACAV_LICENSABLE_CXX_FILES ${CXX_SOURCES})

# Include common headers
target_include_directories(common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies
# PUBLIC: propagate to targets that link against common
target_link_libraries(common PUBLIC
    ClangFoo::clangcpp
    ClangFoo::llvm
)
