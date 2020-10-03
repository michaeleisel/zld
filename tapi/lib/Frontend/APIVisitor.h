//===- tapi/Frontend/APIVisitor - TAPI API Visitor --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the TAPI API Visitor
///
//===----------------------------------------------------------------------===//
#ifndef TAPI_FRONTEND_API_VISITOR_H
#define TAPI_FRONTEND_API_VISITOR_H

#include "tapi/Core/API.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "tapi/Frontend/FrontendContext.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "llvm/IR/DataLayout.h"

using llvm::DataLayout;
using TAPI_INTERNAL::API;
using TAPI_INTERNAL::APIAccess;
using TAPI_INTERNAL::AvailabilityInfo;
using TAPI_INTERNAL::FrontendContext;
using TAPI_INTERNAL::ObjCContainerRecord;

namespace clang {

class APIVisitor : public ASTConsumer, public RecursiveASTVisitor<APIVisitor> {
public:
  APIVisitor(FrontendContext &context);
  void HandleTranslationUnit(ASTContext &context) override;
  bool shouldVisitTemplateInstantiations() const { return true; }

  bool VisitVarDecl(const VarDecl *decl);
  bool VisitFunctionDecl(const FunctionDecl *decl);
  bool VisitEnumDecl(const EnumDecl *decl);
  bool VisitObjCInterfaceDecl(const ObjCInterfaceDecl *decl);
  bool VisitObjCCategoryDecl(const ObjCCategoryDecl *decl);
  bool VisitObjCProtocolDecl(const ObjCProtocolDecl *decl);
  bool VisitCXXRecordDecl(const CXXRecordDecl *decl);
  bool VisitTypedefNameDecl(const TypedefNameDecl *decl);

private:
  void recordObjCMethods(ObjCContainerRecord *record,
                         const ObjCContainerDecl::method_range methods,
                         bool isDynamic = false);
  void recordObjCProperties(ObjCContainerRecord *record,
                            const ObjCContainerDecl::prop_range properties);
  void recordObjCInstanceVariables(
      ObjCContainerRecord *record,
      const llvm::iterator_range<
          DeclContext::specific_decl_iterator<ObjCIvarDecl>>
          ivars);
  void recordObjCProtocols(ObjCContainerRecord *record,
                           ObjCInterfaceDecl::protocol_range protocols);
  void emitVTableSymbols(const CXXRecordDecl *decl, PresumedLoc loc,
                         AvailabilityInfo avail, APIAccess access,
                         bool emittedVTable = false);
  llvm::Optional<std::pair<APIAccess, PresumedLoc>>
  getFileAttributesForDecl(const NamedDecl *decl) const;
  std::string getMangledName(const NamedDecl *decl) const;
  std::string getBackendMangledName(Twine name) const;
  std::string getMangledCXXVTableName(const CXXRecordDecl *decl) const;
  std::string getMangledCXXRTTI(const CXXRecordDecl *decl) const;
  std::string getMangledCXXRTTIName(const CXXRecordDecl *decl) const;
  std::string getMangledCXXThunk(const GlobalDecl &decl,
                                 const ThunkInfo &thunk) const;
  std::string getMangledCtorDtor(const CXXMethodDecl *decl, int type) const;
  AvailabilityInfo getAvailabilityInfo(const Decl *decl) const;

  FrontendContext &frontend;
  ASTContext &context;
  SourceManager &sourceManager;
  std::unique_ptr<clang::ItaniumMangleContext> mc;
  const DataLayout &dataLayout;
};

class APIVisitorAction : public ASTFrontendAction {
public:
  explicit APIVisitorAction(FrontendContext &context) : context(context) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &compiler,
                                                 StringRef inFile) override {
    context.ast = &compiler.getASTContext();
    context.sourceMgr = &compiler.getSourceManager();
    context.pp = compiler.getPreprocessorPtr();
    return llvm::make_unique<APIVisitor>(context);
  }

  FrontendContext &context;
};

} // end namespace clang.

#endif // TAPI_FRONTEND_API_VISITOR_H
