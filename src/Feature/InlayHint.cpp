#include "Feature/InlayHint.h"

namespace clice {

namespace {

struct LspProtoAdaptor {

    const clang::SourceManager* src;

    bool isInMainFile(clang::SourceLocation loc) {
        return loc.isValid() && src->isInMainFile(loc);
    }

    bool notInMainFile(clang::SourceLocation loc) {
        return !isInMainFile(loc);
    }

    proto::Position toLspPosition(clang::SourceLocation loc) {
        auto presumed = src->getPresumedLoc(loc);
        return {
            .line = presumed.getLine() - 1,
            .character = presumed.getColumn() - 1,
        };
    }

    proto::Range toLspRange(clang::SourceRange sr) {
        return {
            .start = toLspPosition(sr.getBegin()),
            .end = toLspPosition(sr.getEnd()),
        };
    }
};

/// TODO:
/// Replace blank tooltip to something useful.

/// Create a blank markup content as a place holder.
proto::MarkupContent blank() {
    return {
        .kind = proto::MarkupKind::PlainText,
        .value = "",
    };
}

/// Compute inlay hints for a document in given range and config.
struct InlayHintCollector : clang::RecursiveASTVisitor<InlayHintCollector>, LspProtoAdaptor {

    using Base = clang::RecursiveASTVisitor<InlayHintCollector>;

    /// The config of inlay hints collector.
    const config::InlayHintConfig& config;

    /// The restrict range of request.
    clang::SourceRange limit;

    /// The result of inlay hints.
    proto::InlayHintsResult result;

    /// Current file's uri.
    proto::DocumentUri docuri;

    /// The printing policy of clang.
    const clang::PrintingPolicy policy;

    /// Do not produce inlay hints if either range ends is not within the main file.
    bool needFilter(clang::SourceRange range) {
        // skip invalid range or not in main file
        if(range.isInvalid())
            return true;

        if(!src->isInMainFile(range.getBegin()) || !src->isInMainFile(range.getEnd()))
            return true;

        // not involved in restrict range
        if(range.getEnd() < limit.getBegin() || range.getBegin() > limit.getEnd())
            return true;

        return false;
    }

    /// Collect hint for variable declared with `auto` keywords.
    /// The hint string wiil be placed at the right side of identifier, starting with ':' character.
    /// The `originDeclRange` will be used as the link of hint string.
    void collectAutoDeclHint(clang::QualType deduced, clang::SourceRange identRange,
                             std::optional<clang::SourceRange> linkDeclRange) {
        proto::InlayHintLablePart lable{
            .value = std::format(": {}", deduced.getAsString(policy)),
            .tooltip = blank(),
        };

        if(linkDeclRange.has_value())
            lable.Location = {.uri = docuri, .range = toLspRange(*linkDeclRange)};

        proto::InlayHint hint{
            .position = toLspPosition(identRange.getEnd()),
            .lable = {std::move(lable)},
            .kind = proto::InlayHintKind::Type,
        };

        result.push_back(std::move(hint));
    }

    // If `expr` spells a single unqualified identifier, return that name, otherwise, return an
    // empty string.
    static llvm::StringRef takeExprIdentifier(const clang::Expr* expr) {
        auto spelled = expr->IgnoreUnlessSpelledInSource();
        if(auto* declRef = llvm::dyn_cast<clang::DeclRefExpr>(spelled))
            if(!declRef->getQualifier())
                return declRef->getDecl()->getName();
        if(auto* member = llvm::dyn_cast<clang::MemberExpr>(spelled))
            if(!member->getQualifier() && member->isImplicitAccess())
                return member->getMemberDecl()->getName();
        return {};
    }

    bool needHintArgument(const clang::ParmVarDecl* param, const clang::Expr* arg) {
        // Skip anonymous parameters.
        if(param->getName().empty())
            return false;

        // Skip if the argument is a single name and it matches the parameter exactly.
        if(param->getName().equals_insensitive(takeExprIdentifier(arg)))
            return false;

        // Skip if the argument is preceded by any hand-written hint /*paramName*/.
        auto range = arg->getSourceRange();
        auto firstChar = src->getCharacterData(range.getBegin());
        auto length = static_cast<size_t>(src->getCharacterData(range.getEnd()) - firstChar);
        if(auto text = llvm::StringRef{firstChar, length};
           text.contains("/*") && text.contains("*/"))
            return false;

        return true;
    }

    bool isPassedAsMutableLValueRef(const clang::ParmVarDecl* param) {
        auto qual = param->getType();
        return qual->isLValueReferenceType() && !qual.getNonReferenceType().isConstQualified();
    }

    void collectArgumentHint(llvm::ArrayRef<const clang::ParmVarDecl*> params,
                             llvm::ArrayRef<const clang::Expr*> args) {
        for(size_t i = 0; i < params.size() && i < args.size(); ++i) {
            // Pack expansion and default argument is always the tail of arguments.
            if(llvm::isa<clang::PackExpansionExpr>(args[i]) ||
               llvm::isa<clang::CXXDefaultArgExpr>(args[i]))
                break;

            if(!needHintArgument(params[i], args[i]))
                continue;

            // Only hint reference for mutable lvalue reference.
            const bool hintRef = isPassedAsMutableLValueRef(params[i]);
            proto::InlayHintLablePart lable{
                .value = std::format("{}{}:", params[i]->getName(), hintRef ? "&" : ""),
                .tooltip = blank(),
                .Location = proto::Location{.uri = docuri,
                                            .range = toLspRange(params[i]->getSourceRange())}
            };

            proto::InlayHint hint{
                .position = toLspPosition(args[i]->getSourceRange().getBegin()),
                .lable = {std::move(lable)},
                .kind = proto::InlayHintKind::Parameter,
            };

            result.push_back(std::move(hint));
        }
    }

    bool TraverseDecl(clang::Decl* decl) {
        if(!decl || needFilter(decl->getSourceRange()))
            return true;

        return Base::TraverseDecl(decl);
    }

    bool VisitVarDecl(const clang::VarDecl* decl) {
        // Hint for indivadual element of structure binding.
        if(auto bind = llvm::dyn_cast<clang::DecompositionDecl>(decl)) {
            for(auto* binding: bind->bindings()) {
                // Hint for used variable only.
                if(auto type = binding->getType(); !type.isNull() && !type->isDependentType()) {
                    // Hint at the end position of identifier.
                    auto name = binding->getName();
                    collectAutoDeclHint(type.getCanonicalType(),
                                        binding->getBeginLoc().getLocWithOffset(name.size()),
                                        decl->getSourceRange());
                }
            }
            return true;
        }

        /// skip dependent type.
        clang::QualType qty = decl->getType();
        if(qty.isNull() || qty->isDependentType())
            return true;

        if(auto at = qty->getContainedAutoType()) {
            // Use most recent decl as the link of hint string.
            /// FIXME:
            /// Shall we use the first decl as the link of hint string?
            std::optional<clang::SourceRange> originDeclRange;
            if(auto mrd = decl->getMostRecentDecl())
                originDeclRange = mrd->getSourceRange();

            auto tailOfIdentifier = decl->getLocation().getLocWithOffset(decl->getName().size());
            collectAutoDeclHint(qty, tailOfIdentifier, originDeclRange);
        }
        return true;
    }

    static bool isBuiltinFnCall(const clang::CallExpr* expr) {
        namespace btin = clang::Builtin;
        switch(expr->getBuiltinCallee()) {
            case btin::BIaddressof:
            case btin::BIas_const:
            case btin::BIforward:
            case btin::BImove:
            case btin::BImove_if_noexcept: return true;
            default: return false;
        }
    }

    /// Try find the FunctionProtoType of a CallExpr which callee is a function pointer.
    static auto detectCallViaFnPointer(const clang::Expr* call)
        -> std::optional<clang::FunctionProtoTypeLoc> {

        auto nake = call->IgnoreParenCasts();
        clang::TypeLoc target;

        if(auto* tydef = nake->getType().getTypePtr()->getAs<clang::TypedefType>())
            target = tydef->getDecl()->getTypeSourceInfo()->getTypeLoc();
        else if(auto* declRef = llvm::dyn_cast<clang::DeclRefExpr>(nake))
            if(auto* varDecl = llvm::dyn_cast<clang::VarDecl>(declRef->getDecl()))
                target = varDecl->getTypeSourceInfo()->getTypeLoc();

        if(!target)
            return std::nullopt;

        // Unwrap types that may be wrapping the function type.
        while(true) {
            if(auto P = target.getAs<clang::PointerTypeLoc>())
                target = P.getPointeeLoc();
            else if(auto A = target.getAs<clang::AttributedTypeLoc>())
                target = A.getModifiedLoc();
            else if(auto P = target.getAs<clang::ParenTypeLoc>())
                target = P.getInnerLoc();
            else
                break;
        }

        if(auto proto = target.getAs<clang::FunctionProtoTypeLoc>())
            return proto;

        return std::nullopt;
    }

    bool VisitCallExpr(const clang::CallExpr* call) {
        // Don't hint for UDL operator like `operaotr ""_str` , and builtin funtion.
        if(!call || llvm::isa<clang::UserDefinedLiteral>(call) || isBuiltinFnCall(call))
            return true;

        // They were handled in  `VisitCXXMemberCallExpr`. `VisitCXXOperatorCallExpr`.
        if(llvm::isa<clang::CXXMemberCallExpr>(call) || llvm::isa<clang::CXXOperatorCallExpr>(call))
            return true;

        // For a CallExpr, there are 2 case of Callee:
        //     1. An object which has coresponding FunctionDecl, free function or method.
        //     2. A function pointer, which has no FunctionDecl but FunctionProtoTypeLoc.

        // Use FunctionDecl if callee is a free function or method.
        const clang::FunctionDecl* fndecl = nullptr;
        const clang::Decl* calleeDecl = call->getCalleeDecl();
        if(auto decl = llvm::dyn_cast<clang::FunctionDecl>(calleeDecl))
            fndecl = decl;
        else if(auto tfndecl = llvm::dyn_cast<clang::FunctionTemplateDecl>(calleeDecl))
            fndecl = tfndecl->getTemplatedDecl();

        if(fndecl)
            // free function
            collectArgumentHint(fndecl->parameters(), {call->getArgs(), call->getNumArgs()});
        else if(auto proto = detectCallViaFnPointer(call->getCallee()); proto.has_value())
            // function pointer
            collectArgumentHint(proto->getParams(), {call->getArgs(), call->getNumArgs()});

        return true;
    }

    bool VisitCXXOperatorCallExpr(const clang::CXXOperatorCallExpr* call) {
        // Do not hint paramters for operator overload except `operator()`, and `operator[]` with
        // only one parameter.
        auto opkind = call->getOperator();
        if(opkind == clang::OO_Call || opkind == clang::OO_Subscript && call->getNumArgs() != 1) {
            auto method = llvm::dyn_cast<clang::CXXMethodDecl>(call->getCalleeDecl());

            llvm::ArrayRef<const clang::ParmVarDecl*> params{method->parameters()};
            llvm::ArrayRef<const clang::Expr*> args{call->getArgs(), call->getNumArgs()};

            // Skip `this` parameter declaration if callee is CXXMethodDecl.
            if(!method->hasCXXExplicitFunctionObjectParameter())
                args = args.drop_front();

            collectArgumentHint(params, args);
        }

        return true;
    }

    static bool isSimpleSetter(const clang::CXXMethodDecl* md) {
        if(md->getNumParams() != 1)
            return false;

        auto name = md->getName();
        if(!name.starts_with_insensitive("set"))
            return false;

        // Check that the part after "set" matches the name of the parameter (ignoring case). The
        // idea here is that if the parameter name differs, it may contain extra information that
        // may be useful to show in a hint, as in:
        //   void setTimeout(int timeoutMillis);
        // The underscores in FunctionName and Parameter will be ignored.
        llvm::SmallString<32> param, fnname;
        for(auto c: name.drop_front(3))
            if(c != '_')
                fnname.push_back(c);

        for(auto c: md->getParamDecl(0)->getName())
            if(c != '_')
                param.push_back(c);

        return fnname.equals_insensitive(param);
    }

    bool VisitCXXMemberCallExpr(const clang::CXXMemberCallExpr* call) {
        auto callee = llvm::dyn_cast<clang::FunctionDecl>(call->getCalleeDecl());

        // Do not hint move / copy constructor call.
        if(auto ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(callee))
            if(ctor->isCopyOrMoveConstructor())
                return true;

        // Do not hint simple setter function call. e.g. `setX(1)`.
        if(auto md = llvm::dyn_cast<clang::CXXMethodDecl>(callee))
            if(isSimpleSetter(md))
                return true;

        llvm::ArrayRef<const clang::ParmVarDecl*> params{callee->parameters()};
        llvm::ArrayRef<const clang::Expr*> args{call->getArgs(), call->getNumArgs()};

        // Skip `this` parameter declaration if callee is CXXMethodDecl.
        if(auto md = llvm::dyn_cast<clang::CXXMethodDecl>(callee))
            if(md->hasCXXExplicitFunctionObjectParameter())
                args = args.drop_front();

        collectArgumentHint(params, args);
        return true;
    }

    void collectReturnTypeHint(clang::SourceLocation hintLoc, clang::QualType retType,
                               clang::SourceRange retTypeDeclRange) {
        proto::InlayHintLablePart lable{
            .value = std::format("-> {}", retType.getAsString(policy)),
            .tooltip = blank(),
            .Location = proto::Location{.uri = docuri, .range = toLspRange(retTypeDeclRange)}
        };

        proto::InlayHint hint{
            .position = toLspPosition(hintLoc),
            .lable = {std::move(lable)},
            .kind = proto::InlayHintKind::Parameter,
        };

        result.push_back(std::move(hint));
    }

    bool VisitFunctionDecl(const clang::FunctionDecl* decl) {
        // Hint return type for function declaration.
        if(auto proto = llvm::dyn_cast<clang::FunctionProtoType>(decl->getType().getTypePtr()))
            if(proto->hasTrailingReturn())
                return true;

        if(auto fnTypeLoc = decl->getFunctionTypeLoc())
            // Hint for function declaration with `auto` or `decltype(...)` return type.
            if(fnTypeLoc.getReturnLoc().getContainedAutoTypeLoc())
                // Right side of ')' in parameter list.
                collectReturnTypeHint(fnTypeLoc.getRParenLoc().getLocWithOffset(1),
                                      decl->getReturnType(),
                                      decl->getSourceRange());

        return true;
    }

    bool VisitLambdaExpr(const clang::LambdaExpr* expr) {
        clang::FunctionDecl* decl = expr->getCallOperator();
        if(expr->hasExplicitResultType())
            return true;

        // where to place the hint position, in default it is an invalid value.
        clang::SourceLocation hintLoc = {};
        if(!expr->hasExplicitParameters())
            // left side of '{' before the lambda body.
            hintLoc = expr->getCompoundStmtBody()->getLBracLoc();
        else if(auto fnTypeLoc = decl->getFunctionTypeLoc())
            // right side of ')'.
            hintLoc = fnTypeLoc.getRParenLoc().getLocWithOffset(1);

        if(hintLoc.isValid())
            collectReturnTypeHint(hintLoc, decl->getReturnType(), decl->getSourceRange());

        return true;
    }
};

}  // namespace

namespace feature {

json::Value inlayHintCapability(json::Value InlayHintClientCapabilities) {
    return {};
}

/// Compute inlay hints for a document in given range and config.
proto::InlayHintsResult inlayHints(proto::InlayHintParams param, ASTInfo& ast,
                                   const config::InlayHintConfig& config) {
    clang::SourceManager* src = &ast.srcMgr();

    /// FIXME:
    /// Take 0-0 based Lsp Location from `param.range` and convert it to clang 1-1 based
    /// source location.
    clang::SourceRange fixedRange;  // = range...

    // In default, use the whole main file as the restrict range.
    if(fixedRange.isInvalid()) {
        clang::FileID main = src->getMainFileID();
        fixedRange = {src->getLocForStartOfFile(main), src->getLocForEndOfFile(main)};
    }

    InlayHintCollector collector{
        .config = config,
        .limit = fixedRange,
        .docuri = std::move(param.textDocument.uri),
        .policy = ast.context().getPrintingPolicy(),
    };
    collector.src = src;

    collector.TraverseTranslationUnitDecl(ast.tu());

    return std::move(collector.result);
}

}  // namespace feature

}  // namespace clice
