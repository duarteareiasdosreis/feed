#ifndef FEED_COMMANDS_H
#define FEED_COMMANDS_H

#include <string>
#include <vector>
#include <functional>
#include "github_client.h"

namespace feed {
namespace commands {

// Callback for progress messages during long operations
using ProgressCallback = std::function<void(const std::string&)>;

// Default no-op callback
inline void no_progress(const std::string&) {}

// All commands return JSON strings
// Success: JSON object with result data
// Error: JSON object with "error" field

// --- Configuration Commands ---

// Initialize feed with organization and filters
// Returns: {"success": true, "org": "...", "db_path": "...", ...}
std::string init(const std::string& org,
                 const std::string& token,
                 const RepoFilter& filter,
                 bool store_token = true);

// Get current configuration
// Returns: {"org": "...", "token": "***", "filter": {...}}
std::string get_config();

// Get sync status (fast, local-only - no API calls)
// Returns: {"last_sync": "...", "repos": [...], "filter": {...}, "total_commits": N}
std::string get_sync_status();

// Add repositories to include list
// Returns: {"added": [...], "total_repos": N}
std::string add_repos(const std::vector<std::string>& repos);

// Remove repositories from include list
// Returns: {"removed": [...], "not_found": [...], "total_repos": N}
std::string remove_repos(const std::vector<std::string>& repos);

// --- Repository Commands ---

// List repositories matching current filters
// Returns: {"count": N, "repositories": [...]}
std::string list_repos(ProgressCallback progress = no_progress);

// Sync commits from repositories
// Returns: {"new_commits": N, "repos_synced": N, "purged_commits": N, ...}
std::string sync(ProgressCallback progress = no_progress);

// --- Query Commands ---

// Get recent commits
// Returns: {"commits": [...], "count": N}
std::string get_recent_commits(const std::string& repo = "", int limit = 50);

// Find similar commits using semantic search
// Returns: {"results": [...], "query": "...", "count": N}
std::string find_similar(const std::string& query, int top_k = 5);

// Get commits by tag
// Returns: {"commits": [...], "tag": "...", "days": N, "count": N}
std::string get_tagged(const std::string& tag, int days = 7, int limit = 20);

// Get repository activity summary
// Returns: {"repo": "...", "total_commits": N, "top_authors": [...], ...}
std::string get_summary(const std::string& repo, int days = 7);

// --- Maintenance Commands ---

// Rebuild search index
// Returns: {"message": "...", "vocabulary_size": N, "commits_indexed": N}
std::string rebuild_index();

// Get available tags
// Returns: {"tags": [...]}
std::string get_tags();

// --- Helper Functions ---

// Check if result is an error (has "error" field)
bool is_error(const std::string& json_result);

// Extract error message from result
std::string get_error_message(const std::string& json_result);

}  // namespace commands
}  // namespace feed

#endif  // FEED_COMMANDS_H
