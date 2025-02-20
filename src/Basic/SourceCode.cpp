#include "Basic/SourceCode.h"

#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

namespace clice {

llvm::StringRef getFileContent(const clang::SourceManager& SM, clang::FileID fid) {
    return SM.getBufferData(fid);
}

std::uint32_t getTokenLength(const clang::SourceManager& SM, clang::SourceLocation location) {
    return clang::Lexer::MeasureTokenLength(location, SM, {});
}

llvm::StringRef getTokenSpelling(const clang::SourceManager& SM, clang::SourceLocation location) {
    return llvm::StringRef(SM.getCharacterData(location), getTokenLength(SM, location));
}

void tokenize(llvm::StringRef content,
              llvm::unique_function<bool(const clang::Token&)> callback,
              bool ignoreComments,
              const clang::LangOptions* langOpts) {
    clang::LangOptions defaultLangOpts;
    defaultLangOpts.CPlusPlus = 1;
    defaultLangOpts.CPlusPlus26 = 1;
    defaultLangOpts.LineComment = !ignoreComments;

    clang::Lexer lexer(clang::SourceLocation::getFromRawEncoding(1),
                       langOpts ? *langOpts : defaultLangOpts,
                       content.begin(),
                       content.begin(),
                       content.end());
    lexer.SetCommentRetentionState(!ignoreComments);

    clang::Token token;
    while(true) {
        bool end = lexer.LexFromRawLexer(token);
        if(!callback(token)) {
            break;
        }

        if(end || token.is(clang::tok::eof)) {
            break;
        }
    }
}

}  // namespace clice
