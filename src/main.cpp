#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <nlohmann/json.hpp>

#include "config.h"
#include "storage.h"
#include "github_client.h"
#include "classifier.h"
#include "search.h"
#include "api.h"

using json = nlohmann::json;

namespace {

void print_usage() {
    std::cout << R"(
feed - Engineering Intelligence Tool

Usage: feed <command> [options]

Commands:
  init --org <org> [--token <token>] [filters]  Initialize with GitHub organization
  sync                                           Sync commits from filtered repositories
  list-repos                                     List repositories matching current filters
  add-repo <repo> [repo2] ...                    Add repo(s) to the include list
  remove-repo <repo> [repo2] ...                 Remove repo(s) from the include list
  recent [--repo <name>] [--limit N]            List recent commits
  similar "<query>" [--top N]                   Find similar commits
  tagged <tag> [--days N]                       Get commits by classification tag
  summary <repo> [--days N]                     Get repository activity summary
  rebuild                                       Rebuild search index
  tags                                          List available classification tags
  config                                        Show current configuration

Environment Variables:
  GITHUB_TOKEN          GitHub personal access token (recommended for security)
                        If set, --token argument becomes optional

Filter Options (for init):
  --token <token>       GitHub token (optional if GITHUB_TOKEN env var is set)
  --language <lang>     Filter by language (can specify multiple: --language go --language python)
  --topic <topic>       Filter by GitHub topic (can specify multiple)
  --include <repo>      Only sync specific repos (can specify multiple)
  --exclude <repo>      Exclude specific repos (can specify multiple)
  --active-days N       Only repos with commits in last N days
  --max-repos N         Maximum number of repos to sync
  --min-stars N         Minimum star count
  --no-forks            Exclude forked repositories
  --include-archived    Include archived repositories

Examples:
  # Initialize using GITHUB_TOKEN environment variable (recommended)
  export GITHUB_TOKEN=ghp_xxx
  feed init --org myorg --language go --language rust --max-repos 50

  # Initialize with explicit token (stored in config file)
  feed init --org myorg --token ghp_xxx --language go --language rust --max-repos 50

  # Initialize with topic filter
  feed init --org myorg --topic backend --active-days 30

  # Initialize with specific repos only
  feed init --org myorg --include api-server --include web-client

  # Add more repos to the include list later
  feed add-repo another-repo yet-another-repo

  # Remove repos from the include list
  feed remove-repo old-repo

  # Preview which repos match your filters
  feed list-repos

  # Sync commits from filtered repos
  feed sync

  # Query commands
  feed recent --repo api-server --limit 20
  feed similar "optimize database query" --top 10
  feed tagged optimization --days 14
)";
}

std::string get_config_path() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/" + feed::config::CONFIG_FILE;
    }
    return feed::config::CONFIG_FILE;
}

std::string get_token_from_env() {
    const char* token = std::getenv("GITHUB_TOKEN");
    return token ? token : "";
}

bool load_config(std::string& org, std::string& token, feed::RepoFilter& filter) {
    std::ifstream file(get_config_path());
    if (!file.is_open()) {
        return false;
    }

    try {
        json config;
        file >> config;
        org = config.value("org", "");
        token = config.value("token", "");

        // Fall back to environment variable if token not in config
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

bool save_config(const std::string& org, const std::string& token,
                 const feed::RepoFilter& filter, bool store_token = true) {
    std::ofstream file(get_config_path());
    if (!file.is_open()) {
        return false;
    }

    json config;
    config["org"] = org;

    // Only store token if explicitly provided (not from env var)
    if (store_token && !token.empty()) {
        config["token"] = token;
    } else {
        config["token"] = "";  // Empty string signals to use env var
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

std::string get_arg_value(const std::vector<std::string>& args, const std::string& flag,
                          const std::string& default_value = "") {
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == flag && i + 1 < args.size()) {
            return args[i + 1];
        }
    }
    return default_value;
}

std::vector<std::string> get_arg_values(const std::vector<std::string>& args,
                                         const std::string& flag) {
    std::vector<std::string> values;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == flag && i + 1 < args.size()) {
            values.push_back(args[i + 1]);
        }
    }
    return values;
}

bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
    for (const auto& arg : args) {
        if (arg == flag) return true;
    }
    return false;
}

feed::RepoFilter parse_filter_args(const std::vector<std::string>& args) {
    feed::RepoFilter filter;

    for (const auto& lang : get_arg_values(args, "--language")) {
        filter.languages.insert(lang);
    }
    for (const auto& topic : get_arg_values(args, "--topic")) {
        filter.topics.insert(topic);
    }
    for (const auto& repo : get_arg_values(args, "--include")) {
        filter.include_repos.insert(repo);
    }
    for (const auto& repo : get_arg_values(args, "--exclude")) {
        filter.exclude_repos.insert(repo);
    }

    std::string active_days = get_arg_value(args, "--active-days");
    if (!active_days.empty()) {
        filter.active_days = std::stoi(active_days);
    }

    std::string max_repos = get_arg_value(args, "--max-repos");
    if (!max_repos.empty()) {
        filter.max_repos = std::stoi(max_repos);
    }

    std::string min_stars = get_arg_value(args, "--min-stars");
    if (!min_stars.empty()) {
        filter.min_stars = std::stoi(min_stars);
    }

    filter.include_forks = !has_flag(args, "--no-forks");
    filter.include_archived = has_flag(args, "--include-archived");

    return filter;
}

int cmd_init(const std::vector<std::string>& args) {
    std::string org = get_arg_value(args, "--org");
    std::string token = get_arg_value(args, "--token");
    bool token_from_arg = !token.empty();

    // Fall back to environment variable if --token not provided
    if (token.empty()) {
        token = get_token_from_env();
    }

    if (org.empty()) {
        std::cerr << "Error: --org is required for init" << std::endl;
        return 1;
    }

    if (token.empty()) {
        std::cerr << "Error: GitHub token required. Either:" << std::endl;
        std::cerr << "  - Set GITHUB_TOKEN environment variable, or" << std::endl;
        std::cerr << "  - Use --token <token> argument" << std::endl;
        return 1;
    }

    feed::RepoFilter filter = parse_filter_args(args);

    // Only store token in config if provided via --token (not env var)
    if (!save_config(org, token, filter, token_from_arg)) {
        std::cerr << "Error: Failed to save configuration" << std::endl;
        return 1;
    }

    // Initialize database
    feed::Storage db(feed::config::DEFAULT_DB_PATH);
    db.init_schema();

    std::cout << "Initialized feed for organization: " << org << std::endl;
    std::cout << "Database created: " << feed::config::DEFAULT_DB_PATH << std::endl;

    if (!token_from_arg) {
        std::cout << "Token: Using GITHUB_TOKEN environment variable" << std::endl;
    }

    // Print filter summary
    if (!filter.languages.empty()) {
        std::cout << "Languages: ";
        for (const auto& l : filter.languages) std::cout << l << " ";
        std::cout << std::endl;
    }
    if (!filter.topics.empty()) {
        std::cout << "Topics: ";
        for (const auto& t : filter.topics) std::cout << t << " ";
        std::cout << std::endl;
    }
    if (!filter.include_repos.empty()) {
        std::cout << "Include repos: ";
        for (const auto& r : filter.include_repos) std::cout << r << " ";
        std::cout << std::endl;
    }
    if (filter.max_repos > 0) {
        std::cout << "Max repos: " << filter.max_repos << std::endl;
    }
    if (filter.active_days > 0) {
        std::cout << "Active days: " << filter.active_days << std::endl;
    }

    std::cout << "\nRun 'feed list-repos' to preview matching repos" << std::endl;
    std::cout << "Run 'feed sync' to fetch commits" << std::endl;

    return 0;
}

int cmd_list_repos(const std::vector<std::string>& args) {
    std::string org, token;
    feed::RepoFilter filter;

    if (!load_config(org, token, filter)) {
        std::cerr << "Error: Not initialized. Run 'feed init' first." << std::endl;
        return 1;
    }

    feed::GitHubClient client(org, token);

    std::cout << "Fetching repositories from " << org << "..." << std::endl;

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

    std::cout << result.dump(2) << std::endl;

    return 0;
}

int cmd_config(const std::vector<std::string>& args) {
    std::string org, token;
    feed::RepoFilter filter;

    if (!load_config(org, token, filter)) {
        std::cerr << "Error: Not initialized. Run 'feed init' first." << std::endl;
        return 1;
    }

    // Check if token came from env var by reading raw config
    std::ifstream file(get_config_path());
    bool token_from_env = false;
    if (file.is_open()) {
        json raw_config;
        file >> raw_config;
        std::string stored_token = raw_config.value("token", "");
        token_from_env = stored_token.empty();
    }

    json config;
    config["org"] = org;
    config["token"] = token_from_env ? "***from GITHUB_TOKEN env***" : "***stored in config***";

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

    std::cout << config.dump(2) << std::endl;

    return 0;
}

int cmd_add_repo(const std::vector<std::string>& args) {
    std::string org, token;
    feed::RepoFilter filter;

    if (!load_config(org, token, filter)) {
        std::cerr << "Error: Not initialized. Run 'feed init' first." << std::endl;
        return 1;
    }

    // Check if token is stored in config or from env var
    std::ifstream file(get_config_path());
    bool token_stored = false;
    if (file.is_open()) {
        json raw_config;
        file >> raw_config;
        token_stored = !raw_config.value("token", "").empty();
    }

    // Collect all repo names from args (skip program name and command)
    std::vector<std::string> repos_to_add;
    for (size_t i = 2; i < args.size(); i++) {
        if (args[i][0] != '-') {  // Skip flags
            repos_to_add.push_back(args[i]);
        }
    }

    if (repos_to_add.empty()) {
        std::cerr << "Error: At least one repository name is required" << std::endl;
        std::cerr << "Usage: feed add-repo <repo1> [repo2] [repo3] ..." << std::endl;
        return 1;
    }

    // Add repos to include list
    for (const auto& repo : repos_to_add) {
        filter.include_repos.insert(repo);
        std::cout << "Added: " << repo << std::endl;
    }

    if (!save_config(org, token, filter, token_stored)) {
        std::cerr << "Error: Failed to save configuration" << std::endl;
        return 1;
    }

    std::cout << "\nTotal repos in include list: " << filter.include_repos.size() << std::endl;
    return 0;
}

int cmd_remove_repo(const std::vector<std::string>& args) {
    std::string org, token;
    feed::RepoFilter filter;

    if (!load_config(org, token, filter)) {
        std::cerr << "Error: Not initialized. Run 'feed init' first." << std::endl;
        return 1;
    }

    // Check if token is stored in config or from env var
    std::ifstream file(get_config_path());
    bool token_stored = false;
    if (file.is_open()) {
        json raw_config;
        file >> raw_config;
        token_stored = !raw_config.value("token", "").empty();
    }

    // Collect all repo names from args (skip program name and command)
    std::vector<std::string> repos_to_remove;
    for (size_t i = 2; i < args.size(); i++) {
        if (args[i][0] != '-') {  // Skip flags
            repos_to_remove.push_back(args[i]);
        }
    }

    if (repos_to_remove.empty()) {
        std::cerr << "Error: At least one repository name is required" << std::endl;
        std::cerr << "Usage: feed remove-repo <repo1> [repo2] [repo3] ..." << std::endl;
        return 1;
    }

    // Remove repos from include list
    for (const auto& repo : repos_to_remove) {
        if (filter.include_repos.erase(repo) > 0) {
            std::cout << "Removed: " << repo << std::endl;
        } else {
            std::cout << "Not found: " << repo << std::endl;
        }
    }

    if (!save_config(org, token, filter, token_stored)) {
        std::cerr << "Error: Failed to save configuration" << std::endl;
        return 1;
    }

    std::cout << "\nTotal repos in include list: " << filter.include_repos.size() << std::endl;
    return 0;
}

int cmd_sync(const std::vector<std::string>& args) {
    std::string org, token;
    feed::RepoFilter filter;

    if (!load_config(org, token, filter)) {
        std::cerr << "Error: Not initialized. Run 'feed init' first." << std::endl;
        return 1;
    }

    feed::Storage db(feed::config::DEFAULT_DB_PATH);
    db.init_schema();

    feed::GitHubClient client(org, token);
    feed::Classifier classifier;
    feed::SearchEngine engine;

    // Load existing vocabulary
    engine.deserialize_vocabulary(db.load_vocabulary());
    engine.deserialize_idf(db.load_idf_scores());

    std::cout << "Fetching repository list from " << org << "..." << std::endl;

    auto repos = client.list_repos(filter);
    std::cout << "Found " << repos.size() << " repositories matching filters" << std::endl;

    if (repos.empty()) {
        std::cout << "No repositories to sync." << std::endl;
        return 0;
    }

    int new_commits = 0;
    int repos_synced = 0;
    std::vector<std::string> errors;

    for (const auto& repo : repos) {
        try {
            std::string since = db.get_last_fetch_time(repo);

            auto commits = client.fetch_commits(repo, feed::config::DEFAULT_COMMITS_PER_PAGE, since);

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

            // Update timestamp
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
            db.set_last_fetch_time(repo, ss.str());

            repos_synced++;
            std::cout << "  " << repo << ": " << repo_new << " new commits" << std::endl;

        } catch (const std::exception& e) {
            errors.push_back(repo + ": " + e.what());
            std::cerr << "  " << repo << ": ERROR - " << e.what() << std::endl;
        }
    }

    // Rebuild search index if needed
    int total_commits = db.get_commit_count();
    if (engine.needs_rebuild(total_commits) && total_commits > 0) {
        std::cout << "\nRebuilding search index..." << std::endl;

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

        std::cout << "Index rebuilt with " << engine.vocab_size() << " terms" << std::endl;
    }

    std::cout << "\nSync complete: " << new_commits << " new commits from "
              << repos_synced << " repos" << std::endl;

    if (!errors.empty()) {
        std::cout << "Errors: " << errors.size() << std::endl;
    }

    return 0;
}

int cmd_recent(const std::vector<std::string>& args) {
    feed::Storage db(feed::config::DEFAULT_DB_PATH);

    std::string repo = get_arg_value(args, "--repo");
    int limit = std::stoi(get_arg_value(args, "--limit", "50"));

    std::string result = feed::api::get_recent_commits(db, repo, limit);
    std::cout << result << std::endl;

    return 0;
}

int cmd_similar(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "Error: Query string is required" << std::endl;
        return 1;
    }

    std::string query = args[2];  // args[0]=program, args[1]=command, args[2]=query
    int top_k = std::stoi(get_arg_value(args, "--top", "5"));

    feed::Storage db(feed::config::DEFAULT_DB_PATH);
    feed::SearchEngine engine;

    std::string result = feed::api::find_similar_commits(db, engine, query, top_k);
    std::cout << result << std::endl;

    return 0;
}

int cmd_tagged(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "Error: Tag name is required" << std::endl;
        return 1;
    }

    std::string tag = args[2];  // args[0]=program, args[1]=command, args[2]=tag
    int days = std::stoi(get_arg_value(args, "--days", "7"));

    // Validate tag exists
    feed::Classifier classifier;
    auto valid_tags = classifier.get_available_tags();
    bool tag_exists = std::find(valid_tags.begin(), valid_tags.end(), tag) != valid_tags.end();

    if (!tag_exists) {
        std::cerr << "Error: Unknown tag '" << tag << "'" << std::endl;
        std::cerr << "Available tags: ";
        for (size_t i = 0; i < valid_tags.size(); i++) {
            std::cerr << valid_tags[i];
            if (i < valid_tags.size() - 1) std::cerr << ", ";
        }
        std::cerr << std::endl;
        return 1;
    }

    feed::Storage db(feed::config::DEFAULT_DB_PATH);

    std::string result = feed::api::get_tagged_commits(db, tag, days);
    std::cout << result << std::endl;

    return 0;
}

int cmd_summary(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "Error: Repository name is required" << std::endl;
        return 1;
    }

    std::string repo = args[2];  // args[0]=program, args[1]=command, args[2]=repo
    int days = std::stoi(get_arg_value(args, "--days", "7"));

    feed::Storage db(feed::config::DEFAULT_DB_PATH);

    std::string result = feed::api::get_repo_activity_summary(db, repo, days);
    std::cout << result << std::endl;

    return 0;
}

int cmd_rebuild(const std::vector<std::string>& args) {
    feed::Storage db(feed::config::DEFAULT_DB_PATH);
    feed::SearchEngine engine;

    std::cout << "Rebuilding search index..." << std::endl;

    std::string result = feed::api::rebuild_search_index(db, engine);
    std::cout << result << std::endl;

    return 0;
}

int cmd_tags(const std::vector<std::string>& args) {
    feed::Classifier classifier;

    std::string result = feed::api::get_available_tags(classifier);
    std::cout << result << std::endl;

    return 0;
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv, argv + argc);

    if (args.size() < 2 || has_flag(args, "--help") || has_flag(args, "-h")) {
        print_usage();
        return 0;
    }

    std::string command = args[1];

    try {
        if (command == "init") {
            return cmd_init(args);
        } else if (command == "sync") {
            return cmd_sync(args);
        } else if (command == "list-repos") {
            return cmd_list_repos(args);
        } else if (command == "add-repo") {
            return cmd_add_repo(args);
        } else if (command == "remove-repo") {
            return cmd_remove_repo(args);
        } else if (command == "config") {
            return cmd_config(args);
        } else if (command == "recent") {
            return cmd_recent(args);
        } else if (command == "similar") {
            return cmd_similar(args);
        } else if (command == "tagged") {
            return cmd_tagged(args);
        } else if (command == "summary") {
            return cmd_summary(args);
        } else if (command == "rebuild") {
            return cmd_rebuild(args);
        } else if (command == "tags") {
            return cmd_tags(args);
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            print_usage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
