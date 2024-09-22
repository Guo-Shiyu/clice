#include <gtest/gtest.h>
#include <Index/Index.h>
#include <AST/Compiler.h>

using namespace clice;

TEST(clice_test, index) {
    std::vector<const char*> compileArgs = {
        "clang++",
        "-std=c++20",
        "main.cpp",
        "-resource-dir",
        "/home/ykiko/C++/clice2/build/lib/clang/20",
    };
    const char* code = R"(
template<typename T, typename U> struct X {};

template<typename T> struct X<T, T> {};

void f() {
    X<int, int> x;
    X<char, int> y;
}
)";
    auto invocation = createInvocation("main.cpp", code, compileArgs);
    auto instance = createInstance(std::move(invocation));

    auto indexConsumer = std::make_shared<IndexConsumer>();
    clang::index::IndexingOptions indexOptions;
    auto action = clang::index::createIndexingAction(indexConsumer, indexOptions);

    if(!action->BeginSourceFile(*instance, instance->getFrontendOpts().Inputs[0])) {
        return;
    }

    auto consumer =
        clang::index::createIndexingASTConsumer(indexConsumer, indexOptions, instance->getPreprocessorPtr());
    instance->setASTConsumer(std::move(consumer));

    if(auto err = action->Execute()) {
        llvm::errs() << "Error while indexing: " << err << "\n";
        std::terminate();
    }

    instance->getASTContext().getTranslationUnitDecl()->dump();

    action->EndSourceFile();
}