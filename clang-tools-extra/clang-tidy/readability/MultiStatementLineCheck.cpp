//===--- MultiStatementLineCheck.cpp - clang-tidy -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MultiStatementLineCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::readability {

 
void MultiStatementLineCheck::registerMatchers(MatchFinder *Finder) {
  auto matcher =
    compoundStmt(
      hasParent(stmt()),                  // Ensure it's not the main function body
      has(                                // At least one statement inside
        stmt(hasParent(compoundStmt()))
      ),
      forEach(                            // Iterate over each statement
        stmt(hasParent(compoundStmt())).bind("stmt")
      )
    );

  Finder->addMatcher(matcher, this);
}

void MultiStatementLineCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MatchedDecl = Result.Nodes.getNodeAs<FunctionDecl>("stmt");
  if (!MatchedDecl)
    return;

  const clang::ASTContext *Context = Result.Context;
  const clang::SourceManager &SourceManager = Context->getSourceManager();

  const unsigned Line = SourceManager.getPresumedLineNumber(MatchedDecl->getBeginLoc());

}

} // namespace clang::tidy::readability
