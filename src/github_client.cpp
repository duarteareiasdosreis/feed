#include "github_client.h"
#include "config.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <set>
#include <regex>
#include <thread>
#include <chrono>

namespace feed {

using json = nlohmann::json;

namespace {

// Callback for libcurl to write response data
size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Callback for libcurl to capture response headers
size_t header_callback(char* buffer, size_t size, size_t nitems, std::string* userp) {
    size_t total_size = size * nitems;
    userp->append(buffer, total_size);
    return total_size;
}

}  // anonymous namespace

GitHubClient::GitHubClient(const std::string& org, const std::string& token)
    : org_(org), token_(token) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

GitHubClient::~GitHubClient() {
    curl_global_cleanup();
}

std::string GitHubClient::api_request(const std::string& endpoint) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::string url;
    if (endpoint.find("http") == 0) {
        url = endpoint;
    } else {
        url = std::string(config::GITHUB_API_BASE) + endpoint;
    }

    std::string response_body;
    std::string response_headers;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "User-Agent: feed-cli/1.0");

    if (!token_.empty()) {
        std::string auth_header = "Authorization: Bearer " + token_;
        headers = curl_slist_append(headers, auth_header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw std::runtime_error("Curl request failed: " + std::string(curl_easy_strerror(res)));
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (http_code == 403) {
        // Check for rate limiting
        try {
            json error = json::parse(response_body);
            if (error.contains("message") &&
                error["message"].get<std::string>().find("rate limit") != std::string::npos) {
                throw std::runtime_error("GitHub API rate limit exceeded");
            }
        } catch (json::exception&) {}
        throw std::runtime_error("GitHub API access forbidden (403)");
    }

    if (http_code == 404) {
        throw std::runtime_error("GitHub resource not found (404): " + url);
    }

    if (http_code >= 400) {
        throw std::runtime_error("GitHub API error: HTTP " + std::to_string(http_code));
    }

    // Small delay to respect rate limits
    std::this_thread::sleep_for(std::chrono::milliseconds(config::RATE_LIMIT_DELAY_MS));

    return response_body;
}

std::string GitHubClient::parse_next_page_url(const std::string& link_header) {
    // Parse Link header to find next page
    // Format: <url>; rel="next", <url>; rel="last"
    std::regex next_regex("<([^>]+)>;\\s*rel=\"next\"");
    std::smatch match;

    if (std::regex_search(link_header, match, next_regex)) {
        return match[1].str();
    }

    return "";
}

std::vector<std::string> GitHubClient::list_repos() {
    std::vector<std::string> repos;
    std::string endpoint = "/orgs/" + org_ + "/repos?per_page=100&type=all";

    while (!endpoint.empty()) {
        std::string response = api_request(endpoint);

        try {
            json repo_list = json::parse(response);

            for (const auto& repo : repo_list) {
                if (repo.contains("name") && !repo["archived"].get<bool>()) {
                    repos.push_back(repo["name"].get<std::string>());
                }
            }
        } catch (json::exception& e) {
            throw std::runtime_error("Failed to parse repository list: " + std::string(e.what()));
        }

        // Check for pagination (would need to capture headers in real implementation)
        // For simplicity, we'll stop after first page in this implementation
        // Full implementation would capture Link header from response
        break;
    }

    return repos;
}

std::vector<std::string> GitHubClient::extract_top_level_paths(const std::string& repo,
                                                               const std::string& commit_sha) {
    std::set<std::string> paths;

    try {
        std::string endpoint = "/repos/" + org_ + "/" + repo + "/commits/" + commit_sha;
        std::string response = api_request(endpoint);

        json commit_detail = json::parse(response);

        if (commit_detail.contains("files")) {
            for (const auto& file : commit_detail["files"]) {
                if (file.contains("filename")) {
                    std::string filename = file["filename"].get<std::string>();
                    // Extract top-level directory
                    size_t slash_pos = filename.find('/');
                    if (slash_pos != std::string::npos) {
                        paths.insert(filename.substr(0, slash_pos));
                    } else {
                        paths.insert("/");  // Root level file
                    }
                }
            }
        }
    } catch (...) {
        // If we can't get commit details, return empty paths
    }

    return std::vector<std::string>(paths.begin(), paths.end());
}

std::vector<Commit> GitHubClient::fetch_commits(const std::string& repo,
                                                 int limit,
                                                 const std::string& since) {
    std::vector<Commit> commits;

    std::string endpoint = "/repos/" + org_ + "/" + repo + "/commits?per_page=" +
                           std::to_string(std::min(limit, config::DEFAULT_COMMITS_PER_PAGE));

    if (!since.empty()) {
        endpoint += "&since=" + since;
    }

    int fetched = 0;

    while (!endpoint.empty() && fetched < limit) {
        std::string response = api_request(endpoint);

        try {
            json commit_list = json::parse(response);

            for (const auto& c : commit_list) {
                if (fetched >= limit) break;

                Commit commit;
                commit.repo_name = repo;
                commit.commit_hash = c["sha"].get<std::string>();

                if (c.contains("commit")) {
                    const auto& commit_data = c["commit"];

                    if (commit_data.contains("message")) {
                        commit.message = commit_data["message"].get<std::string>();
                    }

                    if (commit_data.contains("author") && commit_data["author"].contains("date")) {
                        commit.timestamp = commit_data["author"]["date"].get<std::string>();
                    }

                    if (commit_data.contains("author") && commit_data["author"].contains("name")) {
                        commit.author = commit_data["author"]["name"].get<std::string>();
                    }
                }

                // Override author with login if available
                if (c.contains("author") && !c["author"].is_null() && c["author"].contains("login")) {
                    commit.author = c["author"]["login"].get<std::string>();
                }

                // Note: Fetching top-level paths for each commit is expensive
                // Skip for bulk fetches, could be done on-demand later
                // commit.top_level_paths = extract_top_level_paths(repo, commit.commit_hash);

                commits.push_back(commit);
                fetched++;
            }
        } catch (json::exception& e) {
            throw std::runtime_error("Failed to parse commits: " + std::string(e.what()));
        }

        // Break after first page for simplicity
        // Full implementation would parse Link header for pagination
        break;
    }

    return commits;
}

}  // namespace feed
