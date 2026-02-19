#include "classifier.h"
#include <algorithm>
#include <cctype>

namespace feed {

Classifier::Classifier() {
    // Initialize tag keywords
    tag_keywords_["optimization"] = {
        "perf", "optimize", "optimise", "speed", "fast", "faster",
        "cache", "caching", "memory", "efficient", "efficiency",
        "performance", "slow", "latency", "throughput", "benchmark"
    };

    tag_keywords_["experimental"] = {
        "experiment", "experimental", "poc", "proof of concept",
        "prototype", "wip", "work in progress", "spike", "try",
        "attempt", "draft", "test out", "exploring"
    };

    tag_keywords_["temporary"] = {
        "temp", "temporary", "hack", "workaround", "fixme", "todo",
        "quick fix", "quickfix", "hotfix", "hot fix", "band-aid",
        "bandaid", "stopgap", "interim", "placeholder"
    };

    tag_keywords_["refactor"] = {
        "refactor", "refactoring", "restructure", "reorganize",
        "reorganise", "clean up", "cleanup", "simplify", "simplification",
        "consolidate", "deduplicate", "extract", "inline", "rename"
    };

    tag_keywords_["architectural_change"] = {
        "architect", "architecture", "design", "pattern", "migration",
        "migrate", "overhaul", "rewrite", "redesign", "restructure",
        "foundation", "infrastructure", "framework", "modular",
        "decouple", "coupling", "dependency"
    };

    tag_keywords_["bugfix"] = {
        "fix", "bug", "issue", "error", "crash", "broken", "repair",
        "resolve", "patch", "correct", "debug", "regression"
    };

    tag_keywords_["feature"] = {
        "add", "new", "feature", "implement", "introduce", "support",
        "enable", "allow", "capability"
    };

    tag_keywords_["documentation"] = {
        "doc", "docs", "documentation", "readme", "comment", "comments",
        "javadoc", "docstring", "explain", "clarify"
    };

    tag_keywords_["testing"] = {
        "test", "tests", "testing", "spec", "specs", "unittest",
        "unit test", "integration test", "e2e", "coverage"
    };

    tag_keywords_["security"] = {
        "security", "secure", "vulnerability", "cve", "auth",
        "authentication", "authorization", "permission", "encrypt",
        "decrypt", "sanitize", "xss", "injection", "csrf"
    };

    tag_keywords_["dependency"] = {
        "dependency", "dependencies", "upgrade", "update", "bump",
        "version", "package", "npm", "pip", "cargo", "maven", "gradle"
    };
}

std::string Classifier::to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool Classifier::contains_keyword(const std::string& message, const std::string& keyword) {
    std::string lower_message = to_lower(message);
    std::string lower_keyword = to_lower(keyword);

    // For multi-word keywords, do direct substring search
    if (keyword.find(' ') != std::string::npos) {
        return lower_message.find(lower_keyword) != std::string::npos;
    }

    // For single words, check word boundaries
    size_t pos = 0;
    while ((pos = lower_message.find(lower_keyword, pos)) != std::string::npos) {
        // Check if it's at word boundary
        bool start_ok = (pos == 0) || !std::isalnum(static_cast<unsigned char>(lower_message[pos - 1]));
        bool end_ok = (pos + lower_keyword.length() >= lower_message.length()) ||
                      !std::isalnum(static_cast<unsigned char>(lower_message[pos + lower_keyword.length()]));

        if (start_ok && end_ok) {
            return true;
        }
        pos++;
    }

    return false;
}

std::vector<std::string> Classifier::classify(const std::string& message) {
    std::vector<std::string> tags;

    for (const auto& [tag, keywords] : tag_keywords_) {
        for (const auto& keyword : keywords) {
            if (contains_keyword(message, keyword)) {
                tags.push_back(tag);
                break;  // Don't add same tag twice
            }
        }
    }

    return tags;
}

std::vector<std::string> Classifier::get_available_tags() const {
    std::vector<std::string> tags;
    tags.reserve(tag_keywords_.size());

    for (const auto& [tag, _] : tag_keywords_) {
        tags.push_back(tag);
    }

    std::sort(tags.begin(), tags.end());
    return tags;
}

}  // namespace feed
