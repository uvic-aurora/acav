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
  ${CMAKE_CURRENT_SOURCE_DIR}/MakeAst.cpp
)

acav_global_list_add(ACAV_LICENSABLE_CXX_FILES ${CXX_SOURCES})

add_executable(make-ast ${CXX_SOURCES})

# Include common headers
target_include_directories(make-ast PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Link required libraries
# Note: Clang libraries are already provided by common
target_link_libraries(make-ast PRIVATE
    common
)
