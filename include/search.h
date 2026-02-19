#ifndef FEED_SEARCH_H
#define FEED_SEARCH_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "storage.h"

namespace feed {

struct SearchResult {
    Commit commit;
    float similarity;
};

class SearchEngine {
public:
    SearchEngine();

    // Build TF-IDF vector for a document
    std::vector<float> compute_embedding(const std::string& text);

    // Cosine similarity between two vectors
    static float similarity(const std::vector<float>& a, const std::vector<float>& b);

    // Find similar commits
    std::vector<SearchResult> find_similar(
        const std::string& query,
        const std::vector<Commit>& corpus,
        int top_k = 5
    );

    // Build vocabulary from corpus
    void build_vocabulary(const std::vector<std::string>& documents);

    // Check if vocabulary needs rebuilding
    bool needs_rebuild(int current_doc_count) const;

    // Get vocabulary size
    int vocab_size() const { return static_cast<int>(vocabulary_.size()); }

    // Serialization for persistence
    std::string serialize_vocabulary() const;
    std::string serialize_idf() const;
    void deserialize_vocabulary(const std::string& json);
    void deserialize_idf(const std::string& json);

private:
    // Tokenize text into words
    std::vector<std::string> tokenize(const std::string& text);

    // Calculate term frequency
    std::unordered_map<std::string, float> compute_tf(const std::vector<std::string>& tokens);

    // Vocabulary: word -> index in vector
    std::unordered_map<std::string, int> vocabulary_;

    // IDF scores: word -> IDF value
    std::unordered_map<std::string, float> idf_;

    // Document count when vocabulary was built
    int doc_count_at_build_ = 0;

    // Stopwords to filter
    static const std::unordered_set<std::string> STOPWORDS;
};

}  // namespace feed

#endif  // FEED_SEARCH_H
