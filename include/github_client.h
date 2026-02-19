#ifndef FEED_GITHUB_CLIENT_H
#define FEED_GITHUB_CLIENT_H

#include <string>
#include <vector>
#include "storage.h"

namespace feed {

class GitHubClient {
public:
    GitHubClient(const std::string& org, const std::string& token);
    ~GitHubClient();

    // Non-copyable
    GitHubClient(const GitHubClient&) = delete;
    GitHubClient& operator=(const GitHubClient&) = delete;

    // Get list of repository names in the organization
    std::vector<std::string> list_repos();

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

    // Extract top-level directories from commit files
    std::vector<std::string> extract_top_level_paths(const std::string& repo,
                                                      const std::string& commit_sha);
};

}  // namespace feed

#endif  // FEED_GITHUB_CLIENT_H
