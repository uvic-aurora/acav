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

cmake_minimum_required(VERSION 3.28)
project(cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_VERBOSE_MAKEFILE ON)

add_subdirectory(crtp)
add_subdirectory(modules)


