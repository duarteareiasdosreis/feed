#include "github_client.h"
#include "config.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <stdexcept>
#include <algorithm>
#include <regex>
#include <thread>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

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

// Convert string to lowercase
std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Parse ISO 8601 timestamp to time_t
std::time_t parse_iso8601(const std::string& timestamp) {
    if (timestamp.empty()) return 0;

    std::tm tm = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) return 0;

    return std::mktime(&tm);
}

// Get current time minus N days as time_t
std::time_t days_ago(int days) {
    auto now = std::chrono::system_clock::now();
    auto then = now - std::chrono::hours(24 * days);
    return std::chrono::system_clock::to_time_t(then);
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
    std::regex next_regex("<([^>]+)>;\\s*rel=\"next\"");
    std::smatch match;

    if (std::regex_search(link_header, match, next_regex)) {
        return match[1].str();
    }

    return "";
}

RepoInfo GitHubClient::parse_repo_info(const std::string& json_str) {
    RepoInfo info;
    json repo = json::parse(json_str);

    info.name = repo.value("name", "");
    // language can be null for repos with no detected language
    if (repo.contains("language") && !repo["language"].is_null()) {
        info.language = repo["language"].get<std::string>();
    }
    info.pushed_at = repo.value("pushed_at", "");
    info.archived = repo.value("archived", false);
    info.fork = repo.value("fork", false);
    info.stargazers_count = repo.value("stargazers_count", 0);

    if (repo.contains("topics") && repo["topics"].is_array()) {
        for (const auto& topic : repo["topics"]) {
            info.topics.push_back(topic.get<std::string>());
        }
    }

    return info;
}

bool GitHubClient::matches_filter(const RepoInfo& repo, const RepoFilter& filter) {
    // Check explicit include list first
    if (!filter.include_repos.empty()) {
        if (filter.include_repos.find(repo.name) == filter.include_repos.end()) {
            return false;
        }
    }

    // Check explicit exclude list
    if (filter.exclude_repos.find(repo.name) != filter.exclude_repos.end()) {
        return false;
    }

    // Check archived
    if (!filter.include_archived && repo.archived) {
        return false;
    }

    // Check forks
    if (!filter.include_forks && repo.fork) {
        return false;
    }

    // Check minimum stars
    if (filter.min_stars > 0 && repo.stargazers_count < filter.min_stars) {
        return false;
    }

    // Check language (case-insensitive)
    if (!filter.languages.empty()) {
        std::string repo_lang = to_lower(repo.language);
        bool found = false;
        for (const auto& lang : filter.languages) {
            if (to_lower(lang) == repo_lang) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    // Check topics (repo must have at least one matching topic)
    if (!filter.topics.empty()) {
        bool found = false;
        for (const auto& repo_topic : repo.topics) {
            std::string lower_topic = to_lower(repo_topic);
            for (const auto& filter_topic : filter.topics) {
                if (to_lower(filter_topic) == lower_topic) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) {
            return false;
        }
    }

    // Check activity (last push within N days)
    if (filter.active_days > 0 && !repo.pushed_at.empty()) {
        std::time_t pushed_time = parse_iso8601(repo.pushed_at);
        std::time_t cutoff = days_ago(filter.active_days);
        if (pushed_time < cutoff) {
            return false;
        }
    }

    return true;
}

std::vector<RepoInfo> GitHubClient::list_repos_detailed(const RepoFilter& filter) {
    std::vector<RepoInfo> repos;
    std::string endpoint = "/orgs/" + org_ + "/repos?per_page=100&type=all";

    int page = 0;
    const int max_pages = 100;  // Safety limit: 100 pages * 100 repos = 10k max

    while (!endpoint.empty() && page < max_pages) {
        page++;
        std::string response = api_request(endpoint);

        try {
            json repo_list = json::parse(response);

            if (repo_list.empty()) {
                break;
            }

            for (const auto& repo_json : repo_list) {
                RepoInfo info;
                info.name = repo_json.value("name", "");
                // language can be null for repos with no detected language
                if (repo_json.contains("language") && !repo_json["language"].is_null()) {
                    info.language = repo_json["language"].get<std::string>();
                }
                info.pushed_at = repo_json.value("pushed_at", "");
                info.archived = repo_json.value("archived", false);
                info.fork = repo_json.value("fork", false);
                info.stargazers_count = repo_json.value("stargazers_count", 0);

                if (repo_json.contains("topics") && repo_json["topics"].is_array()) {
                    for (const auto& topic : repo_json["topics"]) {
                        info.topics.push_back(topic.get<std::string>());
                    }
                }

                if (matches_filter(info, filter)) {
                    repos.push_back(info);

                    // Check max repos limit
                    if (filter.max_repos > 0 &&
                        static_cast<int>(repos.size()) >= filter.max_repos) {
                        return repos;
                    }
                }
            }

            // Check if there are more pages
            if (repo_list.size() < 100) {
                break;
            }

            // Build next page URL
            endpoint = "/orgs/" + org_ + "/repos?per_page=100&type=all&page=" +
                       std::to_string(page + 1);

        } catch (json::exception& e) {
            throw std::runtime_error("Failed to parse repository list: " + std::string(e.what()));
        }
    }

    return repos;
}

std::vector<std::string> GitHubClient::list_repos(const RepoFilter& filter) {
    auto detailed = list_repos_detailed(filter);

    std::vector<std::string> names;
    names.reserve(detailed.size());

    for (const auto& repo : detailed) {
        names.push_back(repo.name);
    }

    return names;
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
                    size_t slash_pos = filename.find('/');
                    if (slash_pos != std::string::npos) {
                        paths.insert(filename.substr(0, slash_pos));
                    } else {
                        paths.insert("/");
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

                if (c.contains("author") && !c["author"].is_null() && c["author"].contains("login")) {
                    commit.author = c["author"]["login"].get<std::string>();
                }

                commits.push_back(commit);
                fetched++;
            }
        } catch (json::exception& e) {
            throw std::runtime_error("Failed to parse commits: " + std::string(e.what()));
        }

        break;  // Single page for now
    }

    return commits;
}

}  // namespace feed
