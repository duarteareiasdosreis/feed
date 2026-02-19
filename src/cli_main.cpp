#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#include "commands.h"
#include "github_client.h"

namespace {

void print_usage() {
    std::cout << R"(
feed - Engineering Intelligence Tool

Usage: feed <command> [options]

Commands:
  init --org <org> [--token <token>] [filters]  Initialize with GitHub organization
  sync                                           Sync commits from filtered repositories
  status                                         Show sync status (fast, local data only)
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
  GITHUB_FEED_TOKEN          GitHub personal access token (recommended for security)
                        If set, --token argument becomes optional

Filter Options (for init):
  --token <token>       GitHub token (optional if GITHUB_FEED_TOKEN env var is set)
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
  # Initialize using GITHUB_FEED_TOKEN environment variable (recommended)
  export GITHUB_FEED_TOKEN=ghp_xxx
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
    return std::find(args.begin(), args.end(), flag) != args.end();
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

// Progress callback that prints to stdout
void cli_progress(const std::string& message) {
    std::cout << message << std::endl;
}

// Handle result: print and return exit code
int handle_result(const std::string& result) {
    if (feed::commands::is_error(result)) {
        std::cerr << "Error: " << feed::commands::get_error_message(result) << std::endl;
        return 1;
    }
    std::cout << result << std::endl;
    return 0;
}

int cmd_init(const std::vector<std::string>& args) {
    std::string org = get_arg_value(args, "--org");
    std::string token = get_arg_value(args, "--token");
    bool store_token = !token.empty();

    feed::RepoFilter filter = parse_filter_args(args);

    std::string result = feed::commands::init(org, token, filter, store_token);

    if (feed::commands::is_error(result)) {
        std::cerr << "Error: " << feed::commands::get_error_message(result) << std::endl;
        return 1;
    }

    // Print human-readable output for CLI
    std::cout << "Initialized feed for organization: " << org << std::endl;
    std::cout << "\nRun 'feed list-repos' to preview matching repos" << std::endl;
    std::cout << "Run 'feed sync' to fetch commits" << std::endl;

    return 0;
}

int cmd_sync(const std::vector<std::string>& args) {
    std::string result = feed::commands::sync(cli_progress);

    if (feed::commands::is_error(result)) {
        std::cerr << "Error: " << feed::commands::get_error_message(result) << std::endl;
        return 1;
    }

    // The progress callback already printed status, just show summary
    std::cout << "\nSync complete." << std::endl;
    return 0;
}

int cmd_list_repos(const std::vector<std::string>& args) {
    std::string result = feed::commands::list_repos(cli_progress);
    return handle_result(result);
}

int cmd_config(const std::vector<std::string>& args) {
    std::string result = feed::commands::get_config();
    return handle_result(result);
}

int cmd_status(const std::vector<std::string>& args) {
    std::string result = feed::commands::get_sync_status();
    return handle_result(result);
}

int cmd_add_repo(const std::vector<std::string>& args) {
    std::vector<std::string> repos;
    for (size_t i = 2; i < args.size(); i++) {
        if (args[i][0] != '-') {
            repos.push_back(args[i]);
        }
    }

    std::string result = feed::commands::add_repos(repos);

    if (feed::commands::is_error(result)) {
        std::cerr << "Error: " << feed::commands::get_error_message(result) << std::endl;
        return 1;
    }

    for (const auto& repo : repos) {
        std::cout << "Added: " << repo << std::endl;
    }
    return 0;
}

int cmd_remove_repo(const std::vector<std::string>& args) {
    std::vector<std::string> repos;
    for (size_t i = 2; i < args.size(); i++) {
        if (args[i][0] != '-') {
            repos.push_back(args[i]);
        }
    }

    std::string result = feed::commands::remove_repos(repos);

    if (feed::commands::is_error(result)) {
        std::cerr << "Error: " << feed::commands::get_error_message(result) << std::endl;
        return 1;
    }

    std::cout << result << std::endl;
    return 0;
}

int cmd_recent(const std::vector<std::string>& args) {
    std::string repo = get_arg_value(args, "--repo");
    int limit = std::stoi(get_arg_value(args, "--limit", "50"));

    std::string result = feed::commands::get_recent_commits(repo, limit);
    return handle_result(result);
}

int cmd_similar(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "Error: Query string is required" << std::endl;
        return 1;
    }

    std::string query = args[2];
    int top_k = std::stoi(get_arg_value(args, "--top", "5"));

    std::string result = feed::commands::find_similar(query, top_k);
    return handle_result(result);
}

int cmd_tagged(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "Error: Tag name is required" << std::endl;
        return 1;
    }

    std::string tag = args[2];
    int days = std::stoi(get_arg_value(args, "--days", "7"));
    int limit = std::stoi(get_arg_value(args, "--limit", "20"));

    std::string result = feed::commands::get_tagged(tag, days, limit);
    return handle_result(result);
}

int cmd_summary(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "Error: Repository name is required" << std::endl;
        return 1;
    }

    std::string repo = args[2];
    int days = std::stoi(get_arg_value(args, "--days", "7"));

    std::string result = feed::commands::get_summary(repo, days);
    return handle_result(result);
}

int cmd_rebuild(const std::vector<std::string>& args) {
    std::cout << "Rebuilding search index..." << std::endl;
    std::string result = feed::commands::rebuild_index();
    return handle_result(result);
}

int cmd_tags(const std::vector<std::string>& args) {
    std::string result = feed::commands::get_tags();
    return handle_result(result);
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
        } else if (command == "status") {
            return cmd_status(args);
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
