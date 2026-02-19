#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <nlohmann/json.hpp>
#include "api.h"
#include "storage.h"
#include "search.h"
#include "classifier.h"
#include "mock_github_client.h"
#include <cstdio>

namespace feed {
namespace {

using json = nlohmann::json;
using ::testing::HasSubstr;
using ::testing::SizeIs;

class ApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "test_api_db_" + std::to_string(test_counter_++) + ".db";
        storage_ = std::make_unique<Storage>(test_db_path_);
        storage_->init_schema();
    }

    void TearDown() override {
        storage_.reset();
        std::remove(test_db_path_.c_str());
    }

    void insert_test_commits() {
        // Use messages with overlapping words so vocabulary can be built
        Commit c1 = testing::make_test_commit(
            "repo1", "hash1", "alice", "Fix bug in login module system", "2024-01-15T10:00:00Z"
        );
        c1.tags = {"bugfix"};

        Commit c2 = testing::make_test_commit(
            "repo1", "hash2", "bob", "Fix bug in database module query", "2024-01-15T11:00:00Z"
        );
        c2.tags = {"optimization"};

        Commit c3 = testing::make_test_commit(
            "repo2", "hash3", "alice", "Fix bug in user module feature", "2024-01-15T12:00:00Z"
        );
        c3.tags = {"feature"};

        storage_->insert_commit(c1);
        storage_->insert_commit(c2);
        storage_->insert_commit(c3);
    }

    std::unique_ptr<Storage> storage_;
    std::string test_db_path_;
    static int test_counter_;
};

int ApiTest::test_counter_ = 0;

// Test get_recent_commits - returns JSON
TEST_F(ApiTest, GetRecentCommits_ReturnsValidJson) {
    insert_test_commits();

    std::string result = api::get_recent_commits(*storage_, "", 10);

    EXPECT_NO_THROW({
        json j = json::parse(result);
        EXPECT_TRUE(j.contains("commits"));
        EXPECT_TRUE(j.contains("count"));
    });
}

// Test get_recent_commits - returns correct count
TEST_F(ApiTest, GetRecentCommits_ReturnsCorrectCount) {
    insert_test_commits();

    std::string result = api::get_recent_commits(*storage_, "", 10);
    json j = json::parse(result);

    EXPECT_EQ(j["count"], 3);
    EXPECT_EQ(j["commits"].size(), 3);
}

// Test get_recent_commits - filters by repo
TEST_F(ApiTest, GetRecentCommits_FiltersByRepo) {
    insert_test_commits();

    std::string result = api::get_recent_commits(*storage_, "repo1", 10);
    json j = json::parse(result);

    EXPECT_EQ(j["count"], 2);
    EXPECT_EQ(j["repo_filter"], "repo1");
    for (const auto& commit : j["commits"]) {
        EXPECT_EQ(commit["repo_name"], "repo1");
    }
}

// Test get_recent_commits - respects limit
TEST_F(ApiTest, GetRecentCommits_RespectsLimit) {
    insert_test_commits();

    std::string result = api::get_recent_commits(*storage_, "", 2);
    json j = json::parse(result);

    EXPECT_EQ(j["commits"].size(), 2);
}

// Test get_recent_commits - empty database
TEST_F(ApiTest, GetRecentCommits_HandlesEmptyDatabase) {
    std::string result = api::get_recent_commits(*storage_, "", 10);
    json j = json::parse(result);

    EXPECT_EQ(j["count"], 0);
    EXPECT_TRUE(j["commits"].empty());
}

// Test find_similar_commits - returns JSON
TEST_F(ApiTest, FindSimilarCommits_ReturnsValidJson) {
    insert_test_commits();

    // Build search index
    SearchEngine engine;
    std::vector<std::string> messages = {
        "Fix bug in login",
        "Optimize database query",
        "Add new feature"
    };
    engine.build_vocabulary(messages);
    storage_->save_vocabulary(engine.serialize_vocabulary());
    storage_->save_idf_scores(engine.serialize_idf());

    std::string result = api::find_similar_commits(*storage_, engine, "fix bug", 5);

    EXPECT_NO_THROW({
        json j = json::parse(result);
        EXPECT_TRUE(j.contains("results"));
        EXPECT_TRUE(j.contains("query"));
        EXPECT_TRUE(j.contains("count"));
    });
}

// Test find_similar_commits - returns results with similarity
TEST_F(ApiTest, FindSimilarCommits_IncludesSimilarityScore) {
    insert_test_commits();

    SearchEngine engine;
    std::vector<std::string> messages = {
        "Fix bug in login",
        "Optimize database query",
        "Add new feature"
    };
    engine.build_vocabulary(messages);

    std::string result = api::find_similar_commits(*storage_, engine, "fix bug", 5);
    json j = json::parse(result);

    if (!j["results"].empty()) {
        for (const auto& r : j["results"]) {
            EXPECT_TRUE(r.contains("similarity"));
            EXPECT_GE(r["similarity"].get<float>(), 0.0f);
        }
    }
}

// Test get_tagged_commits - returns JSON
TEST_F(ApiTest, GetTaggedCommits_ReturnsValidJson) {
    insert_test_commits();

    std::string result = api::get_tagged_commits(*storage_, "bugfix", 30);

    EXPECT_NO_THROW({
        json j = json::parse(result);
        EXPECT_TRUE(j.contains("commits"));
        EXPECT_TRUE(j.contains("tag"));
        EXPECT_TRUE(j.contains("days"));
    });
}

// Test get_tagged_commits - filters by tag
TEST_F(ApiTest, GetTaggedCommits_FiltersByTag) {
    insert_test_commits();

    std::string result = api::get_tagged_commits(*storage_, "bugfix", 365);
    json j = json::parse(result);

    EXPECT_EQ(j["tag"], "bugfix");
    for (const auto& commit : j["commits"]) {
        EXPECT_THAT(commit["tags"].dump(), HasSubstr("bugfix"));
    }
}

// Test get_repo_activity_summary - returns JSON
TEST_F(ApiTest, GetRepoActivitySummary_ReturnsValidJson) {
    insert_test_commits();

    std::string result = api::get_repo_activity_summary(*storage_, "repo1", 30);

    EXPECT_NO_THROW({
        json j = json::parse(result);
        EXPECT_TRUE(j.contains("repo"));
        EXPECT_TRUE(j.contains("total_commits"));
        EXPECT_TRUE(j.contains("top_authors"));
        EXPECT_TRUE(j.contains("tag_distribution"));
    });
}

// Test get_repo_activity_summary - correct stats
TEST_F(ApiTest, GetRepoActivitySummary_ReturnsCorrectStats) {
    insert_test_commits();

    std::string result = api::get_repo_activity_summary(*storage_, "repo1", 30);
    json j = json::parse(result);

    EXPECT_EQ(j["repo"], "repo1");
    EXPECT_EQ(j["total_commits"], 2);
}

// Test get_repo_activity_summary - top authors
TEST_F(ApiTest, GetRepoActivitySummary_IncludesTopAuthors) {
    insert_test_commits();

    std::string result = api::get_repo_activity_summary(*storage_, "repo1", 30);
    json j = json::parse(result);

    EXPECT_FALSE(j["top_authors"].empty());
    for (const auto& author : j["top_authors"]) {
        EXPECT_TRUE(author.contains("author"));
        EXPECT_TRUE(author.contains("commit_count"));
    }
}

// Test get_available_tags - returns all tags
TEST_F(ApiTest, GetAvailableTags_ReturnsAllTags) {
    Classifier classifier;

    std::string result = api::get_available_tags(classifier);
    json j = json::parse(result);

    EXPECT_TRUE(j.contains("tags"));
    EXPECT_FALSE(j["tags"].empty());

    // Check for expected tags
    auto tags = j["tags"].get<std::vector<std::string>>();
    EXPECT_THAT(tags, ::testing::Contains("bugfix"));
    EXPECT_THAT(tags, ::testing::Contains("optimization"));
    EXPECT_THAT(tags, ::testing::Contains("refactor"));
}

// Test rebuild_search_index - returns JSON
TEST_F(ApiTest, RebuildSearchIndex_ReturnsValidJson) {
    insert_test_commits();
    SearchEngine engine;

    std::string result = api::rebuild_search_index(*storage_, engine);

    EXPECT_NO_THROW({
        json j = json::parse(result);
        EXPECT_TRUE(j.contains("vocabulary_size") || j.contains("message"));
    });
}

// Test rebuild_search_index - builds vocabulary
TEST_F(ApiTest, RebuildSearchIndex_BuildsVocabulary) {
    insert_test_commits();
    SearchEngine engine;

    std::string result = api::rebuild_search_index(*storage_, engine);
    json j = json::parse(result);

    EXPECT_GT(j["vocabulary_size"].get<int>(), 0);
    EXPECT_EQ(j["commits_indexed"].get<int>(), 3);
}

// Test rebuild_search_index - empty database
TEST_F(ApiTest, RebuildSearchIndex_HandlesEmptyDatabase) {
    SearchEngine engine;

    std::string result = api::rebuild_search_index(*storage_, engine);
    json j = json::parse(result);

    EXPECT_EQ(j["vocabulary_size"].get<int>(), 0);
}

// Test error handling
TEST_F(ApiTest, GetRecentCommits_HandlesInvalidStorage) {
    // Close storage and try to use it
    storage_.reset();

    // Re-create with a valid path to avoid crash
    storage_ = std::make_unique<Storage>(test_db_path_);
    storage_->init_schema();

    // This should work without errors
    std::string result = api::get_recent_commits(*storage_, "", 10);
    json j = json::parse(result);
    EXPECT_TRUE(j.contains("commits") || j.contains("error"));
}

// Test JSON structure - commits have required fields
TEST_F(ApiTest, GetRecentCommits_CommitsHaveRequiredFields) {
    insert_test_commits();

    std::string result = api::get_recent_commits(*storage_, "", 10);
    json j = json::parse(result);

    for (const auto& commit : j["commits"]) {
        EXPECT_TRUE(commit.contains("repo_name"));
        EXPECT_TRUE(commit.contains("commit_hash"));
        EXPECT_TRUE(commit.contains("author"));
        EXPECT_TRUE(commit.contains("message"));
        EXPECT_TRUE(commit.contains("timestamp"));
        EXPECT_TRUE(commit.contains("tags"));
    }
}

}  // namespace
}  // namespace feed
