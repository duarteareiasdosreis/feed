#include "commands.h"
#include "config.h"
#include "storage.h"
#include "github_client.h"
#include "classifier.h"
#include "search.h"
#include "api.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <set>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <algorithm>

namespace feed {
namespace commands {

using json = nlohmann::json;

namespace {

std::string get_config_path() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/" + config::CONFIG_FILE;
    }
    return config::CONFIG_FILE;
}

std::string get_token_from_env() {
    const char* token = std::getenv("GITHUB_FEED_TOKEN");
    return token ? token : "";
}

std::string make_error(const std::string& message) {
    json error;
    error["error"] = message;
    return error.dump(2);
}

std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

bool load_config_internal(std::string& org, std::string& token, RepoFilter& filter) {
    std::ifstream file(get_config_path());
    if (!file.is_open()) {
        return false;
    }

    try {
        json config;
        file >> config;
        org = config.value("org", "");
        token = config.value("token", "");

        if (token.empty()) {
            token = get_token_from_env();
        }

        if (config.contains("filter")) {
            const auto& f = config["filter"];

            if (f.contains("languages")) {
                for (const auto& lang : f["languages"]) {
                    filter.languages.insert(lang.get<std::string>());
                }
            }
            if (f.contains("topics")) {
                for (const auto& topic : f["topics"]) {
                    filter.topics.insert(topic.get<std::string>());
                }
            }
            if (f.contains("include_repos")) {
                for (const auto& repo : f["include_repos"]) {
                    filter.include_repos.insert(repo.get<std::string>());
                }
            }
            if (f.contains("exclude_repos")) {
                for (const auto& repo : f["exclude_repos"]) {
                    filter.exclude_repos.insert(repo.get<std::string>());
                }
            }
            filter.active_days = f.value("active_days", 0);
            filter.max_repos = f.value("max_repos", 0);
            filter.min_stars = f.value("min_stars", 0);
            filter.include_archived = f.value("include_archived", false);
            filter.include_forks = f.value("include_forks", true);
        }

        return !org.empty() && !token.empty();
    } catch (...) {
        return false;
    }
}

bool save_config_internal(const std::string& org, const std::string& token,
                          const RepoFilter& filter, bool store_token) {
    std::ofstream file(get_config_path());
    if (!file.is_open()) {
        return false;
    }

    json config;
    config["org"] = org;

    if (store_token && !token.empty()) {
        config["token"] = token;
    } else {
        config["token"] = "";
    }

    json f;
    f["languages"] = std::vector<std::string>(filter.languages.begin(), filter.languages.end());
    f["topics"] = std::vector<std::string>(filter.topics.begin(), filter.topics.end());
    f["include_repos"] = std::vector<std::string>(filter.include_repos.begin(), filter.include_repos.end());
    f["exclude_repos"] = std::vector<std::string>(filter.exclude_repos.begin(), filter.exclude_repos.end());
    f["active_days"] = filter.active_days;
    f["max_repos"] = filter.max_repos;
    f["min_stars"] = filter.min_stars;
    f["include_archived"] = filter.include_archived;
    f["include_forks"] = filter.include_forks;

    config["filter"] = f;

    file << config.dump(2);
    return true;
}

bool is_token_stored_in_config() {
    std::ifstream file(get_config_path());
    if (!file.is_open()) {
        return false;
    }
    try {
        json config;
        file >> config;
        return !config.value("token", "").empty();
    } catch (...) {
        return false;
    }
}

}  // anonymous namespace

// --- Configuration Commands ---

std::string init(const std::string& org,
                 const std::string& token_arg,
                 const RepoFilter& filter,
                 bool store_token) {
    std::string token = token_arg;

    if (token.empty()) {
        token = get_token_from_env();
    }

    if (org.empty()) {
        return make_error("--org is required for init");
    }

    if (token.empty()) {
        return make_error("GitHub token required. Set GITHUB_FEED_TOKEN environment variable or use --token");
    }

    if (!save_config_internal(org, token, filter, store_token)) {
        return make_error("Failed to save configuration");
    }

    Storage db(config::DEFAULT_DB_PATH);
    db.init_schema();

    json result;
    result["success"] = true;
    result["org"] = org;
    result["db_path"] = config::DEFAULT_DB_PATH;
    result["token_source"] = store_token ? "config_file" : "environment_variable";

    if (!filter.languages.empty()) {
        result["languages"] = std::vector<std::string>(filter.languages.begin(), filter.languages.end());
    }
    if (!filter.topics.empty()) {
        result["topics"] = std::vector<std::string>(filter.topics.begin(), filter.topics.end());
    }
    if (!filter.include_repos.empty()) {
        result["include_repos"] = std::vector<std::string>(filter.include_repos.begin(), filter.include_repos.end());
    }
    if (filter.max_repos > 0) {
        result["max_repos"] = filter.max_repos;
    }
    if (filter.active_days > 0) {
        result["active_days"] = filter.active_days;
    }

    return result.dump(2);
}

std::string get_config() {
    std::string org, token;
    RepoFilter filter;

    if (!load_config_internal(org, token, filter)) {
        return make_error("Not initialized. Run 'feed init' first.");
    }

    bool token_from_env = !is_token_stored_in_config();

    json config;
    config["org"] = org;
    config["token"] = token_from_env ? "***from GITHUB_FEED_TOKEN env***" : "***stored in config***";

    json f;
    f["languages"] = std::vector<std::string>(filter.languages.begin(), filter.languages.end());
    f["topics"] = std::vector<std::string>(filter.topics.begin(), filter.topics.end());
    f["include_repos"] = std::vector<std::string>(filter.include_repos.begin(), filter.include_repos.end());
    f["exclude_repos"] = std::vector<std::string>(filter.exclude_repos.begin(), filter.exclude_repos.end());
    f["active_days"] = filter.active_days;
    f["max_repos"] = filter.max_repos;
    f["min_stars"] = filter.min_stars;
    f["include_archived"] = filter.include_archived;
    f["include_forks"] = filter.include_forks;
    config["filter"] = f;

    return config.dump(2);
}

std::string add_repos(const std::vector<std::string>& repos) {
    std::string org, token;
    RepoFilter filter;

    if (!load_config_internal(org, token, filter)) {
        return make_error("Not initialized. Run 'feed init' first.");
    }

    if (repos.empty()) {
        return make_error("At least one repository name is required");
    }

    bool token_stored = is_token_stored_in_config();

    std::vector<std::string> added;
    for (const auto& repo : repos) {
        filter.include_repos.insert(repo);
        added.push_back(repo);
    }

    if (!save_config_internal(org, token, filter, token_stored)) {
        return make_error("Failed to save configuration");
    }

    json result;
    result["added"] = added;
    result["total_repos"] = filter.include_repos.size();
    return result.dump(2);
}

std::string remove_repos(const std::vector<std::string>& repos) {
    std::string org, token;
    RepoFilter filter;

    if (!load_config_internal(org, token, filter)) {
        return make_error("Not initialized. Run 'feed init' first.");
    }

    if (repos.empty()) {
        return make_error("At least one repository name is required");
    }

    bool token_stored = is_token_stored_in_config();

    std::vector<std::string> removed;
    std::vector<std::string> not_found;

    for (const auto& repo : repos) {
        if (filter.include_repos.erase(repo) > 0) {
            removed.push_back(repo);
        } else {
            not_found.push_back(repo);
        }
    }

    if (!save_config_internal(org, token, filter, token_stored)) {
        return make_error("Failed to save configuration");
    }

    json result;
    result["removed"] = removed;
    result["not_found"] = not_found;
    result["total_repos"] = filter.include_repos.size();
    return result.dump(2);
}

// --- Repository Commands ---

std::string list_repos(ProgressCallback progress) {
    std::string org, token;
    RepoFilter filter;

    if (!load_config_internal(org, token, filter)) {
        return make_error("Not initialized. Run 'feed init' first.");
    }

    progress("Fetching repositories from " + org + "...");

    try {
        GitHubClient client(org, token);
        auto repos = client.list_repos_detailed(filter);

        json result;
        result["count"] = repos.size();
        result["repositories"] = json::array();

        for (const auto& repo : repos) {
            json r;
            r["name"] = repo.name;
            r["language"] = repo.language;
            r["stars"] = repo.stargazers_count;
            r["topics"] = repo.topics;
            r["last_push"] = repo.pushed_at;
            r["fork"] = repo.fork;
            r["archived"] = repo.archived;
            result["repositories"].push_back(r);
        }

        return result.dump(2);
    } catch (const std::exception& e) {
        return make_error(e.what());
    }
}

std::string sync(ProgressCallback progress) {
    std::string org, token;
    RepoFilter filter;

    if (!load_config_internal(org, token, filter)) {
        return make_error("Not initialized. Run 'feed init' first.");
    }

    try {
        Storage db(config::DEFAULT_DB_PATH);
        db.init_schema();

        GitHubClient client(org, token);
        Classifier classifier;
        SearchEngine engine;

        engine.deserialize_vocabulary(db.load_vocabulary());
        engine.deserialize_idf(db.load_idf_scores());

        progress("Fetching repository list from " + org + "...");

        auto repos = client.list_repos(filter);
        progress("Found " + std::to_string(repos.size()) + " repositories matching filters");

        // Purge commits from repos no longer tracked
        std::set<std::string> tracked_repos(repos.begin(), repos.end());
        auto stored_repos = db.get_stored_repos();
        int purged_commits = 0;

        for (const auto& stored_repo : stored_repos) {
            if (tracked_repos.find(stored_repo) == tracked_repos.end()) {
                int deleted = db.delete_commits_for_repo(stored_repo);
                if (deleted > 0) {
                    progress("Purged " + std::to_string(deleted) + " commits from removed repo: " + stored_repo);
                    purged_commits += deleted;
                }
            }
        }

        int new_commits = 0;
        int repos_synced = 0;
        std::vector<std::string> errors;

        for (const auto& repo : repos) {
            try {
                std::string since = db.get_last_fetch_time(repo);
                auto commits = client.fetch_commits(repo, config::DEFAULT_COMMITS_PER_PAGE, since);

                int repo_new = 0;
                for (auto& commit : commits) {
                    if (db.commit_exists(commit.commit_hash)) {
                        continue;
                    }

                    commit.tags = classifier.classify(commit.message);
                    db.insert_commit(commit);
                    repo_new++;
                    new_commits++;
                }

                db.set_last_fetch_time(repo, current_timestamp());
                repos_synced++;

                progress("  " + repo + ": " + std::to_string(repo_new) + " new commits");

            } catch (const std::exception& e) {
                errors.push_back(repo + ": " + e.what());
                progress("  " + repo + ": ERROR - " + e.what());
            }
        }

        // Rebuild search index if needed
        int total_commits = db.get_commit_count();
        bool index_rebuilt = false;

        if (engine.needs_rebuild(total_commits) && total_commits > 0) {
            progress("Rebuilding search index...");

            auto all_commits = db.get_recent_commits("", total_commits);
            std::vector<std::string> messages;
            messages.reserve(all_commits.size());
            for (const auto& c : all_commits) {
                messages.push_back(c.message);
            }

            engine.build_vocabulary(messages);

            for (const auto& c : all_commits) {
                auto embedding = engine.compute_embedding(c.message);
                if (!embedding.empty()) {
                    db.update_embedding(c.commit_hash, embedding);
                }
            }

            db.save_vocabulary(engine.serialize_vocabulary());
            db.save_idf_scores(engine.serialize_idf());

            progress("Index rebuilt with " + std::to_string(engine.vocab_size()) + " terms");
            index_rebuilt = true;
        }

        json result;
        result["new_commits"] = new_commits;
        result["repos_synced"] = repos_synced;
        result["total_repos"] = repos.size();
        result["purged_commits"] = purged_commits;
        result["errors"] = errors;
        result["vocabulary_size"] = engine.vocab_size();
        result["index_rebuilt"] = index_rebuilt;

        return result.dump(2);

    } catch (const std::exception& e) {
        return make_error(e.what());
    }
}

// --- Query Commands ---

std::string get_recent_commits(const std::string& repo, int limit) {
    try {
        Storage db(config::DEFAULT_DB_PATH);
        return api::get_recent_commits(db, repo, limit);
    } catch (const std::exception& e) {
        return make_error(e.what());
    }
}

std::string find_similar(const std::string& query, int top_k) {
    if (query.empty()) {
        return make_error("Query string is required");
    }

    try {
        Storage db(config::DEFAULT_DB_PATH);
        SearchEngine engine;
        return api::find_similar_commits(db, engine, query, top_k);
    } catch (const std::exception& e) {
        return make_error(e.what());
    }
}

std::string get_tagged(const std::string& tag, int days, int limit) {
    if (tag.empty()) {
        return make_error("Tag name is required");
    }

    Classifier classifier;
    auto valid_tags = classifier.get_available_tags();
    bool tag_exists = std::find(valid_tags.begin(), valid_tags.end(), tag) != valid_tags.end();

    if (!tag_exists) {
        std::string tags_list;
        for (size_t i = 0; i < valid_tags.size(); i++) {
            tags_list += valid_tags[i];
            if (i < valid_tags.size() - 1) tags_list += ", ";
        }
        return make_error("Unknown tag '" + tag + "'. Available tags: " + tags_list);
    }

    try {
        Storage db(config::DEFAULT_DB_PATH);
        return api::get_tagged_commits(db, tag, days, limit);
    } catch (const std::exception& e) {
        return make_error(e.what());
    }
}

std::string get_summary(const std::string& repo, int days) {
    if (repo.empty()) {
        return make_error("Repository name is required");
    }

    try {
        Storage db(config::DEFAULT_DB_PATH);
        return api::get_repo_activity_summary(db, repo, days);
    } catch (const std::exception& e) {
        return make_error(e.what());
    }
}

// --- Maintenance Commands ---

std::string rebuild_index() {
    try {
        Storage db(config::DEFAULT_DB_PATH);
        SearchEngine engine;
        return api::rebuild_search_index(db, engine);
    } catch (const std::exception& e) {
        return make_error(e.what());
    }
}

std::string get_tags() {
    try {
        Classifier classifier;
        return api::get_available_tags(classifier);
    } catch (const std::exception& e) {
        return make_error(e.what());
    }
}

// --- Helper Functions ---

bool is_error(const std::string& json_result) {
    try {
        json j = json::parse(json_result);
        return j.contains("error");
    } catch (...) {
        return true;  // Parse failure is an error
    }
}

std::string get_error_message(const std::string& json_result) {
    try {
        json j = json::parse(json_result);
        if (j.contains("error")) {
            return j["error"].get<std::string>();
        }
        return "";
    } catch (...) {
        return "Failed to parse result";
    }
}

}  // namespace commands
}  // namespace feed
