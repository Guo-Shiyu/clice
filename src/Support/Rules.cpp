#include "Support/Rules.h"
#include "../Compiler/Driver.h"

namespace clice {

bool has_effect(const config::Rule& rule) {
    if(rule.patterns.empty()) {
        return false;
    }

    const bool has_readonly_specified = rule.readonly == "always" || rule.readonly == "never";
    return has_readonly_specified || !rule.append.empty() || !rule.remove.empty();
}

namespace {

void parse_rule_opt(const llvm::SmallVectorImpl<std::string>& options,
                    ArgumentParser& parser,
                    StringSet& pool,
                    DenseDeriverOptions& storage,
                    const char* error_context) {
    using DriverOptID = DenseDeriverOptions::DriverOptID;

    llvm::SmallVector<const char*> args;
    for(auto& option: options) {
        args.push_back(option.c_str());
    }

    parser.parse(
        args,
        [&storage, &pool](const std::unique_ptr<llvm::opt::Arg>& arg) {
            DriverOptID id = static_cast<DriverOptID>(arg->getOption().getID());
            if(arg->getNumValues() == 0) {
                storage.options.push_back({id, StringSet::InvalidID});
            } else {
                /// TODO: is it possible that one option have multiple values?
                // For options with 1 or multiple values, we only store the first one.
                storage.options.push_back({id, pool.get(arg->getValue(0))});
            }
        },
        [&](int index, int count) {
            LOG_WARN("missing argument index: {}, count: {} when parse rule's {} ",
                     index,
                     count,
                     error_context);
        });
}

}  // namespace

auto Rule::create(config::Rule rule,
                  llvm::SmallVectorImpl<std::pair<uint32_t, GlobParseError>>& errors,
                  StringSet& pool) -> std::optional<Rule> {
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
            errors.emplace_back(index, std::move(glob).error());
        }
    }

    // If no valid pattern, return nullopt.
    if(result.pattern.empty()) {
        return std::nullopt;
    }

    ArgumentParser parser{&pool.get_allocator()};
    parse_rule_opt(rule.append, parser, pool, result.append, "append");
    parse_rule_opt(rule.remove, parser, pool, result.remove, "remove");

    // Then check whether the rule has any effect, because all options in append/remove
    // may be unrecognized and ignored.
    if(result.append.options.empty() && result.remove.options.empty() &&
       result.readonly == ReadModeFlag::Auto) {
        return std::nullopt;
    }

    return result;
}

void Rule::apply(StringSet& pool, std::vector<const char*> arguments) const {
    /// FIXME: apply the rule in CompilationDatabase::derive_arguments
}

std::optional<const Rule*> RuleManager::lookup(llvm::StringRef file) {
    if(rules.empty()) {
        return std::nullopt;
    }

    // Find in cache first
    InlineCache* quick = std::ranges::find_if(caches, [file](auto& c) { return c.file == file; });
    if(quick != caches.end()) {
        if(quick != caches.begin()) {
            // Move the accessed cache entry to the front (most recently used)
            std::iter_swap(quick, caches.begin());
        }
        return quick->rule;
    }

    // Not found in cache, do a full search
    const auto first_match = std::ranges::find_if(rules, [&](auto& r) { return r->match(file); });
    if(first_match == rules.end()) {
        return std::nullopt;
    }

    if(caches.size() < MaxCacheSize) {
        caches.emplace_back(InlineCache{file, first_match->get()});
    } else {
        // Update the inline cache, but not change the order.
        InlineCache& entry = caches.back();
        entry.file = file;
        entry.rule = first_match->get();
    }

    return first_match->get();
}

}  // namespace clice
