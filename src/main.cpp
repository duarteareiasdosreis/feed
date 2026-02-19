#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
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
  init --org <org> --token <token>   Initialize with GitHub organization
  sync                               Sync commits from all repositories
  recent [--repo <name>] [--limit N] List recent commits
  similar "<query>" [--top N]        Find similar commits
  tagged <tag> [--days N]            Get commits by classification tag
  summary <repo> [--days N]          Get repository activity summary
  rebuild                            Rebuild search index
  tags                               List available classification tags

Options:
  --org <org>      GitHub organization name
  --token <token>  GitHub personal access token
  --repo <name>    Filter by repository name
  --limit N        Limit number of results (default: 50)
  --top N          Number of similar commits (default: 5)
  --days N         Number of days to look back (default: 7)
  --help           Show this help message

Examples:
  feed init --org myorg --token ghp_xxxx
  feed sync
  feed recent --repo api-server --limit 20
  feed similar "optimize database query" --top 10
  feed tagged optimization --days 14
  feed summary my-repo --days 30
)";
}

std::string get_config_path() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/" + feed::config::CONFIG_FILE;
    }
    return feed::config::CONFIG_FILE;
}

bool load_config(std::string& org, std::string& token) {
    std::ifstream file(get_config_path());
    if (!file.is_open()) {
        return false;
    }

    try {
        json config;
        file >> config;
        org = config.value("org", "");
        token = config.value("token", "");
        return !org.empty() && !token.empty();
    } catch (...) {
        return false;
    }
}

bool save_config(const std::string& org, const std::string& token) {
    std::ofstream file(get_config_path());
    if (!file.is_open()) {
        return false;
    }

    json config;
    config["org"] = org;
    config["token"] = token;
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

bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
    for (const auto& arg : args) {
        if (arg == flag) return true;
    }
    return false;
}

int cmd_init(const std::vector<std::string>& args) {
    std::string org = get_arg_value(args, "--org");
    std::string token = get_arg_value(args, "--token");

    if (org.empty() || token.empty()) {
        std::cerr << "Error: --org and --token are required for init" << std::endl;
        return 1;
    }

    if (!save_config(org, token)) {
        std::cerr << "Error: Failed to save configuration" << std::endl;
        return 1;
    }

    // Initialize database
    feed::Storage db(feed::config::DEFAULT_DB_PATH);
    db.init_schema();

    std::cout << "Initialized feed for organization: " << org << std::endl;
    std::cout << "Database created: " << feed::config::DEFAULT_DB_PATH << std::endl;
    std::cout << "Run 'feed sync' to fetch commits" << std::endl;

    return 0;
}

int cmd_sync(const std::vector<std::string>& args) {
    std::string org, token;
    if (!load_config(org, token)) {
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

    std::cout << "Syncing commits from " << org << "..." << std::endl;

    std::string result = feed::api::update_org_commits(client, db, classifier, engine);
    std::cout << result << std::endl;

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
    if (args.size() < 2) {
        std::cerr << "Error: Query string is required" << std::endl;
        return 1;
    }

    std::string query = args[1];
    int top_k = std::stoi(get_arg_value(args, "--top", "5"));

    feed::Storage db(feed::config::DEFAULT_DB_PATH);
    feed::SearchEngine engine;

    std::string result = feed::api::find_similar_commits(db, engine, query, top_k);
    std::cout << result << std::endl;

    return 0;
}

int cmd_tagged(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "Error: Tag name is required" << std::endl;
        return 1;
    }

    std::string tag = args[1];
    int days = std::stoi(get_arg_value(args, "--days", "7"));

    feed::Storage db(feed::config::DEFAULT_DB_PATH);

    std::string result = feed::api::get_tagged_commits(db, tag, days);
    std::cout << result << std::endl;

    return 0;
}

int cmd_summary(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "Error: Repository name is required" << std::endl;
        return 1;
    }

    std::string repo = args[1];
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
