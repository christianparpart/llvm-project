//===--- MisplacedElseIfCheck.cpp - clang-tidy ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MisplacedElseIfCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/CommonOptionsParser.h"

using namespace clang::ast_matchers;

namespace clang::tidy::readability {

MisplacedElseIfCheck::MisplacedElseIfCheck(StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context),
      MaxAcceptableLineDistance(Options.get("MaxAcceptableLineDistance", MaxAcceptableLineDistance))
{
}

void MisplacedElseIfCheck::storeOptions(ClangTidyOptions::OptionMap &Opts)
{
  // add option to allow configuring the maximum consecutive lines between an if and an else if
  Options.store(Opts, "MaxAcceptableLineDistance", MaxAcceptableLineDistance);
}

void MisplacedElseIfCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
    ifStmt(
      hasElse(
        ifStmt(isExpansionInMainFile()).bind("inner_if") 
      )
    ).bind("outer_if"),
    this
  );
}

void MisplacedElseIfCheck::check(const MatchFinder::MatchResult &Result) {
  // FIXME: Add callback implementation.
  const auto *InnerIf = Result.Nodes.getNodeAs<IfStmt>("inner_if");
  const auto *OuterIf = Result.Nodes.getNodeAs<IfStmt>("outer_if");
  if (!InnerIf || !OuterIf || !OuterIf->getElse())
    return;

  const clang::ASTContext *Context = Result.Context;
  const clang::SourceManager &SourceManager = Context->getSourceManager();

  const unsigned OuterIfElseLine = SourceManager.getPresumedLineNumber(OuterIf->getElseLoc());
  const unsigned InnerIfLine = SourceManager.getPresumedLineNumber(InnerIf->getBeginLoc());
  const auto LineDistance = InnerIfLine - OuterIfElseLine;

  if (LineDistance <= MaxAcceptableLineDistance)
    return;

  diag(OuterIf->getBeginLoc(), "misplaced 'else if' after 'if' statement", DiagnosticIDs::Remark)
    << FixItHint::CreateRemoval(OuterIf->getElseLoc())
    << FixItHint::CreateInsertion(InnerIf->getBeginLoc(), "else ");
  // if (!MatchedDecl->getIdentifier() || MatchedDecl->getName().starts_with("awesome_"))
  //   return;
  // diag(MatchedDecl->getLocation(), "function %0 is insufficiently awesome")
  //     << MatchedDecl
  //     << FixItHint::CreateInsertion(MatchedDecl->getLocation(), "awesome_");
  // diag(MatchedDecl->getLocation(), "insert 'awesome'", DiagnosticIDs::Note);
}

} // namespace clang::tidy::readability
