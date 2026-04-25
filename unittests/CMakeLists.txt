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

# Unit tests using Catch2
# Each subdirectory contains tests for a specific component

# Helper function to add unit tests
function(add_acav_unittest target_name)
  add_executable(${target_name} ${ARGN})

  target_link_libraries(${target_name}
    PRIVATE
      Catch2::Catch2WithMain
  )

  # Add to CTest
  add_test(NAME ${target_name} COMMAND ${target_name})

  # Set test properties
  set_tests_properties(${target_name} PROPERTIES
    LABELS "unittest"
  )

  # Use Catch2 test discovery for better test reporting
  catch_discover_tests(${target_name})
endfunction()

# Add subdirectories for each component
add_subdirectory(query-dependencies)
add_subdirectory(make-ast)
add_subdirectory(common)
add_subdirectory(app)


