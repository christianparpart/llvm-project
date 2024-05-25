//===--- FixStdstringDataAccessCheck.cpp - clang-tidy ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FixStdstringDataAccessCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::misc {

void FixStdstringDataAccessCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
    cxxMemberCallExpr(
      callee(
        cxxMethodDecl(
          hasName("data"),
          ofClass(hasName("std::basic_string"))
        )
      )   
    ).bind("cxxMemberCallExpr"), 
    this
  );
}

void FixStdstringDataAccessCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Call = Result.Nodes.getNodeAs<CXXMemberCallExpr>("cxxMemberCallExpr");

  const auto FunctionNameSourceRange = CharSourceRange::getCharRange(
      Call->getExprLoc().getLocWithOffset(0), Call->getExprLoc().getLocWithOffset(4));
      // Call->getExprLoc().getLocWithOffset(0), Call->getExprLoc().getLocWithOffset(-1));

  diag(Call->getEndLoc(), "Consider rewriting .data() to .c_str()")
    << FixItHint::CreateReplacement(FunctionNameSourceRange, "c_str");
}

} // namespace clang::tidy::misc
