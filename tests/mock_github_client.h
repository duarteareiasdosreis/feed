#ifndef FEED_MOCK_GITHUB_CLIENT_H
#define FEED_MOCK_GITHUB_CLIENT_H

#include <gmock/gmock.h>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "storage.h"

namespace feed {
namespace testing {

// Get current timestamp in ISO 8601 format
inline std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// Interface for GitHub client operations (for mocking)
class IGitHubClient {
public:
    virtual ~IGitHubClient() = default;
    virtual std::vector<std::string> list_repos() = 0;
    virtual std::vector<Commit> fetch_commits(const std::string& repo,
                                               int limit,
                                               const std::string& since) = 0;
    virtual const std::string& org() const = 0;
};

// Mock implementation
class MockGitHubClient : public IGitHubClient {
public:
    MOCK_METHOD(std::vector<std::string>, list_repos, (), (override));
    MOCK_METHOD(std::vector<Commit>, fetch_commits,
                (const std::string& repo, int limit, const std::string& since),
                (override));
    MOCK_METHOD(const std::string&, org, (), (const, override));
};

// Test fixture helper to create sample commits
inline Commit make_test_commit(const std::string& repo,
                               const std::string& hash,
                               const std::string& author,
                               const std::string& message,
                               const std::string& timestamp = "2024-01-15T10:30:00Z") {
    Commit c;
    c.repo_name = repo;
    c.commit_hash = hash;
    c.author = author;
    c.message = message;
    c.timestamp = timestamp;
    return c;
}

}  // namespace testing
}  // namespace feed

#endif  // FEED_MOCK_GITHUB_CLIENT_H
