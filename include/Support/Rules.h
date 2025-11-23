#pragma once

#include "Server/Config.h"
#include "Support/Enum.h"
#include "Support/GlobPattern.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "clang/Driver/Options.h"

namespace clice {

/// Check whether a rule from configuration file has any effect.
/// A rule is considered to have effect if it has at least one of the following:
/// 1. pattern field is not empty.
/// 2. readonly is set to "always" or "never".
/// 3. append field is not empty.
/// 4. remove field is not empty.
bool has_effect(const config::Rule& rule);

using DriverOptID = clang::driver::options::ID;

/// A derived driver option with its arguments represented as strings.
/// Used in Rule's append and remove fields.
struct DenseDeriverOptions {
    /// TODO: Use llvm::StringRef allocated by StringSet to store compile options instead of
    /// std::string.
    using ArgPair = std::pair<DriverOptID, std::string>;

    llvm::SmallVector<ArgPair> options;

    bool contains(DriverOptID id) const {
        return std::ranges::contains(options, id, &ArgPair::first);
    }
};

/// ReadOnlyFlag indicates whether a file matched by a rule is treated as readonly.
enum class ReadModeFlag : uint8_t {
    Auto,
    Always,
    Never,
};

/// For a rule objects, the append and remove fields are maps between DriverOptionID and
/// its arguments string. The arguments string will be stored in a StringPool object during
/// creation.
///
/// Invalid pattern will be ignored, if all patterns are invalid, the creation will be failed.
///
/// For options with arguments, the arguments are stored in the small vector.
/// e.g { DriverOptionID::OPT_I, {"dir1", "dir2"} } means appending -Idir1 -Idir2 to the command.
/// For options without arguments, the vector is empty.
///
/// For added arguments, the arguments will be appended to the command in order.
/// For removed arguments, all matching arguments (both DriverOptionID and it's value) will be
/// removed from the command.
/// Unrecognized options in append or remove will be ignored.
///
/// See clice.toml for details.
struct Rule {
    /// Whether the file is treated as readonly.
    /// nullopt: auto, true: always, false: never.
    ReadModeFlag readonly;

    /// The glob patterns to match the file path.
    llvm::SmallVector<GlobPattern> pattern;

    /// Additional arguments to append to the command.
    DenseDeriverOptions append;

    /// Arguments to remove from the command.
    DenseDeriverOptions remove;

    /// Try to create the rule from config::Rule. The rule from configuration file should be
    /// pre-validated by `has_effect` function.
    /// Parse errors will be returned in `errors` with it's index in `rule.patterns` if any.
    /// Unrecognized options in append or remove will be ignored.
    static auto create(config::Rule rule,
                       llvm::SmallVectorImpl<std::pair<uint32_t, GlobParseError>>& errors
                       /*, StringSet pool*/) -> std::optional<Rule>;
};

/// Manages all rules loaded from configuration file.
class RuleManager {
public:
    /// Apply rules to the given file path, and fill in the derived append/remove options.
    size_t apply(llvm::StringRef file, DenseDeriverOptions& append, DenseDeriverOptions& remove);

private:
    /// All rules loaded from configuration file in the occurrence order.
    llvm::SmallVector<std::unique_ptr<Rule>> rules;
};

}  // namespace clice
