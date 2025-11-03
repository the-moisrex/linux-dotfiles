#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RawCommentList.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <string>
#include <vector>

using namespace clang;

namespace {

class MyVisitor : public RecursiveASTVisitor<MyVisitor> {
private:
  ASTContext &Context;
  std::string SymbolName;

public:
  MyVisitor(ASTContext &Ctx, std::string Sym) : Context(Ctx), SymbolName(Sym) {}

  bool VisitNamedDecl(NamedDecl *D) {
    if (D->getNameAsString() == SymbolName) {
      PrintingPolicy Policy(Context.getLangOpts());
      Policy.TerseOutput = true;
      Policy.PolishForDeclaration = true;
      D->print(llvm::outs(), Policy);
      llvm::outs() << ";\n\n";
    }
    return true;
  }
};

class MyConsumer : public ASTConsumer {
private:
  std::unique_ptr<MyVisitor> Visitor;
  std::string SymbolName;

public:
  MyConsumer(ASTContext *Ctx, std::string Sym) : SymbolName(Sym) {
    Visitor = std::make_unique<MyVisitor>(*Ctx, Sym);
  }

  void HandleTranslationUnit(ASTContext &Ctx) override {
    Visitor->TraverseDecl(Ctx.getTranslationUnitDecl());
  }
};

class MyPluginAction : public PluginASTAction {
private:
  std::string SymbolName;

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return std::make_unique<MyConsumer>(&CI.getASTContext(), SymbolName);
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &Args) override {
    if (Args.size() != 1) {
      DiagnosticsEngine &D = CI.getDiagnostics();
      unsigned DiagID =
          D.getCustomDiagID(DiagnosticsEngine::Error,
                            "Expected exactly one argument: the symbol name");
      D.Report(DiagID);
      return false;
    }
    SymbolName = Args[0];
    return true;
  }

  PluginASTAction::ActionType getActionType() override {
    return AddBeforeMainAction;
  }
};

} // namespace

static FrontendPluginRegistry::Add<MyPluginAction>
    X("decls", "Prints declaration for a given symbol name");