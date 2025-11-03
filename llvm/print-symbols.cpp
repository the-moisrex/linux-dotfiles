#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <string_view>

using namespace clang;
using namespace llvm;

namespace {

class PrintSymbolConsumer : public ASTConsumer {
public:
  PrintSymbolConsumer(CompilerInstance &CI, StringRef SymbolName)
      : CI(CI), TargetName(SymbolName.str()) {}

  void HandleTranslationUnit(ASTContext &Ctx) override {
    PrintingPolicy Policy(Ctx.getLangOpts());
    Policy.TerseOutput = false;
    raw_ostream &out = outs();

    struct Visitor : public RecursiveASTVisitor<Visitor> {
      std::string Target;
      PrintingPolicy Policy;
      raw_ostream &out;
      ASTContext &Ctx;
      SmallVector<NamedDecl *, 4> matches;
      raw_ostream &sout;

      Visitor(ASTContext &ctx, raw_ostream &o, const std::string &target,
              const PrintingPolicy &policy)
          : Ctx(ctx), out(o), Target(target), Policy(policy), sout{outs()} {}

      bool VisitNamedDecl(NamedDecl *ND) {
        if (ND->getNameAsString() == Target) {
          matches.push_back(ND);
        }
        return true;
      }
    };

    Visitor visitor(Ctx, out, TargetName, Policy);
    visitor.TraverseTranslationUnitDecl(Ctx.getTranslationUnitDecl());

    if (visitor.matches.empty()) {
      DiagnosticsEngine &Diags = CI.getDiagnostics();
      unsigned DiagID = Diags.getCustomDiagID(
          DiagnosticsEngine::Warning,
          "print-symbols plugin: no symbol named '%0' found in TU");
      Diags.Report(DiagID) << TargetName;
      return;
    }

    for (auto *ND : visitor.matches) {
      ND->print(out, Policy);
      out << "\n";

      if (auto *CRD = dyn_cast<CXXRecordDecl>(ND)) {
        for (auto *MD : CRD->methods()) {
          if (MD->isThisDeclarationADefinition() && MD->isOutOfLine()) {
            MD->print(out, Policy);
            out << "\n";
          }
        }
        out << "// ---- end of class " << TargetName << " ----\n\n";
      } else {
        out << "// ---- end of symbol " << TargetName << " ----\n\n";
      }
    }
  }

private:
  CompilerInstance &CI;
  std::string TargetName;
};

class PrintSymbolAction : public PluginASTAction {
public:
  std::unique_ptr<ASTConsumer>
  CreateASTConsumer(CompilerInstance &CI, StringRef /*InFile*/) override {
    return std::make_unique<PrintSymbolConsumer>(CI, SymbolName);
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    for (auto &arg : args) {
      // Accept either "symbol=Foo" or just "Foo" for convenience.
      if (arg.starts_with("symbol="))
        SymbolName = arg.substr(strlen("symbol="));
      else
        SymbolName = arg;
    }

    if (SymbolName.empty()) {
      DiagnosticsEngine &Diags = CI.getDiagnostics();
      unsigned DiagID = Diags.getCustomDiagID(
          DiagnosticsEngine::Warning,
          "print-symbols plugin: no symbol name provided; use "
          "-plugin-arg-print-symbols symbol=Foo");
      Diags.Report(DiagID);
      return false; // fail parsing
    }
    return true;
  }

private:
  std::string SymbolName;
};

} // namespace

static FrontendPluginRegistry::Add<PrintSymbolAction> X(
    "print-symbols",
    "print a symbol and if class/struct, its member function implementations");

// -----------------------------
// Build & run (short guide)
// -----------------------------
/*
2) Run the plugin with clang's -cc1 interface. Example:

   clang -cc1 -load ./libprint-symbols-plugin.so -plugin print-symbols \
         -plugin-arg-print-symbols symbol=Foo test.cpp

   Alternative argument styles that some clang builds accept:

   clang -cc1 -load ./libprint-symbols-plugin.so -plugin print-symbols \
         -plugin-arg-print-symbols symbol=Foo test.cpp

Notes:
 - Use `clang -cc1` because the plugin interface is part of clang's frontend
   API. If your clang installation or packaging differs, you may need to
   adjust paths or use the clang binary that matches the headers/libs you
   built against.
 - If you see undefined symbol errors at link/load time, ensure the plugin
   is built against the *same* Clang/LLVM version as the clang you're
   invoking.
 - You can also compile and load against libclang (or use libtooling) but
   using CMake + find_package(Clang) usually simplifies the configuration.
*/