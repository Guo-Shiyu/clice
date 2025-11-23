#include "Support/Rules.h"

namespace clice {

bool has_effect(const config::Rule& rule) {
    if(rule.patterns.empty()) {
        return false;
    }

    const bool has_readonly_specified = rule.readonly == "always" || rule.readonly == "never";
    return has_readonly_specified || !rule.append.empty() || !rule.remove.empty();
}

auto Rule::create(config::Rule rule,
                  llvm::SmallVectorImpl<std::pair<uint32_t, GlobParseError>>& errors)
    -> std::optional<Rule> {
    assert(has_effect(rule) && "Rule has no effect!");

    Rule result;

    // Parse readonly field
    if(rule.readonly == "always") {
        result.readonly = ReadModeFlag::Always;
    } else if(rule.readonly == "never") {
        result.readonly = ReadModeFlag::Never;
    } else {
        // Any other value is treated as auto.
        result.readonly = ReadModeFlag::Auto;
    }

    // Parse pattern field.
    for(auto [index, pattern]: llvm::enumerate(rule.patterns)) {
        if(auto glob = GlobPattern::create(pattern)) {
            result.pattern.push_back(std::move(glob).value());
        } else {
            errors.emplace_back(index, glob.error());
        }
    }

    // If no valid pattern, return nullopt.
    if(result.pattern.empty()) {
        return std::nullopt;
    }

    // /// Parse append field.
    // for(auto& argument: rule.append) {
    //     auto arg = get_option(argument);
    //     if(!arg) {
    //         continue;
    //     }

    //     const auto& opt = arg->getOption();
    //     DriverOptionID id = opt.getID();
    //     rewrite_arg_str(arg.get(), [&result, &pool, id](llvm::StringRef arg) {
    //         result.append[id].push_back(pool.save_cstr(arg));
    //     });
    // }

    // /// Parse remove field.
    // for(auto& argument: rule.remove) {
    //     auto arg = get_option(argument);
    //     if(!arg) {
    //         continue;
    //     }

    //     const auto& opt = arg->getOption();
    //     DriverOptionID id = opt.getID();
    //     rewrite_arg_str(arg.get(), [&result, &pool, id](llvm::StringRef arg) {
    //         result.remove[id].push_back(pool.save_cstr(arg));
    //     });
    // }

    // Then check whether the rule has any effect, because all options in append/remove
    // may be unrecognized and ignored.
    if(result.append.options.empty() && result.remove.options.empty() &&
       result.readonly == ReadModeFlag::Auto) {
        return std::nullopt;
    }

    return result;
}

}  // namespace clice
