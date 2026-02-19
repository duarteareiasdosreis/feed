#ifndef FEED_GITHUB_CLIENT_H
#define FEED_GITHUB_CLIENT_H

#include <string>
#include <vector>
#include <set>
#include "storage.h"

namespace feed {

// Repository metadata from GitHub API
struct RepoInfo {
    std::string name;
    std::string language;
    std::vector<std::string> topics;
    std::string pushed_at;  // Last push timestamp
    bool archived = false;
    bool fork = false;
    int stargazers_count = 0;
};

// Filter criteria for repository selection
struct RepoFilter {
    // Language filters (empty = all languages)
    std::set<std::string> languages;

    // Topic filters (empty = all topics, repo must have ANY of these)
    std::set<std::string> topics;

    // Explicit include list (if non-empty, ONLY these repos)
    std::set<std::string> include_repos;

    // Explicit exclude list
    std::set<std::string> exclude_repos;

    // Only repos with activity in last N days (0 = no filter)
    int active_days = 0;

    // Maximum number of repos to return (0 = no limit)
    int max_repos = 0;

    // Include archived repos
    bool include_archived = false;

    // Include forked repos
    bool include_forks = true;

    // Minimum stars (0 = no filter)
    int min_stars = 0;
};

class GitHubClient {
public:
    GitHubClient(const std::string& org, const std::string& token);
    ~GitHubClient();

    // Non-copyable
    GitHubClient(const GitHubClient&) = delete;
    GitHubClient& operator=(const GitHubClient&) = delete;

    // Get list of repository names with filtering
    std::vector<std::string> list_repos(const RepoFilter& filter = RepoFilter{});

    // Get full repository info (for inspection/debugging)
    std::vector<RepoInfo> list_repos_detailed(const RepoFilter& filter = RepoFilter{});

    // Fetch commits from a repository
    // since: ISO 8601 timestamp (e.g., "2024-01-01T00:00:00Z")
    std::vector<Commit> fetch_commits(const std::string& repo,
                                       int limit = 100,
                                       const std::string& since = "");

    // Get organization name
    const std::string& org() const { return org_; }

private:
    std::string org_;
    std::string token_;

    // Make HTTP request to GitHub API
    std::string api_request(const std::string& endpoint);

    // Parse Link header for pagination
    std::string parse_next_page_url(const std::string& link_header);

    // Parse repository JSON into RepoInfo
    RepoInfo parse_repo_info(const std::string& json_str);

    // Fetch info for a single repo by name
    RepoInfo fetch_repo_info(const std::string& repo_name);

    // Check if repo passes filter criteria
    bool matches_filter(const RepoInfo& repo, const RepoFilter& filter);

    // Extract top-level directories from commit files
    std::vector<std::string> extract_top_level_paths(const std::string& repo,
                                                      const std::string& commit_sha);
};

}  // namespace feed

#endif  // FEED_GITHUB_CLIENT_H
