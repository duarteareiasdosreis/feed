#ifndef FEED_CONFIG_H
#define FEED_CONFIG_H

#include <string>

namespace feed {
namespace config {

// GitHub API settings
constexpr const char* GITHUB_API_BASE = "https://api.github.com";
constexpr int DEFAULT_COMMITS_PER_PAGE = 100;
constexpr int MAX_COMMITS_PER_REPO = 1000;

// Storage settings
constexpr const char* DEFAULT_DB_PATH = "commits.db";
constexpr const char* CONFIG_FILE = ".feed_config";

// Search settings
constexpr int MAX_VOCABULARY_SIZE = 10000;
constexpr int DEFAULT_TOP_K = 5;
constexpr float VOCABULARY_REBUILD_THRESHOLD = 0.10f;  // 10% new commits

// Rate limiting
constexpr int RATE_LIMIT_PER_HOUR = 5000;
constexpr int RATE_LIMIT_DELAY_MS = 100;

// Embedding vector dimension (dynamic based on vocabulary)
constexpr int MIN_EMBEDDING_DIM = 100;

}  // namespace config
}  // namespace feed

#endif  // FEED_CONFIG_H
