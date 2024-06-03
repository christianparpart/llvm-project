//===--- OversizedFunctionBodyCheck.cpp - clang-tidy ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OversizedFunctionBodyCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::misc {

OversizedFunctionBodyCheck::OversizedFunctionBodyCheck(StringRef Name, ClangTidyContext *Context):
  ClangTidyCheck(Name, Context),
  MaxAcceptableLineCount(Options.get("MaxAcceptableLineCount", 100))
{
}

void OversizedFunctionBodyCheck::storeOptions(ClangTidyOptions::OptionMap &Opts)
{
  Options.store(Opts, "MaxAcceptableLineDistance", MaxAcceptableLineCount);
}

void OversizedFunctionBodyCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl(isExpansionInMainFile()).bind("function"), this);
}

void OversizedFunctionBodyCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MethodDecl = Result.Nodes.getNodeAs<FunctionDecl>("function");
  if (!MethodDecl || !MethodDecl->hasBody())
    return;

  const auto* Body = MethodDecl->getBody();

  const clang::ASTContext *Context = Result.Context;
  const clang::SourceManager &SourceManager = Context->getSourceManager();

  const unsigned LineStart = SourceManager.getPresumedLineNumber(Body->getBeginLoc());
  const unsigned LineEnd = SourceManager.getPresumedLineNumber(Body->getEndLoc());
  const unsigned LineCount = LineEnd - LineStart + 1;

  if (LineCount < 5)
    return;

  diag(MethodDecl->getBeginLoc(), "Oversized function body.", DiagnosticIDs::Remark);
}

} // namespace clang::tidy::misc
