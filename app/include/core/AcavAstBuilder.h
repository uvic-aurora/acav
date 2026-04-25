/*$!{
* Aurora Clang AST Viewer (ACAV)
* 
* Copyright (c) 2026 Min Liu
* Copyright (c) 2026 Michael David Adams
* 
* SPDX-License-Identifier: GPL-2.0-or-later
* 
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License along
* with this program; if not, see <https://www.gnu.org/licenses/>.
}$!*/

/// \file AcavAstBuilder.h
/// \brief Clang AST to ACAV AST transformation.
#pragma once

#include "common/FileManager.h"
#include "core/AstNode.h"
#include "core/AstExtractorRunner.h"
#include <clang/AST/ASTContext.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/RecursiveASTVisitor.h>

namespace clang {
class ASTUnit;
class Decl;
class Stmt;
class Type;
class TypeLoc;
class Attr;
class ConceptReference;
class CXXBaseSpecifier;
class CXXCtorInitializer;
class LambdaCapture;
class NestedNameSpecifier;
class NestedNameSpecifierLoc;
class TemplateArgument;
class TemplateArgumentList;
class TemplateArgumentLoc;
class TemplateName;
} // namespace clang

namespace acav {

/// \brief Helper class for extracting rich type information from Clang types
class TypeInfoExtractor {
public:
  explicit TypeInfoExtractor(clang::ASTContext &ctx);

  /// \brief Extract comprehensive type information into JSON
  ///
  /// Adds the following properties:
  ///   - "spelledType": Type as written in source
  ///   - "canonicalType": Canonical type for comparisons
  ///   - "desugaredType": Type with typedefs/using expanded
  ///   - "isConst": Has const qualifier
  ///   - "isVolatile": Has volatile qualifier
  ///   - "isRestrict": Has restrict qualifier
  void extractTypeInfo(clang::QualType qt, AcavJson &properties) const;

  /// \brief Extract TypeLoc-specific information
  ///
  /// Adds:
  ///   - "typeLocClass": TypeLoc class name
  ///   - "hasLocalSourceRange": Whether TypeLoc has valid range
  void extractTypeLocInfo(clang::TypeLoc tl, AcavJson &properties) const;

  /// \brief Extract template argument information
  ///
  /// Adds:
  ///   - "templateArgs": Array of template argument details
  void extractTemplateArgs(const clang::TemplateArgumentList *args,
                           AcavJson &properties) const;

  /// \brief Extract single template argument details
  AcavJson extractTemplateArg(const clang::TemplateArgument &arg) const;

private:
  clang::ASTContext &ctx_;
  clang::PrintingPolicy policy_;

  /// \brief Helper to get qualifier string
  std::string getQualifierString(clang::Qualifiers quals) const;
};

/// \brief ACAV AST builder with comprehensive type extraction
///
/// Provides a high-level interface for building ACAV AST from Clang ASTUnit.
/// Implements RecursiveASTVisitor to traverse Clang AST and extract detailed
/// type information, template arguments, function signatures, etc.
class AcavAstBuilder {
public:
  /// \brief Build ACAV AST from Clang ASTUnit
  ///
  /// \param astUnit Loaded Clang ASTUnit
  /// \param context ACAV AST context (owns all nodes)
  /// \param fileManager File manager for source locations
  /// \param stats Output parameter for extraction statistics
  /// \param extractComments Whether to extract and include comments in AST
  /// \return Root node of built AST, or nullptr on failure
  static AstViewNode *buildFromASTUnit(clang::ASTUnit &astUnit,
                                       AstContext *context,
                                       FileManager &fileManager,
                                       AstExtractionStats &stats,
                                       bool extractComments = false);

private:
  // Implementation is hidden in .cpp file
  class Impl;
};

} // namespace acav
