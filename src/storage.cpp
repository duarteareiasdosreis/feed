#include "storage.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>
#include <cstring>

namespace feed {

using json = nlohmann::json;

Storage::Storage(const std::string& db_path) : db_path_(db_path) {
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error("Cannot open database: " + error);
    }
}

Storage::~Storage() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void Storage::exec_sql(const std::string& sql) {
    char* error_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg);
    if (rc != SQLITE_OK) {
        std::string error = error_msg ? error_msg : "Unknown error";
        sqlite3_free(error_msg);
        throw std::runtime_error("SQL error: " + error);
    }
}

void Storage::init_schema() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS commits (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            repo_name TEXT NOT NULL,
            commit_hash TEXT UNIQUE NOT NULL,
            author TEXT NOT NULL,
            timestamp TEXT NOT NULL,
            message TEXT NOT NULL,
            top_level_paths TEXT,
            tags TEXT,
            embedding BLOB,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE INDEX IF NOT EXISTS idx_repo_timestamp ON commits(repo_name, timestamp DESC);
        CREATE INDEX IF NOT EXISTS idx_commit_hash ON commits(commit_hash);
        CREATE INDEX IF NOT EXISTS idx_tags ON commits(tags);

        CREATE TABLE IF NOT EXISTS fetch_state (
            repo_name TEXT PRIMARY KEY,
            last_fetch TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS search_state (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )";
    exec_sql(schema);
}

std::string Storage::serialize_string_vector(const std::vector<std::string>& vec) {
    json j = vec;
    return j.dump();
}

std::vector<std::string> Storage::deserialize_string_vector(const std::string& json_str) {
    if (json_str.empty()) {
        return {};
    }
    try {
        json j = json::parse(json_str);
        return j.get<std::vector<std::string>>();
    } catch (...) {
        return {};
    }
}

std::string Storage::serialize_float_vector(const std::vector<float>& vec) {
    // Store as binary blob
    return std::string(reinterpret_cast<const char*>(vec.data()),
                       vec.size() * sizeof(float));
}

std::vector<float> Storage::deserialize_float_vector(const void* blob, int size) {
    if (!blob || size == 0) {
        return {};
    }
    int count = size / sizeof(float);
    std::vector<float> vec(count);
    std::memcpy(vec.data(), blob, size);
    return vec;
}

bool Storage::commit_exists(const std::string& hash) {
    const char* sql = "SELECT 1 FROM commits WHERE commit_hash = ? LIMIT 1";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);

    bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

void Storage::insert_commit(const Commit& commit) {
    const char* sql = R"(
        INSERT OR IGNORE INTO commits
        (repo_name, commit_hash, author, timestamp, message, top_level_paths, tags, embedding)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare insert statement");
    }

    sqlite3_bind_text(stmt, 1, commit.repo_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, commit.commit_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, commit.author.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, commit.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, commit.message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, serialize_string_vector(commit.top_level_paths).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, serialize_string_vector(commit.tags).c_str(), -1, SQLITE_TRANSIENT);

    if (!commit.embedding.empty()) {
        std::string blob = serialize_float_vector(commit.embedding);
        sqlite3_bind_blob(stmt, 8, blob.data(), blob.size(), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 8);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to insert commit");
    }

    sqlite3_finalize(stmt);
}

void Storage::update_embedding(const std::string& hash, const std::vector<float>& vec) {
    const char* sql = "UPDATE commits SET embedding = ? WHERE commit_hash = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare update statement");
    }

    std::string blob = serialize_float_vector(vec);
    sqlite3_bind_blob(stmt, 1, blob.data(), blob.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Storage::update_tags(const std::string& hash, const std::vector<std::string>& tags) {
    const char* sql = "UPDATE commits SET tags = ? WHERE commit_hash = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare update statement");
    }

    sqlite3_bind_text(stmt, 1, serialize_string_vector(tags).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string Storage::get_last_fetch_time(const std::string& repo) {
    const char* sql = "SELECT last_fetch FROM fetch_state WHERE repo_name = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    sqlite3_bind_text(stmt, 1, repo.c_str(), -1, SQLITE_TRANSIENT);

    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) result = text;
    }

    sqlite3_finalize(stmt);
    return result;
}

void Storage::set_last_fetch_time(const std::string& repo, const std::string& timestamp) {
    const char* sql = R"(
        INSERT OR REPLACE INTO fetch_state (repo_name, last_fetch)
        VALUES (?, ?)
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    sqlite3_bind_text(stmt, 1, repo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, timestamp.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<Commit> Storage::get_recent_commits(const std::string& repo, int limit) {
    std::string sql = "SELECT id, repo_name, commit_hash, author, timestamp, message, "
                      "top_level_paths, tags, embedding, created_at FROM commits ";

    if (!repo.empty()) {
        sql += "WHERE repo_name = ? ";
    }
    sql += "ORDER BY timestamp DESC LIMIT ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    int param_idx = 1;
    if (!repo.empty()) {
        sqlite3_bind_text(stmt, param_idx++, repo.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, param_idx, limit);

    std::vector<Commit> commits;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Commit c;
        c.id = sqlite3_column_int(stmt, 0);
        c.repo_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        c.commit_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        c.author = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        c.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        c.message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

        const char* paths = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (paths) c.top_level_paths = deserialize_string_vector(paths);

        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (tags) c.tags = deserialize_string_vector(tags);

        const void* blob = sqlite3_column_blob(stmt, 8);
        int blob_size = sqlite3_column_bytes(stmt, 8);
        if (blob) c.embedding = deserialize_float_vector(blob, blob_size);

        const char* created = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        if (created) c.created_at = created;

        commits.push_back(c);
    }

    sqlite3_finalize(stmt);
    return commits;
}

std::vector<Commit> Storage::get_commits_by_tag(const std::string& tag, int days) {
    std::string sql = R"(
        SELECT id, repo_name, commit_hash, author, timestamp, message,
               top_level_paths, tags, embedding, created_at
        FROM commits
        WHERE tags LIKE ?
        AND timestamp >= datetime('now', ?)
        ORDER BY timestamp DESC
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    std::string tag_pattern = "%" + tag + "%";
    std::string days_modifier = "-" + std::to_string(days) + " days";

    sqlite3_bind_text(stmt, 1, tag_pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, days_modifier.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<Commit> commits;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Commit c;
        c.id = sqlite3_column_int(stmt, 0);
        c.repo_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        c.commit_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        c.author = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        c.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        c.message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

        const char* paths = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (paths) c.top_level_paths = deserialize_string_vector(paths);

        const char* tags_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (tags_str) c.tags = deserialize_string_vector(tags_str);

        const void* blob = sqlite3_column_blob(stmt, 8);
        int blob_size = sqlite3_column_bytes(stmt, 8);
        if (blob) c.embedding = deserialize_float_vector(blob, blob_size);

        const char* created = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        if (created) c.created_at = created;

        commits.push_back(c);
    }

    sqlite3_finalize(stmt);
    return commits;
}

std::vector<Commit> Storage::get_all_commits_with_embeddings() {
    const char* sql = R"(
        SELECT id, repo_name, commit_hash, author, timestamp, message,
               top_level_paths, tags, embedding, created_at
        FROM commits
        WHERE embedding IS NOT NULL
        ORDER BY timestamp DESC
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    std::vector<Commit> commits;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Commit c;
        c.id = sqlite3_column_int(stmt, 0);
        c.repo_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        c.commit_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        c.author = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        c.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        c.message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

        const char* paths = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (paths) c.top_level_paths = deserialize_string_vector(paths);

        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (tags) c.tags = deserialize_string_vector(tags);

        const void* blob = sqlite3_column_blob(stmt, 8);
        int blob_size = sqlite3_column_bytes(stmt, 8);
        if (blob) c.embedding = deserialize_float_vector(blob, blob_size);

        const char* created = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        if (created) c.created_at = created;

        commits.push_back(c);
    }

    sqlite3_finalize(stmt);
    return commits;
}

std::vector<Commit> Storage::get_commits_for_repo(const std::string& repo, int limit) {
    return get_recent_commits(repo, limit);
}

int Storage::get_commit_count(const std::string& repo) {
    std::string sql = "SELECT COUNT(*) FROM commits";
    if (!repo.empty()) {
        sql += " WHERE repo_name = ?";
    }

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    if (!repo.empty()) {
        sqlite3_bind_text(stmt, 1, repo.c_str(), -1, SQLITE_TRANSIENT);
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

int Storage::get_commit_count_since(const std::string& repo, int days) {
    std::string sql = R"(
        SELECT COUNT(*) FROM commits
        WHERE timestamp >= datetime('now', ?)
    )";

    if (!repo.empty()) {
        sql += " AND repo_name = ?";
    }

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    std::string days_modifier = "-" + std::to_string(days) + " days";
    sqlite3_bind_text(stmt, 1, days_modifier.c_str(), -1, SQLITE_TRANSIENT);

    if (!repo.empty()) {
        sqlite3_bind_text(stmt, 2, repo.c_str(), -1, SQLITE_TRANSIENT);
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

void Storage::save_vocabulary(const std::string& vocab_json) {
    const char* sql = "INSERT OR REPLACE INTO search_state (key, value) VALUES ('vocabulary', ?)";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    sqlite3_bind_text(stmt, 1, vocab_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string Storage::load_vocabulary() {
    const char* sql = "SELECT value FROM search_state WHERE key = 'vocabulary'";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) result = text;
    }

    sqlite3_finalize(stmt);
    return result;
}

void Storage::save_idf_scores(const std::string& idf_json) {
    const char* sql = "INSERT OR REPLACE INTO search_state (key, value) VALUES ('idf_scores', ?)";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    sqlite3_bind_text(stmt, 1, idf_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string Storage::load_idf_scores() {
    const char* sql = "SELECT value FROM search_state WHERE key = 'idf_scores'";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement");
    }

    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) result = text;
    }

    sqlite3_finalize(stmt);
    return result;
}

}  // namespace feed
