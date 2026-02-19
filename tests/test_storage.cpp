#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "storage.h"
#include "mock_github_client.h"
#include <filesystem>
#include <cstdio>

namespace feed {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::Contains;

class StorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a unique test database for each test
        test_db_path_ = "test_db_" + std::to_string(test_counter_++) + ".db";
        storage_ = std::make_unique<Storage>(test_db_path_);
        storage_->init_schema();
    }

    void TearDown() override {
        storage_.reset();
        std::remove(test_db_path_.c_str());
    }

    std::unique_ptr<Storage> storage_;
    std::string test_db_path_;
    static int test_counter_;
};

int StorageTest::test_counter_ = 0;

// Test schema initialization
TEST_F(StorageTest, InitSchema_CreatesTablesSuccessfully) {
    // Schema should be created in SetUp
    // Verify we can perform basic operations
    EXPECT_EQ(storage_->get_commit_count(), 0);
}

// Test commit insertion
TEST_F(StorageTest, InsertCommit_StoresCommitCorrectly) {
    Commit c = testing::make_test_commit(
        "test-repo", "abc123", "jdoe", "Fix bug in login"
    );
    c.tags = {"bugfix"};

    storage_->insert_commit(c);

    EXPECT_TRUE(storage_->commit_exists("abc123"));
    EXPECT_EQ(storage_->get_commit_count(), 1);
}

// Test commit existence check
TEST_F(StorageTest, CommitExists_ReturnsFalseForNonexistent) {
    EXPECT_FALSE(storage_->commit_exists("nonexistent"));
}

// Test duplicate commit handling
TEST_F(StorageTest, InsertCommit_IgnoresDuplicates) {
    Commit c = testing::make_test_commit(
        "test-repo", "abc123", "jdoe", "Fix bug"
    );

    storage_->insert_commit(c);
    storage_->insert_commit(c);  // Duplicate

    EXPECT_EQ(storage_->get_commit_count(), 1);
}

// Test recent commits retrieval
TEST_F(StorageTest, GetRecentCommits_ReturnsInDescendingOrder) {
    Commit c1 = testing::make_test_commit(
        "repo1", "hash1", "author1", "First commit", "2024-01-01T10:00:00Z"
    );
    Commit c2 = testing::make_test_commit(
        "repo1", "hash2", "author2", "Second commit", "2024-01-02T10:00:00Z"
    );
    Commit c3 = testing::make_test_commit(
        "repo1", "hash3", "author3", "Third commit", "2024-01-03T10:00:00Z"
    );

    storage_->insert_commit(c1);
    storage_->insert_commit(c2);
    storage_->insert_commit(c3);

    auto commits = storage_->get_recent_commits("", 10);

    ASSERT_THAT(commits, SizeIs(3));
    EXPECT_EQ(commits[0].commit_hash, "hash3");  // Most recent first
    EXPECT_EQ(commits[1].commit_hash, "hash2");
    EXPECT_EQ(commits[2].commit_hash, "hash1");
}

// Test repo filter
TEST_F(StorageTest, GetRecentCommits_FiltersbyRepo) {
    Commit c1 = testing::make_test_commit("repo1", "hash1", "a", "Commit 1");
    Commit c2 = testing::make_test_commit("repo2", "hash2", "a", "Commit 2");
    Commit c3 = testing::make_test_commit("repo1", "hash3", "a", "Commit 3");

    storage_->insert_commit(c1);
    storage_->insert_commit(c2);
    storage_->insert_commit(c3);

    auto commits = storage_->get_recent_commits("repo1", 10);

    ASSERT_THAT(commits, SizeIs(2));
    for (const auto& c : commits) {
        EXPECT_EQ(c.repo_name, "repo1");
    }
}

// Test limit parameter
TEST_F(StorageTest, GetRecentCommits_RespectsLimit) {
    for (int i = 0; i < 10; i++) {
        Commit c = testing::make_test_commit(
            "repo", "hash" + std::to_string(i), "author", "Commit " + std::to_string(i)
        );
        storage_->insert_commit(c);
    }

    auto commits = storage_->get_recent_commits("", 5);

    EXPECT_THAT(commits, SizeIs(5));
}

// Test fetch state management
TEST_F(StorageTest, FetchState_StoresAndRetrievesCorrectly) {
    std::string timestamp = "2024-01-15T12:00:00Z";

    storage_->set_last_fetch_time("test-repo", timestamp);

    EXPECT_EQ(storage_->get_last_fetch_time("test-repo"), timestamp);
}

// Test fetch state for unknown repo
TEST_F(StorageTest, FetchState_ReturnsEmptyForUnknownRepo) {
    EXPECT_TRUE(storage_->get_last_fetch_time("unknown-repo").empty());
}

// Test embedding update
TEST_F(StorageTest, UpdateEmbedding_StoresEmbeddingCorrectly) {
    Commit c = testing::make_test_commit("repo", "hash123", "author", "Test commit");
    storage_->insert_commit(c);

    std::vector<float> embedding = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    storage_->update_embedding("hash123", embedding);

    auto commits = storage_->get_all_commits_with_embeddings();
    ASSERT_THAT(commits, SizeIs(1));
    EXPECT_EQ(commits[0].embedding.size(), 5);
    EXPECT_FLOAT_EQ(commits[0].embedding[0], 0.1f);
    EXPECT_FLOAT_EQ(commits[0].embedding[4], 0.5f);
}

// Test tags update
TEST_F(StorageTest, UpdateTags_StoresTagsCorrectly) {
    Commit c = testing::make_test_commit("repo", "hash123", "author", "Test commit");
    storage_->insert_commit(c);

    std::vector<std::string> tags = {"bugfix", "optimization"};
    storage_->update_tags("hash123", tags);

    auto commits = storage_->get_recent_commits("", 1);
    ASSERT_THAT(commits, SizeIs(1));
    EXPECT_THAT(commits[0].tags, Contains("bugfix"));
    EXPECT_THAT(commits[0].tags, Contains("optimization"));
}

// Test commit count
TEST_F(StorageTest, GetCommitCount_ReturnsCorrectCount) {
    EXPECT_EQ(storage_->get_commit_count(), 0);

    storage_->insert_commit(testing::make_test_commit("r1", "h1", "a", "m"));
    EXPECT_EQ(storage_->get_commit_count(), 1);

    storage_->insert_commit(testing::make_test_commit("r2", "h2", "a", "m"));
    EXPECT_EQ(storage_->get_commit_count(), 2);
}

// Test commit count by repo
TEST_F(StorageTest, GetCommitCount_FiltersbyRepo) {
    storage_->insert_commit(testing::make_test_commit("repo1", "h1", "a", "m"));
    storage_->insert_commit(testing::make_test_commit("repo1", "h2", "a", "m"));
    storage_->insert_commit(testing::make_test_commit("repo2", "h3", "a", "m"));

    EXPECT_EQ(storage_->get_commit_count("repo1"), 2);
    EXPECT_EQ(storage_->get_commit_count("repo2"), 1);
    EXPECT_EQ(storage_->get_commit_count(), 3);
}

// Test vocabulary persistence
TEST_F(StorageTest, VocabularyPersistence_SavesAndLoads) {
    std::string vocab = R"({"word1": 0, "word2": 1, "word3": 2})";

    storage_->save_vocabulary(vocab);
    std::string loaded = storage_->load_vocabulary();

    EXPECT_EQ(loaded, vocab);
}

// Test IDF scores persistence
TEST_F(StorageTest, IdfPersistence_SavesAndLoads) {
    std::string idf = R"({"word1": 1.5, "word2": 2.0})";

    storage_->save_idf_scores(idf);
    std::string loaded = storage_->load_idf_scores();

    EXPECT_EQ(loaded, idf);
}

// Test commits by tag
TEST_F(StorageTest, GetCommitsByTag_FiltersCorrectly) {
    // Use current timestamp so date filter works
    std::string now = testing::current_timestamp();

    Commit c1 = testing::make_test_commit("repo", "h1", "a", "Fix bug", now);
    c1.tags = {"bugfix"};

    Commit c2 = testing::make_test_commit("repo", "h2", "a", "Optimize query", now);
    c2.tags = {"optimization"};

    Commit c3 = testing::make_test_commit("repo", "h3", "a", "Fix perf issue", now);
    c3.tags = {"bugfix", "optimization"};

    storage_->insert_commit(c1);
    storage_->insert_commit(c2);
    storage_->insert_commit(c3);

    auto bugfixes = storage_->get_commits_by_tag("bugfix", 365);
    EXPECT_THAT(bugfixes, SizeIs(2));

    auto optimizations = storage_->get_commits_by_tag("optimization", 365);
    EXPECT_THAT(optimizations, SizeIs(2));
}

}  // namespace
}  // namespace feed
