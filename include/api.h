#ifndef FEED_API_H
#define FEED_API_H

#include <string>
#include "storage.h"
#include "search.h"
#include "github_client.h"
#include "classifier.h"

namespace feed {
namespace api {

// Get recent commits, optionally filtered by repository
// Returns JSON array of commits
std::string get_recent_commits(Storage& db,
                               const std::string& repo = "",
                               int limit = 50);

// Find commits similar to the query using TF-IDF
// Returns JSON array of commits with similarity scores
std::string find_similar_commits(Storage& db,
                                  SearchEngine& engine,
                                  const std::string& query,
                                  int top_k = 5);

// Get commits by classification tag within the last N days
// Returns JSON array of commits
std::string get_tagged_commits(Storage& db,
                                const std::string& tag,
                                int days = 7);

// Get activity summary for a repository
// Returns JSON object with commit statistics
std::string get_repo_activity_summary(Storage& db,
                                       const std::string& repo,
                                       int days = 7);

// Sync commits from organization repositories (with optional filter)
// Returns JSON object with sync results
std::string update_org_commits(GitHubClient& client,
                                Storage& db,
                                Classifier& classifier,
                                SearchEngine& engine,
                                const RepoFilter& filter = RepoFilter{});

// Rebuild search index (vocabulary and embeddings)
// Returns JSON object with rebuild results
std::string rebuild_search_index(Storage& db, SearchEngine& engine);

// Get available classification tags
// Returns JSON array of tag names
std::string get_available_tags(Classifier& classifier);

}  // namespace api
}  // namespace feed

#endif  // FEED_API_H
