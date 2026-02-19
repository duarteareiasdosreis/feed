#include "api.h"
#include "config.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace feed {
namespace api {

using json = nlohmann::json;

namespace {

// Helper to convert commit to JSON
json commit_to_json(const Commit& c) {
    json j;
    j["id"] = c.id;
    j["repo_name"] = c.repo_name;
    j["commit_hash"] = c.commit_hash;
    j["author"] = c.author;
    j["timestamp"] = c.timestamp;
    j["message"] = c.message;
    j["top_level_paths"] = c.top_level_paths;
    j["tags"] = c.tags;
    return j;
}

// Get current ISO 8601 timestamp
std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

}  // anonymous namespace

std::string get_recent_commits(Storage& db, const std::string& repo, int limit) {
    try {
        auto commits = db.get_recent_commits(repo, limit);

        json result;
        result["commits"] = json::array();
        for (const auto& c : commits) {
            result["commits"].push_back(commit_to_json(c));
        }
        result["count"] = commits.size();
        result["repo_filter"] = repo.empty() ? nullptr : json(repo);

        return result.dump(2);
    } catch (const std::exception& e) {
        json error;
        error["error"] = e.what();
        return error.dump(2);
    }
}

std::string find_similar_commits(Storage& db, SearchEngine& engine,
                                  const std::string& query, int top_k) {
    try {
        // Load vocabulary if not loaded
        if (engine.vocab_size() == 0) {
            engine.deserialize_vocabulary(db.load_vocabulary());
            engine.deserialize_idf(db.load_idf_scores());
        }

        // Get all commits (with or without embeddings)
        auto commits = db.get_recent_commits("", 1000);

        // Perform search
        auto results = engine.find_similar(query, commits, top_k);

        json response;
        response["results"] = json::array();
        for (const auto& r : results) {
            json item = commit_to_json(r.commit);
            item["similarity"] = r.similarity;
            response["results"].push_back(item);
        }
        response["query"] = query;
        response["count"] = results.size();

        return response.dump(2);
    } catch (const std::exception& e) {
        json error;
        error["error"] = e.what();
        return error.dump(2);
    }
}

std::string get_tagged_commits(Storage& db, const std::string& tag, int days, int limit) {
    try {
        auto commits = db.get_commits_by_tag(tag, days);

        json result;
        result["commits"] = json::array();
        int count = 0;
        for (const auto& c : commits) {
            if (limit > 0 && count >= limit) break;
            result["commits"].push_back(commit_to_json(c));
            count++;
        }
        result["tag"] = tag;
        result["days"] = days;
        result["limit"] = limit;
        result["count"] = count;
        result["total_matching"] = commits.size();

        return result.dump(2);
    } catch (const std::exception& e) {
        json error;
        error["error"] = e.what();
        return error.dump(2);
    }
}

std::string get_repo_activity_summary(Storage& db, const std::string& repo, int days) {
    try {
        auto commits = db.get_commits_for_repo(repo, 500);

        // Count by author
        std::unordered_map<std::string, int> author_counts;
        std::unordered_map<std::string, int> tag_counts;

        int recent_count = 0;
        auto now = std::chrono::system_clock::now();

        for (const auto& c : commits) {
            author_counts[c.author]++;

            for (const auto& tag : c.tags) {
                tag_counts[tag]++;
            }
        }

        // Get recent count
        recent_count = db.get_commit_count_since(repo, days);

        json result;
        result["repo"] = repo;
        result["total_commits"] = commits.size();
        result["commits_last_n_days"] = recent_count;
        result["days"] = days;

        // Top authors
        std::vector<std::pair<std::string, int>> authors(author_counts.begin(),
                                                          author_counts.end());
        std::sort(authors.begin(), authors.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        result["top_authors"] = json::array();
        int count = 0;
        for (const auto& [author, commits_count] : authors) {
            if (count++ >= 5) break;
            json author_obj;
            author_obj["author"] = author;
            author_obj["commit_count"] = commits_count;
            result["top_authors"].push_back(author_obj);
        }

        // Tag distribution
        result["tag_distribution"] = json::object();
        for (const auto& [tag, tc] : tag_counts) {
            result["tag_distribution"][tag] = tc;
        }

        return result.dump(2);
    } catch (const std::exception& e) {
        json error;
        error["error"] = e.what();
        return error.dump(2);
    }
}

std::string update_org_commits(GitHubClient& client, Storage& db,
                                Classifier& classifier, SearchEngine& engine,
                                const RepoFilter& filter) {
    try {
        int new_commits = 0;
        int repos_synced = 0;
        std::vector<std::string> errors;

        // Get list of repositories with filter
        std::vector<std::string> repos;
        try {
            repos = client.list_repos(filter);
        } catch (const std::exception& e) {
            json error;
            error["error"] = std::string("Failed to list repos: ") + e.what();
            return error.dump(2);
        }

        for (const auto& repo : repos) {
            try {
                // Get last fetch time
                std::string since = db.get_last_fetch_time(repo);

                // Fetch commits
                auto commits = client.fetch_commits(repo, config::DEFAULT_COMMITS_PER_PAGE, since);

                for (auto& commit : commits) {
                    // Skip if already exists
                    if (db.commit_exists(commit.commit_hash)) {
                        continue;
                    }

                    // Classify commit
                    commit.tags = classifier.classify(commit.message);

                    // Insert commit
                    db.insert_commit(commit);
                    new_commits++;
                }

                // Update last fetch time
                db.set_last_fetch_time(repo, current_timestamp());
                repos_synced++;

                std::cout << "Synced " << repo << ": " << commits.size() << " commits fetched" << std::endl;

            } catch (const std::exception& e) {
                errors.push_back(repo + ": " + e.what());
                std::cerr << "Error syncing " << repo << ": " << e.what() << std::endl;
            }
        }

        // Rebuild search index if needed
        int total_commits = db.get_commit_count();
        if (engine.needs_rebuild(total_commits) && total_commits > 0) {
            std::cout << "Rebuilding search index..." << std::endl;

            // Collect all commit messages
            auto all_commits = db.get_recent_commits("", total_commits);
            std::vector<std::string> messages;
            messages.reserve(all_commits.size());
            for (const auto& c : all_commits) {
                messages.push_back(c.message);
            }

            // Build vocabulary
            engine.build_vocabulary(messages);

            // Compute and store embeddings
            for (const auto& c : all_commits) {
                auto embedding = engine.compute_embedding(c.message);
                if (!embedding.empty()) {
                    db.update_embedding(c.commit_hash, embedding);
                }
            }

            // Save vocabulary
            db.save_vocabulary(engine.serialize_vocabulary());
            db.save_idf_scores(engine.serialize_idf());

            std::cout << "Search index rebuilt with " << engine.vocab_size() << " terms" << std::endl;
        }

        json result;
        result["new_commits"] = new_commits;
        result["repos_synced"] = repos_synced;
        result["total_repos"] = repos.size();
        result["errors"] = errors;
        result["vocabulary_size"] = engine.vocab_size();

        return result.dump(2);
    } catch (const std::exception& e) {
        json error;
        error["error"] = e.what();
        return error.dump(2);
    }
}

std::string rebuild_search_index(Storage& db, SearchEngine& engine) {
    try {
        int total_commits = db.get_commit_count();
        if (total_commits == 0) {
            json result;
            result["message"] = "No commits to index";
            result["vocabulary_size"] = 0;
            return result.dump(2);
        }

        // Collect all commit messages
        auto all_commits = db.get_recent_commits("", total_commits);
        std::vector<std::string> messages;
        messages.reserve(all_commits.size());
        for (const auto& c : all_commits) {
            messages.push_back(c.message);
        }

        // Build vocabulary
        engine.build_vocabulary(messages);

        // Compute and store embeddings
        int updated = 0;
        for (const auto& c : all_commits) {
            auto embedding = engine.compute_embedding(c.message);
            if (!embedding.empty()) {
                db.update_embedding(c.commit_hash, embedding);
                updated++;
            }
        }

        // Save vocabulary
        db.save_vocabulary(engine.serialize_vocabulary());
        db.save_idf_scores(engine.serialize_idf());

        json result;
        result["message"] = "Search index rebuilt";
        result["vocabulary_size"] = engine.vocab_size();
        result["commits_indexed"] = updated;

        return result.dump(2);
    } catch (const std::exception& e) {
        json error;
        error["error"] = e.what();
        return error.dump(2);
    }
}

std::string get_available_tags(Classifier& classifier) {
    json result;
    result["tags"] = classifier.get_available_tags();
    return result.dump(2);
}

}  // namespace api
}  // namespace feed
