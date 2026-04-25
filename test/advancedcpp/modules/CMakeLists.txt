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

# Enable modules and scanning for this directory
#set(CMAKE_EXPERIMENTAL_CXX_MODULES ON)
#set(CMAKE_CXX_SCAN_FOR_MODULES ON)
#set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD ON)

add_executable(math math_main.cpp math_impl.cpp)

target_sources(math
  PUBLIC FILE_SET CXX_MODULES FILES math.cppm math_mul.cppm
)


