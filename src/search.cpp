#include "search.h"
#include "config.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>

namespace feed {

using json = nlohmann::json;

// Common English stopwords
const std::unordered_set<std::string> SearchEngine::STOPWORDS = {
    "a", "an", "and", "are", "as", "at", "be", "by", "for", "from",
    "has", "he", "in", "is", "it", "its", "of", "on", "that", "the",
    "to", "was", "were", "will", "with", "the", "this", "but", "they",
    "have", "had", "what", "when", "where", "who", "which", "why", "how",
    "all", "each", "every", "both", "few", "more", "most", "other", "some",
    "such", "no", "nor", "not", "only", "own", "same", "so", "than", "too",
    "very", "just", "can", "could", "should", "would", "may", "might", "must",
    "shall", "do", "does", "did", "doing", "done", "being", "been", "am",
    "i", "me", "my", "myself", "we", "our", "ours", "ourselves", "you",
    "your", "yours", "yourself", "yourselves", "him", "his", "himself",
    "she", "her", "hers", "herself", "them", "their", "theirs", "themselves"
};

SearchEngine::SearchEngine() = default;

std::vector<std::string> SearchEngine::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current_token;

    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current_token += std::tolower(static_cast<unsigned char>(c));
        } else if (!current_token.empty()) {
            // Filter stopwords and short tokens
            if (current_token.length() >= 2 &&
                STOPWORDS.find(current_token) == STOPWORDS.end()) {
                tokens.push_back(current_token);
            }
            current_token.clear();
        }
    }

    // Don't forget the last token
    if (!current_token.empty() && current_token.length() >= 2 &&
        STOPWORDS.find(current_token) == STOPWORDS.end()) {
        tokens.push_back(current_token);
    }

    return tokens;
}

std::unordered_map<std::string, float> SearchEngine::compute_tf(
    const std::vector<std::string>& tokens) {
    std::unordered_map<std::string, float> tf;

    if (tokens.empty()) return tf;

    // Count occurrences
    for (const auto& token : tokens) {
        tf[token] += 1.0f;
    }

    // Normalize by total token count
    float total = static_cast<float>(tokens.size());
    for (auto& [word, count] : tf) {
        count /= total;
    }

    return tf;
}

void SearchEngine::build_vocabulary(const std::vector<std::string>& documents) {
    // Count document frequency for each word
    std::unordered_map<std::string, int> doc_freq;
    int total_docs = static_cast<int>(documents.size());

    for (const auto& doc : documents) {
        std::unordered_set<std::string> seen;
        auto tokens = tokenize(doc);

        for (const auto& token : tokens) {
            if (seen.find(token) == seen.end()) {
                doc_freq[token]++;
                seen.insert(token);
            }
        }
    }

    // Build vocabulary and IDF scores
    vocabulary_.clear();
    idf_.clear();

    // Sort by document frequency to prioritize common terms
    std::vector<std::pair<std::string, int>> freq_list(doc_freq.begin(), doc_freq.end());
    std::sort(freq_list.begin(), freq_list.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    int index = 0;
    for (const auto& [word, df] : freq_list) {
        // Skip very rare words (appear in less than 2 documents)
        if (df < 2) continue;

        // Limit vocabulary size
        if (index >= config::MAX_VOCABULARY_SIZE) break;

        vocabulary_[word] = index++;

        // Calculate IDF: log(N / (1 + df))
        idf_[word] = std::log(static_cast<float>(total_docs) / (1.0f + df));
    }

    doc_count_at_build_ = total_docs;
}

bool SearchEngine::needs_rebuild(int current_doc_count) const {
    if (vocabulary_.empty()) return true;

    int new_docs = current_doc_count - doc_count_at_build_;
    float ratio = static_cast<float>(new_docs) / std::max(1, doc_count_at_build_);

    return ratio > config::VOCABULARY_REBUILD_THRESHOLD;
}

std::vector<float> SearchEngine::compute_embedding(const std::string& text) {
    if (vocabulary_.empty()) {
        return {};
    }

    std::vector<float> embedding(vocabulary_.size(), 0.0f);
    auto tokens = tokenize(text);
    auto tf = compute_tf(tokens);

    for (const auto& [word, term_freq] : tf) {
        auto vocab_it = vocabulary_.find(word);
        if (vocab_it != vocabulary_.end()) {
            auto idf_it = idf_.find(word);
            float idf_score = (idf_it != idf_.end()) ? idf_it->second : 1.0f;
            embedding[vocab_it->second] = term_freq * idf_score;
        }
    }

    return embedding;
}

float SearchEngine::similarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || b.empty() || a.size() != b.size()) {
        return 0.0f;
    }

    float dot_product = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;

    for (size_t i = 0; i < a.size(); i++) {
        dot_product += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    if (norm_a == 0.0f || norm_b == 0.0f) {
        return 0.0f;
    }

    return dot_product / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

std::vector<SearchResult> SearchEngine::find_similar(
    const std::string& query,
    const std::vector<Commit>& corpus,
    int top_k) {

    if (vocabulary_.empty()) {
        return {};
    }

    auto query_embedding = compute_embedding(query);
    if (query_embedding.empty()) {
        return {};
    }

    std::vector<SearchResult> results;
    results.reserve(corpus.size());

    for (const auto& commit : corpus) {
        // Compute embedding for commit message if not cached
        std::vector<float> commit_embedding;
        if (!commit.embedding.empty()) {
            commit_embedding = commit.embedding;
        } else {
            commit_embedding = compute_embedding(commit.message);
        }

        if (commit_embedding.empty()) continue;

        float sim = similarity(query_embedding, commit_embedding);
        if (sim > 0.0f) {
            results.push_back({commit, sim});
        }
    }

    // Sort by similarity descending
    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.similarity > b.similarity;
              });

    // Return top-k results
    if (static_cast<int>(results.size()) > top_k) {
        results.resize(top_k);
    }

    return results;
}

std::string SearchEngine::serialize_vocabulary() const {
    json j = vocabulary_;
    return j.dump();
}

std::string SearchEngine::serialize_idf() const {
    json j = idf_;
    return j.dump();
}

void SearchEngine::deserialize_vocabulary(const std::string& json_str) {
    if (json_str.empty()) return;

    try {
        json j = json::parse(json_str);
        vocabulary_ = j.get<std::unordered_map<std::string, int>>();
    } catch (...) {
        vocabulary_.clear();
    }
}

void SearchEngine::deserialize_idf(const std::string& json_str) {
    if (json_str.empty()) return;

    try {
        json j = json::parse(json_str);
        idf_ = j.get<std::unordered_map<std::string, float>>();
    } catch (...) {
        idf_.clear();
    }
}

}  // namespace feed
