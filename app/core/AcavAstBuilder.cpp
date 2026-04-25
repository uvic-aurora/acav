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

#include "core/AcavAstBuilder.h"
// Qt defines 'emit' as a no-op macro (via QObject in AcavAstBuilder.h)
// which conflicts with Sema.h in LLVM 22+ (included transitively)
#undef emit
#include <cctype>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Comment.h>
#include <clang/AST/CommentVisitor.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclContextInternals.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/NestedNameSpecifier.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/TemplateName.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Support/raw_ostream.h>
#include <unordered_map>

namespace acav {

namespace {

InternedString exprValueKindName(clang::ExprValueKind kind) {
  switch (kind) {
  case clang::VK_PRValue:
    return InternedString("VK_PRValue");
  case clang::VK_LValue:
    return InternedString("VK_LValue");
  case clang::VK_XValue:
    return InternedString("VK_XValue");
  }
  return InternedString("VK_Unknown");
}

InternedString exprObjectKindName(clang::ExprObjectKind kind) {
  switch (kind) {
  case clang::OK_Ordinary:
    return InternedString("OK_Ordinary");
  case clang::OK_BitField:
    return InternedString("OK_BitField");
  case clang::OK_VectorComponent:
    return InternedString("OK_VectorComponent");
  case clang::OK_ObjCProperty:
    return InternedString("OK_ObjCProperty");
  case clang::OK_ObjCSubscript:
    return InternedString("OK_ObjCSubscript");
  case clang::OK_MatrixComponent:
    return InternedString("OK_MatrixComponent");
  }
  return InternedString("OK_Unknown");
}

InternedString varDeclInitStyleName(clang::VarDecl::InitializationStyle style) {
  switch (style) {
  case clang::VarDecl::CInit:
    return InternedString("CInit");
  case clang::VarDecl::CallInit:
    return InternedString("CallInit");
  case clang::VarDecl::ListInit:
    return InternedString("ListInit");
  case clang::VarDecl::ParenListInit:
    return InternedString("ParenListInit");
  }
  return InternedString("InitStyle_Unknown");
}

InternedString storageClassName(clang::StorageClass storageClass) {
  switch (storageClass) {
  case clang::SC_None:
    return InternedString("SC_None");
  case clang::SC_Extern:
    return InternedString("SC_Extern");
  case clang::SC_Static:
    return InternedString("SC_Static");
  case clang::SC_PrivateExtern:
    return InternedString("SC_PrivateExtern");
  case clang::SC_Auto:
    return InternedString("SC_Auto");
  case clang::SC_Register:
    return InternedString("SC_Register");
  }
  return InternedString("SC_Unknown");
}

InternedString linkageName(clang::Linkage linkage) {
  switch (linkage) {
  case clang::Linkage::Invalid:
    return InternedString("Invalid");
  case clang::Linkage::None:
    return InternedString("None");
  case clang::Linkage::Internal:
    return InternedString("Internal");
  case clang::Linkage::UniqueExternal:
    return InternedString("UniqueExternal");
  case clang::Linkage::VisibleNone:
    return InternedString("VisibleNone");
  case clang::Linkage::Module:
    return InternedString("Module");
  case clang::Linkage::External:
    return InternedString("External");
  }
  return InternedString("Unknown");
}

AcavJson sourceLocationToJson(const SourceLocation &loc) {
  AcavJson obj = AcavJson::object();
  obj["fileId"] = static_cast<uint64_t>(loc.fileID());
  obj["line"] = static_cast<uint64_t>(loc.line());
  obj["column"] = static_cast<uint64_t>(loc.column());
  return obj;
}

AcavJson sourceRangeToJson(const SourceRange &range) {
  AcavJson obj = AcavJson::object();
  obj["begin"] = sourceLocationToJson(range.begin());
  obj["end"] = sourceLocationToJson(range.end());
  return obj;
}

void maybeAddMacroSpellingRange(AcavJson &properties,
                                const clang::SourceRange &range,
                                const clang::SourceManager &sm,
                                FileManager &fileManager) {
  if (!range.isValid()) {
    return;
  }
  if (!range.getBegin().isMacroID() && !range.getEnd().isMacroID()) {
    return;
  }

  clang::SourceLocation spellingBegin = sm.getSpellingLoc(range.getBegin());
  clang::SourceLocation spellingEnd = sm.getSpellingLoc(range.getEnd());
  if (spellingBegin.isInvalid() || spellingEnd.isInvalid()) {
    return;
  }

  clang::SourceRange spellingRange(spellingBegin, spellingEnd);
  SourceRange auroraRange =
      SourceRange::fromClang(spellingRange, sm, fileManager);
  if (!auroraRange.begin().isValid()) {
    return;
  }

  properties["macroSpellingRange"] = sourceRangeToJson(auroraRange);
}

const char *getFullDeclClassName(const clang::Decl *decl) {
  if (!decl)
    return "Decl";
  switch (decl->getKind()) {
#define DECL(DERIVED, BASE)                                                    \
  case clang::Decl::DERIVED:                                                   \
    return #DERIVED "Decl";
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"
  }
  return "Decl";
}

const char *getFullTypeClassName(const clang::Type *type) {
  if (!type)
    return "Type";
  switch (type->getTypeClass()) {
#define ABSTRACT_TYPE(DERIVED, BASE)
#define TYPE(DERIVED, BASE)                                                    \
  case clang::Type::DERIVED:                                                   \
    return #DERIVED "Type";
#include "clang/AST/TypeNodes.inc"
  }
  return "Type";
}

} // namespace

TypeInfoExtractor::TypeInfoExtractor(clang::ASTContext &ctx)
    : ctx_(ctx), policy_(ctx.getLangOpts()) {
  // Control how types are stringified
  policy_.SuppressTagKeyword = false;
  policy_.Bool = true;
  policy_.UseVoidForZeroParams = true;
  policy_.TerseOutput = false;
  policy_.PolishForDeclaration = true;
  policy_.SuppressUnwrittenScope = false;
}

void TypeInfoExtractor::extractTypeInfo(clang::QualType qt,
                                        AcavJson &properties) const {
  if (qt.isNull()) {
    return;
  }

  // Spelled type (as written in source)
  std::string spelledType = qt.getAsString(policy_);
  properties["spelledType"] = InternedString(spelledType);

  // Canonical type (for comparisons and deduplication)
  clang::QualType canonicalType = qt.getCanonicalType();
  if (canonicalType != qt) {
    std::string canonicalStr = canonicalType.getAsString(policy_);
    properties["canonicalType"] = InternedString(canonicalStr);
  }

  // Desugared type (expand typedefs/using)
  clang::QualType desugared = qt.getDesugaredType(ctx_);
  if (desugared != qt && desugared != canonicalType) {
    std::string desugaredStr = desugared.getAsString(policy_);
    properties["desugaredType"] = InternedString(desugaredStr);
  }

  // Extract qualifiers
  clang::Qualifiers quals = qt.getQualifiers();
  if (quals.hasConst()) {
    properties["isConst"] = true;
  }
  if (quals.hasVolatile()) {
    properties["isVolatile"] = true;
  }
  if (quals.hasRestrict()) {
    properties["isRestrict"] = true;
  }

  // Type class for categorization
  const clang::Type *typePtr = qt.getTypePtr();
  if (typePtr) {
    properties["typeClass"] = InternedString(getFullTypeClassName(typePtr));
  }

  // Additional type-specific information
  if (const auto *ptrType = typePtr->getAs<clang::PointerType>()) {
    clang::QualType pointee = ptrType->getPointeeType();
    properties["pointeeType"] = InternedString(pointee.getAsString(policy_));
  } else if (const auto *refType = typePtr->getAs<clang::ReferenceType>()) {
    clang::QualType pointee = refType->getPointeeType();
    properties["referencedType"] = InternedString(pointee.getAsString(policy_));
    properties["isLValueReference"] =
        llvm::isa<clang::LValueReferenceType>(refType);
    properties["isRValueReference"] =
        llvm::isa<clang::RValueReferenceType>(refType);
  } else if (const auto *arrType = typePtr->getAsArrayTypeUnsafe()) {
    clang::QualType elemType = arrType->getElementType();
    properties["elementType"] = InternedString(elemType.getAsString(policy_));

    // For constant arrays, include size
    if (const auto *constArrType =
            llvm::dyn_cast<clang::ConstantArrayType>(arrType)) {
      properties["arraySize"] =
          static_cast<int64_t>(constArrType->getSize().getZExtValue());
    }
  } else if (const auto *funcType =
                 typePtr->getAs<clang::FunctionProtoType>()) {
    // Function type details
    clang::QualType returnType = funcType->getReturnType();
    properties["returnType"] = InternedString(returnType.getAsString(policy_));
    properties["numParams"] = static_cast<int64_t>(funcType->getNumParams());

    // Exception specification
    switch (funcType->getExceptionSpecType()) {
    case clang::EST_None:
      break;
    case clang::EST_NoThrow:
    case clang::EST_NoexceptTrue:
      properties["noexcept"] = true;
      break;
    case clang::EST_NoexceptFalse:
      properties["noexcept"] = false;
      break;
    default:
      properties["exceptionSpec"] =
          static_cast<int64_t>(funcType->getExceptionSpecType());
      break;
    }

    // Ref qualifier
    switch (funcType->getRefQualifier()) {
    case clang::RQ_None:
      break;
    case clang::RQ_LValue:
      properties["refQualifier"] = InternedString("&");
      break;
    case clang::RQ_RValue:
      properties["refQualifier"] = InternedString("&&");
      break;
    }
  } else if (const auto *recordType = typePtr->getAs<clang::RecordType>()) {
    if (auto *recordDecl = recordType->getDecl()) {
      properties["recordName"] = InternedString(recordDecl->getNameAsString());
    }
  } else if (const auto *enumType = typePtr->getAs<clang::EnumType>()) {
    if (auto *enumDecl = enumType->getDecl()) {
      properties["enumName"] = InternedString(enumDecl->getNameAsString());
    }
  } else if (const auto *typedefType = typePtr->getAs<clang::TypedefType>()) {
    if (auto *typedefDecl = typedefType->getDecl()) {
      clang::QualType underlying = typedefDecl->getUnderlyingType();
      properties["underlyingType"] =
          InternedString(underlying.getAsString(policy_));
    }
  } else if (const auto *autoType = typePtr->getAs<clang::AutoType>()) {
    if (autoType->isDeduced()) {
      clang::QualType deducedType = autoType->getDeducedType();
      properties["deducedType"] =
          InternedString(deducedType.getAsString(policy_));
    }
    properties["isDecltypeAuto"] = autoType->isDecltypeAuto();
  }
}

void TypeInfoExtractor::extractTypeLocInfo(clang::TypeLoc tl,
                                           AcavJson &properties) const {
  if (tl.isNull()) {
    return;
  }

  // TypeLoc class name
  const clang::Type *typePtr = tl.getTypePtr();
  if (typePtr) {
    properties["typeLocClass"] = InternedString(getFullTypeClassName(typePtr));
  }

  // Check if has valid local source range
  clang::SourceRange localRange = tl.getLocalSourceRange();
  properties["hasLocalSourceRange"] = localRange.isValid();
}

void TypeInfoExtractor::extractTemplateArgs(
    const clang::TemplateArgumentList *args, AcavJson &properties) const {
  if (!args || args->size() == 0) {
    return;
  }

  AcavJson argArray = AcavJson::array();
  for (unsigned i = 0; i < args->size(); ++i) {
    const clang::TemplateArgument &arg = args->get(i);
    argArray.push_back(extractTemplateArg(arg));
  }

  properties["templateArgs"] = argArray;
  properties["numTemplateArgs"] = static_cast<int64_t>(args->size());
}

AcavJson TypeInfoExtractor::extractTemplateArg(
    const clang::TemplateArgument &arg) const {
  AcavJson argInfo;

  // Argument kind
  argInfo["kind"] = static_cast<int64_t>(arg.getKind());

  switch (arg.getKind()) {
  case clang::TemplateArgument::Null:
    argInfo["kindName"] = InternedString("Null");
    break;

  case clang::TemplateArgument::Type:
    argInfo["kindName"] = InternedString("Type");
    {
      clang::QualType argType = arg.getAsType();
      if (!argType.isNull() && argType.getTypePtr()) {
        argInfo["value"] = InternedString(argType.getAsString(policy_));
      }
    }
    break;

  case clang::TemplateArgument::Declaration:
    argInfo["kindName"] = InternedString("Declaration");
    if (auto *decl = arg.getAsDecl()) {
      if (auto *namedDecl = llvm::dyn_cast<clang::NamedDecl>(decl)) {
        argInfo["declName"] = InternedString(namedDecl->getNameAsString());
      }
    }
    break;

  case clang::TemplateArgument::NullPtr:
    argInfo["kindName"] = InternedString("NullPtr");
    argInfo["value"] = InternedString("nullptr");
    break;

  case clang::TemplateArgument::Integral:
    argInfo["kindName"] = InternedString("Integral");
    {
      // APSInt access is safe - it's a value type
      argInfo["value"] = arg.getAsIntegral().getSExtValue();

      clang::QualType integralType = arg.getIntegralType();
      if (!integralType.isNull() && integralType.getTypePtr()) {
        argInfo["integralType"] =
            InternedString(integralType.getAsString(policy_));
      }
    }
    break;

  case clang::TemplateArgument::Template:
    argInfo["kindName"] = InternedString("Template");
    {
      clang::TemplateName templateName = arg.getAsTemplate();
      if (!templateName.isNull()) {
        std::string templateNameStr;
        llvm::raw_string_ostream os(templateNameStr);
        templateName.print(os, policy_);
        os.flush();
        if (!templateNameStr.empty()) {
          argInfo["value"] = InternedString(templateNameStr);
        }
      }
    }
    break;

  case clang::TemplateArgument::TemplateExpansion:
    argInfo["kindName"] = InternedString("TemplateExpansion");
    {
      clang::TemplateName templateName = arg.getAsTemplateOrTemplatePattern();
      if (!templateName.isNull()) {
        std::string templateNameStr;
        llvm::raw_string_ostream os(templateNameStr);
        templateName.print(os, policy_);
        os.flush();
        if (!templateNameStr.empty()) {
          argInfo["value"] = InternedString(templateNameStr);
        }
      }
    }
    break;

  case clang::TemplateArgument::Expression:
    argInfo["kindName"] = InternedString("Expression");
    // Expression details would require additional handling
    break;

  case clang::TemplateArgument::Pack:
    argInfo["kindName"] = InternedString("Pack");
    {
      unsigned packSize = arg.pack_size();
      argInfo["packSize"] = static_cast<int64_t>(packSize);

      // Only process packs with reasonable size to avoid potential issues
      if (packSize > 0 && packSize < 1000) {
        AcavJson packArgs = AcavJson::array();
        for (const auto &packArg : arg.pack_elements()) {
          // Recursively extract pack elements
          packArgs.push_back(extractTemplateArg(packArg));
        }
        argInfo["packElements"] = packArgs;
      }
    }
    break;

  case clang::TemplateArgument::StructuralValue:
    argInfo["kindName"] = InternedString("StructuralValue");
    // StructuralValue is a C++20 feature for non-type template parameters
    // Additional handling could be added here in the future
    break;
  }

  return argInfo;
}

std::string
TypeInfoExtractor::getQualifierString(clang::Qualifiers quals) const {
  std::string result;
  if (quals.hasConst())
    result += "const ";
  if (quals.hasVolatile())
    result += "volatile ";
  if (quals.hasRestrict())
    result += "restrict ";
  return result;
}

namespace {

std::string_view toStringView(llvm::StringRef ref) {
  return std::string_view(ref.data(), ref.size());
}

void maybeSetNamedDeclProperty(AcavJson &properties, const char *key,
                               const clang::NamedDecl *decl) {
  if (!decl) {
    return;
  }

  if (const clang::IdentifierInfo *identifier = decl->getIdentifier()) {
    properties[key] = InternedString(toStringView(identifier->getName()));
    return;
  }

  std::string nameStr = decl->getNameAsString();
  if (!nameStr.empty()) {
    properties[key] = InternedString(std::move(nameStr));
  }
}

AcavJson makeDeclContextEntry(const clang::Decl *decl,
                                AstViewNode *node = nullptr) {
  AcavJson entry;
  entry["kind"] = InternedString(getFullDeclClassName(decl));
  if (!decl) {
    return entry;
  }
  if (auto *namedDecl = llvm::dyn_cast<clang::NamedDecl>(decl)) {
    maybeSetNamedDeclProperty(entry, "name", namedDecl);
  }
  if (node) {
    entry["nodePtr"] = reinterpret_cast<uint64_t>(node);
  }
  return entry;
}

AcavJson buildDeclContextChain(
    const clang::DeclContext *start, const clang::Decl *self,
    const std::unordered_map<const clang::DeclContext *, AstViewNode *>
        &contextMap,
    AstViewNode *selfNode) {
  AcavJson chain = AcavJson::array();
  std::vector<const clang::DeclContext *> contexts;

  const clang::DeclContext *current = start;
  while (current) {
    contexts.push_back(current);
    current = current->getParent();
  }

  const clang::Decl *innermostContextDecl = nullptr;
  if (!contexts.empty()) {
    innermostContextDecl = llvm::dyn_cast<clang::Decl>(contexts.front());
  }

  for (auto it = contexts.rbegin(); it != contexts.rend(); ++it) {
    const clang::DeclContext *ctx = *it;
    if (auto *ctxDecl = llvm::dyn_cast<clang::Decl>(ctx)) {
      // Look up AstViewNode for this DeclContext
      AstViewNode *ctxNode = nullptr;
      auto nodeIt = contextMap.find(ctx);
      if (nodeIt != contextMap.end()) {
        ctxNode = nodeIt->second;
      }
      chain.push_back(makeDeclContextEntry(ctxDecl, ctxNode));
    } else {
      AcavJson entry;
      entry["kind"] = InternedString("DeclContext");
      chain.push_back(entry);
    }
  }

  if (self && self != innermostContextDecl) {
    chain.push_back(makeDeclContextEntry(self, selfNode));
  }

  return chain;
}

static void appendWithSpacing(std::string &out, llvm::StringRef text) {
  if (text.empty()) {
    return;
  }
  if (!out.empty()) {
    char last = out.back();
    char first = text.front();
    if (!std::isspace(static_cast<unsigned char>(last)) &&
        !std::isspace(static_cast<unsigned char>(first))) {
      out.push_back(' ');
    }
  }
  out.append(text.begin(), text.end());
}

static void collectCommentText(const clang::comments::Comment *comment,
                               std::string &out) {
  if (!comment) {
    return;
  }

  if (const auto *text =
          llvm::dyn_cast<clang::comments::TextComment>(comment)) {
    appendWithSpacing(out, text->getText());
    return;
  }

  if (const auto *verbatim =
          llvm::dyn_cast<clang::comments::VerbatimBlockLineComment>(comment)) {
    appendWithSpacing(out, verbatim->getText());
    return;
  }

  if (const auto *verbatimLine =
          llvm::dyn_cast<clang::comments::VerbatimLineComment>(comment)) {
    appendWithSpacing(out, verbatimLine->getText());
    return;
  }

  for (auto it = comment->child_begin(); it != comment->child_end(); ++it) {
    collectCommentText(*it, out);
  }

  if (llvm::isa<clang::comments::ParagraphComment>(comment)) {
    if (!out.empty() && out.back() != '\n') {
      out.push_back('\n');
    }
  }
}

class AcavAstVisitor : public clang::RecursiveASTVisitor<AcavAstVisitor> {
public:
  AcavAstVisitor(AstContext *context, FileManager &fm, clang::ASTContext &ctx,
                   clang::Preprocessor *preprocessor, bool extractComments)
      : context_(context), fileManager_(fm), astContext_(ctx),
        preprocessor_(preprocessor), extractComments_(extractComments),
        typeExtractor_(ctx) {}

  bool shouldVisitImplicitCode() const { return true; }
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldWalkTypesOfTypeLocs() const { return true; }
  bool shouldTraversePostOrder() const { return false; }

  bool TraverseDecl(clang::Decl *decl);
  bool TraverseStmt(clang::Stmt *stmt);
#if LLVM_VERSION_MAJOR >= 22
  bool TraverseType(clang::QualType qualType, bool TraverseQualifier = true);
  bool TraverseTypeLoc(clang::TypeLoc typeLoc, bool TraverseQualifier = true);
#else
  bool TraverseType(clang::QualType qualType);
  bool TraverseTypeLoc(clang::TypeLoc typeLoc);
#endif
  bool TraverseAttr(clang::Attr *attr);
  bool TraverseConceptReference(clang::ConceptReference *cr);
  bool TraverseCXXBaseSpecifier(const clang::CXXBaseSpecifier &spec);
  bool TraverseConstructorInitializer(clang::CXXCtorInitializer *init);
  bool TraverseLambdaCapture(clang::LambdaExpr *lambda,
                             const clang::LambdaCapture *capture,
                             clang::Expr *init);
#if LLVM_VERSION_MAJOR >= 22
  bool TraverseNestedNameSpecifier(clang::NestedNameSpecifier nns);
#else
  bool TraverseNestedNameSpecifier(clang::NestedNameSpecifier *nns);
#endif
  bool TraverseNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc loc);
  bool TraverseTemplateArgument(clang::TemplateArgument arg);
  bool TraverseTemplateArgumentLoc(clang::TemplateArgumentLoc loc);
  bool TraverseTemplateName(clang::TemplateName name);

  // Visit hooks (node creation + stack push). Traverse manages pop.
  bool VisitDecl(clang::Decl *decl);
  bool VisitStmt(clang::Stmt *stmt);
  bool VisitType(clang::Type *type);
  bool VisitTypeLoc(clang::TypeLoc typeLoc);
  bool VisitAttr(clang::Attr *attr);
  bool VisitConceptReference(clang::ConceptReference *cr);
  bool VisitCXXBaseSpecifier(const clang::CXXBaseSpecifier &spec);
  bool VisitCXXCtorInitializer(clang::CXXCtorInitializer *init);
  bool VisitLambdaCapture(const clang::LambdaCapture *capture);
  bool VisitNestedNameSpecifier(clang::NestedNameSpecifier *nns);
  bool VisitNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc loc);
  bool VisitTemplateArgument(clang::TemplateArgument arg);
  bool VisitTemplateArgumentLoc(clang::TemplateArgumentLoc loc);
  bool VisitTemplateName(clang::TemplateName name);

  AstViewNode *getRootNode() const { return root_; }
  const AstExtractionStats &getStats() const { return stats_; }

private:
  const InternedString &getCachedTypeString(clang::QualType type) {
    const void *key = type.getAsOpaquePtr();
    auto it = typeStringCache_.find(key);
    if (it != typeStringCache_.end()) {
      return it->second;
    }
    auto [insertIt, inserted] =
        typeStringCache_.try_emplace(key, InternedString(type.getAsString()));
    return insertIt->second;
  }

  void pushNode(AstViewNode *node) {
    if (!node) {
      return;
    }
    if (!root_) {
      root_ = node;
    } else if (!parentStack_.empty()) {
      parentStack_.back()->addChild(node);
    }
    parentStack_.push_back(node);
  }

  void popNode() {
    if (!parentStack_.empty()) {
      parentStack_.pop_back();
    }
  }

  AcavJson &currentProperties() {
    return parentStack_.back()->getNode()->getProperties();
  }

  AstViewNode *createNodeFromDecl(clang::Decl *decl);
  AstViewNode *createNodeFromStmt(clang::Stmt *stmt);
  AstViewNode *createNodeFromType(const clang::Type *type);
  AstViewNode *createNodeFromTypeLoc(clang::TypeLoc typeLoc);
  AstViewNode *createNodeFromAttr(clang::Attr *attr);
  AstViewNode *createNodeFromConceptRef(clang::ConceptReference *cr);
  AstViewNode *createNodeFromCXXBaseSpec(const clang::CXXBaseSpecifier &spec);
  AstViewNode *createNodeFromCtorInit(clang::CXXCtorInitializer *init);
  AstViewNode *createNodeFromLambdaCapture(const clang::LambdaCapture *capture);
#if LLVM_VERSION_MAJOR >= 22
  AstViewNode *createNodeFromNestedNameSpec(clang::NestedNameSpecifier nns);
#else
  AstViewNode *createNodeFromNestedNameSpec(clang::NestedNameSpecifier *nns);
#endif
  AstViewNode *
  createNodeFromNestedNameSpecLoc(clang::NestedNameSpecifierLoc loc);
  AstViewNode *createNodeFromTemplateArg(clang::TemplateArgument arg);
  AstViewNode *createNodeFromTemplateArgLoc(clang::TemplateArgumentLoc loc);
  AstViewNode *createNodeFromTemplateName(clang::TemplateName name);

  AstContext *context_;
  FileManager &fileManager_;
  clang::ASTContext &astContext_;
  clang::Preprocessor *preprocessor_;
  bool extractComments_;
  TypeInfoExtractor typeExtractor_;
  AstViewNode *root_ = nullptr;
  std::vector<AstViewNode *> parentStack_;
  AstExtractionStats stats_;
  llvm::DenseMap<const void *, InternedString> typeStringCache_;
  // Map DeclContext to AstViewNode for navigation in declaration context panel
  std::unordered_map<const clang::DeclContext *, AstViewNode *>
      declContextToNode_;
};

// Traverse and create implementations are identical to the prior version in
// AstExtractorRunner; keep behavior but centralized here.

bool AcavAstVisitor::TraverseDecl(clang::Decl *decl) {
  if (!decl) {
    return true;
  }
  AstViewNode *node = createNodeFromDecl(decl);
  if (!node) {
    return true;
  }
  // Extract comment as property on the Decl node
  if (extractComments_ && preprocessor_) {
    if (const auto *comment =
            astContext_.getCommentForDecl(decl, preprocessor_)) {
      std::string text;
      collectCommentText(comment, text);
      if (!text.empty()) {
        node->getNode()->getProperties()["comment"] = InternedString(text);
        ++stats_.commentCount;
      }
    }
  }
  // Track DeclContext -> AstViewNode mapping for navigation
  if (auto *dc = llvm::dyn_cast<clang::DeclContext>(decl)) {
    declContextToNode_[dc] = node;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseDecl(decl);
  popNode();
  return result;
}

bool AcavAstVisitor::TraverseStmt(clang::Stmt *stmt) {
  if (!stmt) {
    return true;
  }
  AstViewNode *node = createNodeFromStmt(stmt);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseStmt(stmt);
  popNode();
  return result;
}

#if LLVM_VERSION_MAJOR >= 22
bool AcavAstVisitor::TraverseType(clang::QualType qualType,
                                   bool TraverseQualifier) {
  if (qualType.isNull()) {
    return true;
  }
  const clang::Type *type = qualType.getTypePtr();
  AstViewNode *node = createNodeFromType(type);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseType(qualType, TraverseQualifier);
  popNode();
  return result;
}

bool AcavAstVisitor::TraverseTypeLoc(clang::TypeLoc typeLoc,
                                      bool TraverseQualifier) {
  if (typeLoc.isNull()) {
    return true;
  }
  AstViewNode *node = createNodeFromTypeLoc(typeLoc);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result =
      RecursiveASTVisitor::TraverseTypeLoc(typeLoc, TraverseQualifier);
  popNode();
  return result;
}
#else
bool AcavAstVisitor::TraverseType(clang::QualType qualType) {
  if (qualType.isNull()) {
    return true;
  }
  const clang::Type *type = qualType.getTypePtr();
  AstViewNode *node = createNodeFromType(type);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseType(qualType);
  popNode();
  return result;
}

bool AcavAstVisitor::TraverseTypeLoc(clang::TypeLoc typeLoc) {
  if (typeLoc.isNull()) {
    return true;
  }
  AstViewNode *node = createNodeFromTypeLoc(typeLoc);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseTypeLoc(typeLoc);
  popNode();
  return result;
}
#endif

bool AcavAstVisitor::TraverseAttr(clang::Attr *attr) {
  if (!attr) {
    return true;
  }
  AstViewNode *node = createNodeFromAttr(attr);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseAttr(attr);
  popNode();
  return result;
}

bool AcavAstVisitor::TraverseConceptReference(clang::ConceptReference *cr) {
  if (!cr) {
    return true;
  }
  AstViewNode *node = createNodeFromConceptRef(cr);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseConceptReference(cr);
  popNode();
  return result;
}

bool AcavAstVisitor::TraverseCXXBaseSpecifier(
    const clang::CXXBaseSpecifier &spec) {
  AstViewNode *node = createNodeFromCXXBaseSpec(spec);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseCXXBaseSpecifier(spec);
  popNode();
  return result;
}

bool AcavAstVisitor::TraverseConstructorInitializer(
    clang::CXXCtorInitializer *init) {
  if (!init) {
    return true;
  }
  AstViewNode *node = createNodeFromCtorInit(init);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseConstructorInitializer(init);
  popNode();
  return result;
}

bool AcavAstVisitor::TraverseLambdaCapture(
    clang::LambdaExpr *lambda, const clang::LambdaCapture *capture,
    clang::Expr *init) {
  if (!capture) {
    return true;
  }
  AstViewNode *node = createNodeFromLambdaCapture(capture);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result =
      RecursiveASTVisitor::TraverseLambdaCapture(lambda, capture, init);
  popNode();
  return result;
}

#if LLVM_VERSION_MAJOR >= 22
bool AcavAstVisitor::TraverseNestedNameSpecifier(
    clang::NestedNameSpecifier nns) {
  if (!nns) {
    return true;
  }
  AstViewNode *node = createNodeFromNestedNameSpec(nns);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseNestedNameSpecifier(nns);
  popNode();
  return result;
}
#else
bool AcavAstVisitor::TraverseNestedNameSpecifier(
    clang::NestedNameSpecifier *nns) {
  if (!nns) {
    return true;
  }
  AstViewNode *node = createNodeFromNestedNameSpec(nns);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseNestedNameSpecifier(nns);
  popNode();
  return result;
}
#endif

bool AcavAstVisitor::TraverseNestedNameSpecifierLoc(
    clang::NestedNameSpecifierLoc loc) {
  if (!loc) {
    return true;
  }
  AstViewNode *node = createNodeFromNestedNameSpecLoc(loc);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseNestedNameSpecifierLoc(loc);
  popNode();
  return result;
}

bool AcavAstVisitor::TraverseTemplateArgument(clang::TemplateArgument arg) {
  AstViewNode *node = createNodeFromTemplateArg(arg);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseTemplateArgument(arg);
  popNode();
  return result;
}

bool AcavAstVisitor::TraverseTemplateArgumentLoc(
    clang::TemplateArgumentLoc loc) {
  AstViewNode *node = createNodeFromTemplateArgLoc(loc);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseTemplateArgumentLoc(loc);
  popNode();
  return result;
}

bool AcavAstVisitor::TraverseTemplateName(clang::TemplateName name) {
  AstViewNode *node = createNodeFromTemplateName(name);
  if (!node) {
    return true;
  }
  pushNode(node);
  bool result = RecursiveASTVisitor::TraverseTemplateName(name);
  popNode();
  return result;
}

bool AcavAstVisitor::VisitDecl(clang::Decl *decl) {
  if (!decl || parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  properties["isDeclContext"] = llvm::isa<clang::DeclContext>(decl);
  if (auto *namedDecl = llvm::dyn_cast<clang::NamedDecl>(decl)) {
    maybeSetNamedDeclProperty(properties, "name", namedDecl);
    if (namedDecl->hasLinkage()) {
      properties["linkage"] =
          static_cast<int64_t>(namedDecl->getLinkageInternal());
      properties["linkageName"] = linkageName(namedDecl->getLinkageInternal());
    }
  }
  if (auto *valueDecl = llvm::dyn_cast<clang::ValueDecl>(decl)) {
    clang::QualType type = valueDecl->getType();
    if (!type.isNull()) {
      properties["type"] = getCachedTypeString(type);
    }
  }
  if (auto *funcDecl = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
    properties["isDefined"] = funcDecl->isDefined();
    properties["isInlined"] = funcDecl->isInlined();
    properties["isConstexpr"] = funcDecl->isConstexpr();
    properties["isDeleted"] = funcDecl->isDeleted();
    properties["isDefaulted"] = funcDecl->isDefaulted();
    clang::QualType returnType = funcDecl->getReturnType();
    if (!returnType.isNull()) {
      properties["returnType"] = getCachedTypeString(returnType);
    }
    unsigned numParams = funcDecl->getNumParams();
    properties["numParams"] = static_cast<int64_t>(numParams);
    if (numParams > 0) {
      AcavJson params = AcavJson::array();
      for (unsigned i = 0; i < numParams; ++i) {
        const clang::ParmVarDecl *param = funcDecl->getParamDecl(i);
        AcavJson paramInfo;
        maybeSetNamedDeclProperty(paramInfo, "name", param);
        paramInfo["type"] = getCachedTypeString(param->getType());
        params.push_back(paramInfo);
      }
      properties["params"] = params;
    }
    if (auto *cxxMethod = llvm::dyn_cast<clang::CXXMethodDecl>(funcDecl)) {
      properties["isVirtual"] = cxxMethod->isVirtual();
      properties["isPure"] = cxxMethod->isPureVirtual();
      properties["isConst"] = cxxMethod->isConst();
      properties["isStatic"] = cxxMethod->isStatic();
      properties["isOverride"] = cxxMethod->size_overridden_methods() > 0;
      switch (cxxMethod->getRefQualifier()) {
      case clang::RQ_None:
        break;
      case clang::RQ_LValue:
        properties["refQualifier"] = InternedString("&");
        break;
      case clang::RQ_RValue:
        properties["refQualifier"] = InternedString("&&");
        break;
      }
    }
  }
  if (auto *varDecl = llvm::dyn_cast<clang::VarDecl>(decl)) {
    properties["isUsed"] = varDecl->isUsed();
    properties["isReferenced"] = varDecl->isReferenced();
    properties["hasInit"] = varDecl->hasInit();
    properties["isConstexpr"] = varDecl->isConstexpr();
    if (varDecl->hasInit()) {
      properties["initStyle"] = static_cast<int64_t>(varDecl->getInitStyle());
      properties["initStyleName"] =
          varDeclInitStyleName(varDecl->getInitStyle());
    }
    properties["storageClass"] =
        static_cast<int64_t>(varDecl->getStorageClass());
    properties["storageClassName"] =
        storageClassName(varDecl->getStorageClass());
    properties["isThreadLocal"] =
        varDecl->getTLSKind() != clang::VarDecl::TLS_None;
  }
  if (auto *fieldDecl = llvm::dyn_cast<clang::FieldDecl>(decl)) {
    properties["isMutable"] = fieldDecl->isMutable();
    properties["isBitField"] = fieldDecl->isBitField();
  }
  if (auto *parmDecl = llvm::dyn_cast<clang::ParmVarDecl>(decl)) {
    properties["isUsed"] = parmDecl->isUsed();
    properties["hasDefaultArg"] = parmDecl->hasDefaultArg();
  }
  if (auto *recordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(decl)) {
    properties["isReferenced"] = recordDecl->isReferenced();
    properties["hasDefinition"] = recordDecl->hasDefinition();
    if (recordDecl->hasDefinition() && recordDecl->isCompleteDefinition()) {
      properties["isAbstract"] = recordDecl->isAbstract();
      properties["isPolymorphic"] = recordDecl->isPolymorphic();
      properties["isEmpty"] = recordDecl->isEmpty();
      properties["isAggregate"] = recordDecl->isAggregate();
      properties["isPOD"] = recordDecl->isPOD();
      properties["isTrivial"] = recordDecl->isTrivial();
      if (recordDecl->getNumBases() > 0) {
        AcavJson bases = AcavJson::array();
        for (const auto &base : recordDecl->bases()) {
          clang::QualType baseType = base.getType();
          if (baseType.isNull()) {
            continue;
          }
          AcavJson baseInfo;
          baseInfo["type"] = getCachedTypeString(baseType);
          baseInfo["isVirtual"] = base.isVirtual();
          baseInfo["accessSpecifier"] =
              static_cast<int64_t>(base.getAccessSpecifier());
          bases.push_back(baseInfo);
        }
        properties["bases"] = bases;
        properties["numBases"] =
            static_cast<int64_t>(recordDecl->getNumBases());
      }
    }
    if (auto *tagDecl = llvm::dyn_cast<clang::TagDecl>(recordDecl)) {
      properties["tagKind"] = static_cast<int64_t>(tagDecl->getTagKind());
      properties["isCompleteDefinition"] = tagDecl->isCompleteDefinition();
    }
  }
  if (auto *enumDecl = llvm::dyn_cast<clang::EnumDecl>(decl)) {
    properties["isScoped"] = enumDecl->isScoped();
    properties["isFixed"] = enumDecl->isFixed();
    if (enumDecl->isFixed()) {
      clang::QualType intType = enumDecl->getIntegerType();
      if (!intType.isNull()) {
        properties["underlyingType"] = getCachedTypeString(intType);
      }
    }
  }
  if (auto *enumConstDecl = llvm::dyn_cast<clang::EnumConstantDecl>(decl)) {
    properties["initVal"] = enumConstDecl->getInitVal().getSExtValue();
  }
  if (auto *tempTypeParm = llvm::dyn_cast<clang::TemplateTypeParmDecl>(decl)) {
    properties["depth"] = static_cast<int64_t>(tempTypeParm->getDepth());
    properties["index"] = static_cast<int64_t>(tempTypeParm->getIndex());
    properties["hasDefaultArgument"] = tempTypeParm->hasDefaultArgument();
  }
  if (auto *classTemplateSpec =
          llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl)) {
    const clang::TemplateArgumentList &args =
        classTemplateSpec->getTemplateArgs();
    typeExtractor_.extractTemplateArgs(&args, properties);
    properties["specializationKind"] =
        static_cast<int64_t>(classTemplateSpec->getSpecializationKind());
  }
  if (auto *typedefDecl = llvm::dyn_cast<clang::TypedefNameDecl>(decl)) {
    clang::QualType underlying = typedefDecl->getUnderlyingType();
    if (!underlying.isNull()) {
      properties["underlyingType"] = getCachedTypeString(underlying);
    }
  }
  // Attach declaration context info for all Decl nodes.
  // All Decl nodes have semantic and lexical declaration contexts.
  AstViewNode *currentNode =
      parentStack_.empty() ? nullptr : parentStack_.back();
  properties["semanticDeclContext"] = buildDeclContextChain(
      decl->getDeclContext(), decl, declContextToNode_, currentNode);
  properties["lexicalDeclContext"] = buildDeclContextChain(
      decl->getLexicalDeclContext(), decl, declContextToNode_, currentNode);
  return true;
}

bool AcavAstVisitor::VisitStmt(clang::Stmt *stmt) {
  if (!stmt || parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  if (auto *expr = llvm::dyn_cast<clang::Expr>(stmt)) {
    clang::QualType exprType = expr->getType();
    if (!exprType.isNull()) {
      properties["type"] = getCachedTypeString(exprType);
    }
    properties["valueKind"] = static_cast<int64_t>(expr->getValueKind());
    properties["valueKindName"] = exprValueKindName(expr->getValueKind());
    properties["objectKind"] = static_cast<int64_t>(expr->getObjectKind());
    properties["objectKindName"] = exprObjectKindName(expr->getObjectKind());
    properties["isTypeDependent"] = expr->isTypeDependent();
    properties["isValueDependent"] = expr->isValueDependent();
    properties["isLValue"] = expr->isLValue();
    properties["isPRValue"] = expr->isPRValue();
    properties["isXValue"] = expr->isXValue();
    properties["isGLValue"] = expr->isGLValue();
  }
  if (auto *ifStmt = llvm::dyn_cast<clang::IfStmt>(stmt)) {
    properties["hasElse"] = (ifStmt->getElse() != nullptr);
    properties["hasInit"] = (ifStmt->getInit() != nullptr);
    properties["hasVar"] = (ifStmt->getConditionVariable() != nullptr);
    properties["isConstexpr"] = ifStmt->isConstexpr();
  }
  if (auto *binOp = llvm::dyn_cast<clang::BinaryOperator>(stmt)) {
    properties["opcode"] = InternedString(toStringView(binOp->getOpcodeStr()));
    properties["opcodeValue"] = static_cast<int64_t>(binOp->getOpcode());
    properties["isAssignment"] = binOp->isAssignmentOp();
    properties["isCompound"] = binOp->isCompoundAssignmentOp();
    properties["isComparison"] = binOp->isComparisonOp();
    properties["isLogical"] = binOp->isLogicalOp();
  }
  if (auto *unaryOp = llvm::dyn_cast<clang::UnaryOperator>(stmt)) {
    properties["opcode"] = InternedString(
        toStringView(clang::UnaryOperator::getOpcodeStr(unaryOp->getOpcode())));
    properties["opcodeValue"] = static_cast<int64_t>(unaryOp->getOpcode());
    properties["isPrefix"] = unaryOp->isPrefix();
    properties["isPostfix"] = unaryOp->isPostfix();
    properties["isIncrementOp"] = unaryOp->isIncrementOp();
    properties["isDecrementOp"] = unaryOp->isDecrementOp();
  }
  if (auto *intLit = llvm::dyn_cast<clang::IntegerLiteral>(stmt)) {
    properties["value"] = intLit->getValue().getSExtValue();
  }
  if (auto *floatLit = llvm::dyn_cast<clang::FloatingLiteral>(stmt)) {
    properties["value"] = floatLit->getValueAsApproximateDouble();
    properties["isExact"] = floatLit->isExact();
  }
  if (auto *strLit = llvm::dyn_cast<clang::StringLiteral>(stmt)) {
    properties["byteLength"] = static_cast<int64_t>(strLit->getByteLength());
    properties["length"] = static_cast<int64_t>(strLit->getLength());
    properties["charWidth"] = static_cast<int64_t>(strLit->getCharByteWidth());

    const bool isSingleByte = strLit->getCharByteWidth() == 1;

    if (isSingleByte) {
      properties["value"] = InternedString(toStringView(strLit->getString()));
      properties["encoding"] = InternedString("char");
    } else {
      std::string buffer;
      llvm::raw_string_ostream os(buffer);
      strLit->outputString(os);
      os.flush();
      properties["value"] = InternedString(buffer);
      properties["encoding"] = InternedString("wide");
    }
  }
  if (auto *charLit = llvm::dyn_cast<clang::CharacterLiteral>(stmt)) {
    properties["value"] = static_cast<int64_t>(charLit->getValue());
  }
  if (auto *boolLit = llvm::dyn_cast<clang::CXXBoolLiteralExpr>(stmt)) {
    properties["value"] = boolLit->getValue();
  }
  if (auto *declRef = llvm::dyn_cast<clang::DeclRefExpr>(stmt)) {
    if (auto *decl = declRef->getDecl()) {
      if (auto *namedDecl = llvm::dyn_cast<clang::NamedDecl>(decl)) {
        maybeSetNamedDeclProperty(properties, "declName", namedDecl);
      }
    }
  }
  if (auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(stmt)) {
    if (auto *member = memberExpr->getMemberDecl()) {
      maybeSetNamedDeclProperty(properties, "memberName", member);
    }
    properties["isArrow"] = memberExpr->isArrow();
  }
  if (auto *callExpr = llvm::dyn_cast<clang::CallExpr>(stmt)) {
    properties["numArgs"] = static_cast<int64_t>(callExpr->getNumArgs());
    if (auto *callee = callExpr->getCallee()) {
      if (auto *declRef = llvm::dyn_cast<clang::DeclRefExpr>(callee)) {
        if (auto *funcDecl =
                llvm::dyn_cast<clang::FunctionDecl>(declRef->getDecl())) {
          maybeSetNamedDeclProperty(properties, "calleeName", funcDecl);
        }
      }
    }
  }
  if (auto *castExpr = llvm::dyn_cast<clang::CastExpr>(stmt)) {
    properties["castKind"] = InternedString(castExpr->getCastKindName());
    properties["castKindValue"] = static_cast<int64_t>(castExpr->getCastKind());
    if (auto *explicitCast =
            llvm::dyn_cast<clang::ExplicitCastExpr>(castExpr)) {
      clang::QualType targetType = explicitCast->getTypeAsWritten();
      properties["targetType"] = getCachedTypeString(targetType);
    }
  }
  if (auto *ctorExpr = llvm::dyn_cast<clang::CXXConstructExpr>(stmt)) {
    if (auto *ctorDecl = ctorExpr->getConstructor()) {
      maybeSetNamedDeclProperty(properties, "constructorName", ctorDecl);
    }
    properties["numArgs"] = static_cast<int64_t>(ctorExpr->getNumArgs());
    properties["isElidable"] = ctorExpr->isElidable();
    properties["requiresZeroInit"] = ctorExpr->requiresZeroInitialization();
  }
  if (auto *newExpr = llvm::dyn_cast<clang::CXXNewExpr>(stmt)) {
    clang::QualType allocatedType = newExpr->getAllocatedType();
    properties["allocatedType"] = getCachedTypeString(allocatedType);
    properties["isArray"] = newExpr->isArray();
    properties["isGlobalNew"] = newExpr->isGlobalNew();
  }
  if (auto *deleteExpr = llvm::dyn_cast<clang::CXXDeleteExpr>(stmt)) {
    properties["isArray"] = deleteExpr->isArrayForm();
    properties["isGlobalDelete"] = deleteExpr->isGlobalDelete();
  }
  if (auto *lambdaExpr = llvm::dyn_cast<clang::LambdaExpr>(stmt)) {
    properties["isMutable"] = lambdaExpr->isMutable();
    properties["hasExplicitParams"] = lambdaExpr->hasExplicitParameters();
    properties["hasExplicitResultType"] = lambdaExpr->hasExplicitResultType();
    properties["numCaptures"] =
        static_cast<int64_t>(lambdaExpr->capture_size());
  }
  return true;
}

bool AcavAstVisitor::VisitType(clang::Type *type) {
  if (!type || parentStack_.empty()) {
    return true;
  }
  const clang::Type *canonicalType =
      type->getCanonicalTypeInternal().getTypePtr();
  AcavJson &properties = currentProperties();
  if (properties.contains("canonicalType")) {
    return true;
  }
  clang::QualType qt(canonicalType, 0);
  properties["canonicalType"] = getCachedTypeString(qt);
  if (auto *builtinType = llvm::dyn_cast<clang::BuiltinType>(canonicalType)) {
    clang::PrintingPolicy policy(astContext_.getLangOpts());
    properties["typeName"] =
        InternedString(toStringView(builtinType->getName(policy)));
  }
  if (auto *arrayType =
          llvm::dyn_cast<clang::ConstantArrayType>(canonicalType)) {
    properties["size"] =
        static_cast<int64_t>(arrayType->getSize().getZExtValue());
  }
  return true;
}

bool AcavAstVisitor::VisitTypeLoc(clang::TypeLoc typeLoc) {
  if (typeLoc.isNull() || parentStack_.empty()) {
    return true;
  }
  const clang::Type *type = typeLoc.getType().getTypePtr();
  AcavJson &properties = currentProperties();
  Q_UNUSED(type);
  clang::SourceRange localRange = typeLoc.getLocalSourceRange();
  if (localRange.isValid()) {
    properties["hasLocalRange"] = true;
  }
  return true;
}

bool AcavAstVisitor::VisitAttr(clang::Attr *attr) {
  if (!attr || parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  properties["spelling"] = InternedString(attr->getSpelling());
  return true;
}

bool AcavAstVisitor::VisitConceptReference(clang::ConceptReference *cr) {
  if (!cr || parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  if (auto *namedConcept = cr->getNamedConcept()) {
    maybeSetNamedDeclProperty(properties, "name", namedConcept);
  }
  return true;
}

bool AcavAstVisitor::VisitCXXBaseSpecifier(
    const clang::CXXBaseSpecifier &spec) {
  if (parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  clang::QualType baseType = spec.getType();
  if (!baseType.isNull()) {
    properties["baseType"] = getCachedTypeString(baseType);
  }
  return true;
}

bool AcavAstVisitor::VisitCXXCtorInitializer(
    clang::CXXCtorInitializer *init) {
  if (!init || parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  if (init->isMemberInitializer() && init->getMember()) {
    maybeSetNamedDeclProperty(properties, "member", init->getMember());
  }
  return true;
}

bool AcavAstVisitor::VisitLambdaCapture(const clang::LambdaCapture *capture) {
  if (!capture || parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  properties["captureKind"] = static_cast<int64_t>(capture->getCaptureKind());
  return true;
}

bool AcavAstVisitor::VisitNestedNameSpecifier(
    clang::NestedNameSpecifier *nns) {
  if (!nns || parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  std::string qualifierStr;
  llvm::raw_string_ostream os(qualifierStr);
  nns->print(os, astContext_.getPrintingPolicy());
  os.flush();
  properties["qualifier"] = InternedString(qualifierStr);
  return true;
}

bool AcavAstVisitor::VisitNestedNameSpecifierLoc(
    clang::NestedNameSpecifierLoc loc) {
  if (!loc || parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  return true;
}

bool AcavAstVisitor::VisitTemplateArgument(clang::TemplateArgument arg) {
  if (parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  properties["argKind"] = static_cast<int64_t>(arg.getKind());
  switch (arg.getKind()) {
  case clang::TemplateArgument::Type:
    if (!arg.getAsType().isNull()) {
      properties["value"] = getCachedTypeString(arg.getAsType());
    }
    break;
  case clang::TemplateArgument::Integral:
    properties["value"] = arg.getAsIntegral().getSExtValue();
    break;
  default:
    break;
  }
  return true;
}

bool AcavAstVisitor::VisitTemplateArgumentLoc(
    clang::TemplateArgumentLoc loc) {
  if (parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  const clang::TemplateArgument &arg = loc.getArgument();
  properties["argKind"] = static_cast<int64_t>(arg.getKind());
  switch (arg.getKind()) {
  case clang::TemplateArgument::Type:
    if (!arg.getAsType().isNull()) {
      properties["value"] = getCachedTypeString(arg.getAsType());
    }
    break;
  case clang::TemplateArgument::Integral:
    properties["value"] = arg.getAsIntegral().getSExtValue();
    break;
  default:
    break;
  }
  return true;
}

bool AcavAstVisitor::VisitTemplateName(clang::TemplateName name) {
  if (parentStack_.empty()) {
    return true;
  }
  AcavJson &properties = currentProperties();
  if (auto *tempDecl = name.getAsTemplateDecl()) {
    if (auto *namedDecl = llvm::dyn_cast<clang::NamedDecl>(tempDecl)) {
      maybeSetNamedDeclProperty(properties, "name", namedDecl);
    }
  }
  return true;
}

// The createNode* implementations below are copied from the previous visitor
// in AstExtractorRunner to preserve behavior.

AstViewNode *AcavAstVisitor::createNodeFromDecl(clang::Decl *decl) {
  if (!decl) {
    return nullptr;
  }
  AcavJson properties;
  properties["kind"] = InternedString(getFullDeclClassName(decl));
  properties["isDecl"] = true;
  clang::SourceRange clangRange = decl->getSourceRange();
  maybeAddMacroSpellingRange(properties, clangRange,
                             astContext_.getSourceManager(), fileManager_);
  SourceRange auroraRange = SourceRange::fromClang(
      clangRange, astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.declCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *AcavAstVisitor::createNodeFromStmt(clang::Stmt *stmt) {
  if (!stmt) {
    return nullptr;
  }
  AcavJson properties;
  properties["kind"] = InternedString(stmt->getStmtClassName());
  clang::SourceRange clangRange = stmt->getSourceRange();
  maybeAddMacroSpellingRange(properties, clangRange,
                             astContext_.getSourceManager(), fileManager_);
  SourceRange auroraRange = SourceRange::fromClang(
      clangRange, astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.stmtCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *AcavAstVisitor::createNodeFromType(const clang::Type *type) {
  if (!type) {
    return nullptr;
  }
  const clang::Type *canonicalType =
      type->getCanonicalTypeInternal().getTypePtr();
  if (AstNode *existing = context_->findTypeNode(canonicalType)) {
    ++stats_.totalCount;
    ++stats_.typeCount;
    return context_->createAstViewNode(existing);
  }
  AcavJson properties;
  properties["kind"] = InternedString(getFullTypeClassName(type));
  SourceRange auroraRange = SourceRange::fromClang(
      clang::SourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode =
      context_->getOrCreateTypeNode(canonicalType, properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.typeCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *AcavAstVisitor::createNodeFromTypeLoc(clang::TypeLoc typeLoc) {
  if (typeLoc.isNull()) {
    return nullptr;
  }
  const clang::Type *type = typeLoc.getType().getTypePtr();
  AcavJson properties;
  properties["kind"] = InternedString(getFullTypeClassName(type));
  maybeAddMacroSpellingRange(properties, typeLoc.getSourceRange(),
                             astContext_.getSourceManager(), fileManager_);
  SourceRange auroraRange = SourceRange::fromClang(
      typeLoc.getSourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.typeLocCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *AcavAstVisitor::createNodeFromAttr(clang::Attr *attr) {
  if (!attr) {
    return nullptr;
  }
  AcavJson properties;
  properties["kind"] = InternedString("Attr");
  maybeAddMacroSpellingRange(properties, attr->getRange(),
                             astContext_.getSourceManager(), fileManager_);
  SourceRange auroraRange = SourceRange::fromClang(
      attr->getRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.attrCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *
AcavAstVisitor::createNodeFromConceptRef(clang::ConceptReference *cr) {
  if (!cr) {
    return nullptr;
  }
  AcavJson properties;
  properties["kind"] = InternedString("ConceptReference");
  maybeAddMacroSpellingRange(properties, cr->getSourceRange(),
                             astContext_.getSourceManager(), fileManager_);
  SourceRange auroraRange = SourceRange::fromClang(
      cr->getSourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.conceptRefCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *AcavAstVisitor::createNodeFromCXXBaseSpec(
    const clang::CXXBaseSpecifier &spec) {
  AcavJson properties;
  properties["kind"] = InternedString("CXXBaseSpecifier");
  maybeAddMacroSpellingRange(properties, spec.getSourceRange(),
                             astContext_.getSourceManager(), fileManager_);
  SourceRange auroraRange = SourceRange::fromClang(
      spec.getSourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.cxxBaseSpecCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *
AcavAstVisitor::createNodeFromCtorInit(clang::CXXCtorInitializer *init) {
  if (!init) {
    return nullptr;
  }
  AcavJson properties;
  properties["kind"] = InternedString("CXXCtorInitializer");
  maybeAddMacroSpellingRange(properties, init->getSourceRange(),
                             astContext_.getSourceManager(), fileManager_);
  SourceRange auroraRange = SourceRange::fromClang(
      init->getSourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.ctorInitCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *AcavAstVisitor::createNodeFromLambdaCapture(
    const clang::LambdaCapture *capture) {
  if (!capture) {
    return nullptr;
  }
  AcavJson properties;
  properties["kind"] = InternedString("LambdaCapture");
  SourceRange auroraRange = SourceRange::fromClang(
      clang::SourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.lambdaCaptureCount;
  return context_->createAstViewNode(astNode);
}

#if LLVM_VERSION_MAJOR >= 22
AstViewNode *AcavAstVisitor::createNodeFromNestedNameSpec(
    clang::NestedNameSpecifier nns) {
  if (!nns) {
    return nullptr;
  }
#else
AstViewNode *AcavAstVisitor::createNodeFromNestedNameSpec(
    clang::NestedNameSpecifier *nns) {
  if (!nns) {
    return nullptr;
  }
#endif
  AcavJson properties;
  properties["kind"] = InternedString("NestedNameSpecifier");
  SourceRange auroraRange = SourceRange::fromClang(
      clang::SourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.nestedNameSpecCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *AcavAstVisitor::createNodeFromNestedNameSpecLoc(
    clang::NestedNameSpecifierLoc loc) {
  if (!loc) {
    return nullptr;
  }
  AcavJson properties;
  properties["kind"] = InternedString("NestedNameSpecifierLoc");
  maybeAddMacroSpellingRange(properties, loc.getSourceRange(),
                             astContext_.getSourceManager(), fileManager_);
  SourceRange auroraRange = SourceRange::fromClang(
      loc.getSourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.nestedNameSpecLocCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *
AcavAstVisitor::createNodeFromTemplateArg(clang::TemplateArgument arg) {
  AcavJson properties;
  properties["kind"] = InternedString("TemplateArgument");
  SourceRange auroraRange = SourceRange::fromClang(
      clang::SourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.tempArgCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *
AcavAstVisitor::createNodeFromTemplateArgLoc(clang::TemplateArgumentLoc loc) {
  AcavJson properties;
  properties["kind"] = InternedString("TemplateArgumentLoc");
  maybeAddMacroSpellingRange(properties, loc.getSourceRange(),
                             astContext_.getSourceManager(), fileManager_);
  SourceRange auroraRange = SourceRange::fromClang(
      loc.getSourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.tempArgLocCount;
  return context_->createAstViewNode(astNode);
}

AstViewNode *
AcavAstVisitor::createNodeFromTemplateName(clang::TemplateName name) {
  AcavJson properties;
  properties["kind"] = InternedString("TemplateName");
  SourceRange auroraRange = SourceRange::fromClang(
      clang::SourceRange(), astContext_.getSourceManager(), fileManager_);
  AstNode *astNode = context_->createAstNode(properties, auroraRange);
  ++stats_.totalCount;
  ++stats_.tempNameCount;
  return context_->createAstViewNode(astNode);
}

} // namespace

AstViewNode *AcavAstBuilder::buildFromASTUnit(clang::ASTUnit &astUnit,
                                                AstContext *context,
                                                FileManager &fileManager,
                                                AstExtractionStats &stats,
                                                bool extractComments) {
  if (!context) {
    return nullptr;
  }
  clang::ASTContext &ctx = astUnit.getASTContext();
  clang::TranslationUnitDecl *tuDecl = ctx.getTranslationUnitDecl();
  if (!tuDecl) {
    return nullptr;
  }
  AcavAstVisitor visitor(context, fileManager, ctx,
                           astUnit.getPreprocessorPtr().get(), extractComments);
  visitor.TraverseDecl(tuDecl);
  stats = visitor.getStats();
  return visitor.getRootNode();
}

} // namespace acav
