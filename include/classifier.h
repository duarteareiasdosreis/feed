#ifndef FEED_CLASSIFIER_H
#define FEED_CLASSIFIER_H

#include <string>
#include <vector>
#include <unordered_map>

namespace feed {

class Classifier {
public:
    Classifier();

    // Classify a commit message and return matching tags
    std::vector<std::string> classify(const std::string& message);

    // Get all available tags
    std::vector<std::string> get_available_tags() const;

private:
    // Map of tag -> keywords
    std::unordered_map<std::string, std::vector<std::string>> tag_keywords_;

    // Convert string to lowercase
    static std::string to_lower(const std::string& str);

    // Check if message contains keyword (case-insensitive)
    static bool contains_keyword(const std::string& message, const std::string& keyword);
};

}  // namespace feed

#endif  // FEED_CLASSIFIER_H
