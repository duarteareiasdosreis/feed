#ifndef FEED_STORAGE_H
#define FEED_STORAGE_H

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>

namespace feed {

struct Commit {
    int id = 0;
    std::string repo_name;
    std::string commit_hash;
    std::string author;
    std::string timestamp;
    std::string message;
    std::vector<std::string> top_level_paths;
    std::vector<std::string> tags;
    std::vector<float> embedding;
    std::string created_at;
};

class Storage {
public:
    explicit Storage(const std::string& db_path = "commits.db");
    ~Storage();

    // Non-copyable
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    // Schema management
    void init_schema();

    // Commit operations
    bool commit_exists(const std::string& hash);
    void insert_commit(const Commit& commit);
    void update_embedding(const std::string& hash, const std::vector<float>& vec);
    void update_tags(const std::string& hash, const std::vector<std::string>& tags);

    // Fetch state management
    std::string get_last_fetch_time(const std::string& repo);
    void set_last_fetch_time(const std::string& repo, const std::string& timestamp);

    // Query operations
    std::vector<Commit> get_recent_commits(const std::string& repo = "", int limit = 50);
    std::vector<Commit> get_commits_by_tag(const std::string& tag, int days = 7);
    std::vector<Commit> get_all_commits_with_embeddings();
    std::vector<Commit> get_commits_for_repo(const std::string& repo, int limit = 100);

    // Statistics
    int get_commit_count(const std::string& repo = "");
    int get_commit_count_since(const std::string& repo, int days);

    // Vocabulary persistence
    void save_vocabulary(const std::string& vocab_json);
    std::string load_vocabulary();
    void save_idf_scores(const std::string& idf_json);
    std::string load_idf_scores();

private:
    sqlite3* db_ = nullptr;
    std::string db_path_;

    void exec_sql(const std::string& sql);
    std::string serialize_string_vector(const std::vector<std::string>& vec);
    std::vector<std::string> deserialize_string_vector(const std::string& json);
    std::string serialize_float_vector(const std::vector<float>& vec);
    std::vector<float> deserialize_float_vector(const void* blob, int size);
};

}  // namespace feed

#endif  // FEED_STORAGE_H
