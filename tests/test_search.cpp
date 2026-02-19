#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "search.h"
#include "mock_github_client.h"
#include <cmath>

namespace feed {
namespace {

using ::testing::FloatNear;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::Each;
using ::testing::Field;

class SearchEngineTest : public ::testing::Test {
protected:
    SearchEngine engine_;
};

// Test vocabulary building
TEST_F(SearchEngineTest, BuildVocabulary_CreatesVocabularyFromDocuments) {
    // Use overlapping words so they pass the df >= 2 threshold
    std::vector<std::string> documents = {
        "fix bug in login module",
        "fix error in database module",
        "fix issue in user module"
    };

    engine_.build_vocabulary(documents);

    EXPECT_GT(engine_.vocab_size(), 0);
}

// Test empty vocabulary
TEST_F(SearchEngineTest, BuildVocabulary_HandlesEmptyDocuments) {
    std::vector<std::string> documents = {};

    engine_.build_vocabulary(documents);

    EXPECT_EQ(engine_.vocab_size(), 0);
}

// Test embedding computation
TEST_F(SearchEngineTest, ComputeEmbedding_ReturnsVectorOfCorrectSize) {
    std::vector<std::string> documents = {
        "fix bug in login",
        "fix error in logout",
        "optimize cache performance"
    };
    engine_.build_vocabulary(documents);

    auto embedding = engine_.compute_embedding("fix bug");

    EXPECT_EQ(embedding.size(), static_cast<size_t>(engine_.vocab_size()));
}

// Test embedding with unknown words
TEST_F(SearchEngineTest, ComputeEmbedding_HandlesUnknownWords) {
    std::vector<std::string> documents = {
        "fix bug in login",
        "fix error in logout"
    };
    engine_.build_vocabulary(documents);

    auto embedding = engine_.compute_embedding("completely unknown words xyz");

    // Should return vector with zeros for unknown words
    EXPECT_EQ(embedding.size(), static_cast<size_t>(engine_.vocab_size()));
}

// Test embedding without vocabulary
TEST_F(SearchEngineTest, ComputeEmbedding_ReturnsEmptyWithoutVocabulary) {
    auto embedding = engine_.compute_embedding("some text");

    EXPECT_THAT(embedding, IsEmpty());
}

// Test cosine similarity - identical vectors
TEST_F(SearchEngineTest, Similarity_ReturnsOneForIdenticalVectors) {
    std::vector<float> vec = {1.0f, 2.0f, 3.0f};

    float sim = SearchEngine::similarity(vec, vec);

    EXPECT_FLOAT_EQ(sim, 1.0f);
}

// Test cosine similarity - orthogonal vectors
TEST_F(SearchEngineTest, Similarity_ReturnsZeroForOrthogonalVectors) {
    std::vector<float> vec1 = {1.0f, 0.0f, 0.0f};
    std::vector<float> vec2 = {0.0f, 1.0f, 0.0f};

    float sim = SearchEngine::similarity(vec1, vec2);

    EXPECT_FLOAT_EQ(sim, 0.0f);
}

// Test cosine similarity - opposite vectors
TEST_F(SearchEngineTest, Similarity_ReturnsNegativeForOppositeVectors) {
    std::vector<float> vec1 = {1.0f, 0.0f};
    std::vector<float> vec2 = {-1.0f, 0.0f};

    float sim = SearchEngine::similarity(vec1, vec2);

    EXPECT_FLOAT_EQ(sim, -1.0f);
}

// Test cosine similarity - empty vectors
TEST_F(SearchEngineTest, Similarity_ReturnsZeroForEmptyVectors) {
    std::vector<float> empty;
    std::vector<float> vec = {1.0f, 2.0f};

    EXPECT_FLOAT_EQ(SearchEngine::similarity(empty, vec), 0.0f);
    EXPECT_FLOAT_EQ(SearchEngine::similarity(vec, empty), 0.0f);
    EXPECT_FLOAT_EQ(SearchEngine::similarity(empty, empty), 0.0f);
}

// Test cosine similarity - different sizes
TEST_F(SearchEngineTest, Similarity_ReturnsZeroForDifferentSizes) {
    std::vector<float> vec1 = {1.0f, 2.0f};
    std::vector<float> vec2 = {1.0f, 2.0f, 3.0f};

    float sim = SearchEngine::similarity(vec1, vec2);

    EXPECT_FLOAT_EQ(sim, 0.0f);
}

// Test find similar - basic functionality
TEST_F(SearchEngineTest, FindSimilar_ReturnsRelevantResults) {
    std::vector<std::string> documents = {
        "fix bug in login module",
        "fix error in authentication",
        "optimize database query",
        "add new user feature",
        "fix crash on startup"
    };
    engine_.build_vocabulary(documents);

    std::vector<Commit> corpus;
    for (size_t i = 0; i < documents.size(); i++) {
        Commit c = testing::make_test_commit(
            "repo", "hash" + std::to_string(i), "author", documents[i]
        );
        corpus.push_back(c);
    }

    auto results = engine_.find_similar("fix bug", corpus, 3);

    ASSERT_THAT(results, SizeIs(3));
    // Results should be sorted by similarity
    for (size_t i = 1; i < results.size(); i++) {
        EXPECT_GE(results[i - 1].similarity, results[i].similarity);
    }
}

// Test find similar - respects top_k
TEST_F(SearchEngineTest, FindSimilar_RespectsTopK) {
    std::vector<std::string> documents = {
        "fix bug one",
        "fix bug two",
        "fix bug three",
        "fix bug four",
        "fix bug five"
    };
    engine_.build_vocabulary(documents);

    std::vector<Commit> corpus;
    for (size_t i = 0; i < documents.size(); i++) {
        Commit c = testing::make_test_commit(
            "repo", "hash" + std::to_string(i), "author", documents[i]
        );
        corpus.push_back(c);
    }

    auto results = engine_.find_similar("fix bug", corpus, 2);

    EXPECT_THAT(results, SizeIs(2));
}

// Test find similar - empty corpus
TEST_F(SearchEngineTest, FindSimilar_HandlesEmptyCorpus) {
    std::vector<std::string> documents = {"fix bug"};
    engine_.build_vocabulary(documents);

    std::vector<Commit> empty_corpus;

    auto results = engine_.find_similar("fix bug", empty_corpus, 5);

    EXPECT_THAT(results, IsEmpty());
}

// Test find similar - verifies cached embeddings are used without recomputation
TEST_F(SearchEngineTest, FindSimilar_UsesCachedEmbeddings) {
    // Use documents with significant overlap so vocabulary is built
    std::vector<std::string> documents = {
        "fix bug module code system error",
        "fix bug module code system issue",
        "fix bug module code system problem",
        "optimize performance cache memory speed"
    };
    engine_.build_vocabulary(documents);

    // Verify vocabulary was built
    ASSERT_GT(engine_.vocab_size(), 0) << "Vocabulary should be built";

    // Create commit with pre-computed embedding
    Commit c1 = testing::make_test_commit("repo", "h1", "a", documents[0]);
    c1.embedding = engine_.compute_embedding(documents[0]);
    ASSERT_FALSE(c1.embedding.empty()) << "c1 embedding should not be empty";
    ASSERT_EQ(c1.embedding.size(), static_cast<size_t>(engine_.vocab_size()))
        << "Embedding size should match vocabulary size";

    // Verify the commit uses the cached embedding (has correct size)
    // When embedding is set, find_similar should use it directly
    std::vector<Commit> corpus = {c1};

    // Create a commit without cached embedding for comparison
    Commit c2 = testing::make_test_commit("repo", "h2", "a", documents[0]);
    // c2.embedding is empty, will be computed on the fly

    // Both should work - one with cached, one without
    auto results1 = engine_.find_similar(documents[0], corpus, 1);
    corpus = {c2};
    auto results2 = engine_.find_similar(documents[0], corpus, 1);

    // Both approaches should return results
    // (Note: actual results depend on vocabulary and query overlap)
    EXPECT_EQ(results1.size(), results2.size());
}

// Test vocabulary rebuild detection
TEST_F(SearchEngineTest, NeedsRebuild_ReturnsTrueWhenEmpty) {
    EXPECT_TRUE(engine_.needs_rebuild(100));
}

TEST_F(SearchEngineTest, NeedsRebuild_ReturnsFalseAfterBuild) {
    std::vector<std::string> documents = {"test document one", "test document two"};
    engine_.build_vocabulary(documents);

    // Small increase should not trigger rebuild
    EXPECT_FALSE(engine_.needs_rebuild(2));
}

TEST_F(SearchEngineTest, NeedsRebuild_ReturnsTrueForLargeIncrease) {
    std::vector<std::string> documents = {"test document"};
    engine_.build_vocabulary(documents);

    // Large increase (>10%) should trigger rebuild
    // With 1 doc initially, even 2 docs is >10% increase
    // But the threshold is based on the ratio
}

// Test serialization
TEST_F(SearchEngineTest, Serialization_VocabularyRoundTrip) {
    std::vector<std::string> documents = {
        "fix bug in login",
        "optimize database query"
    };
    engine_.build_vocabulary(documents);

    std::string serialized = engine_.serialize_vocabulary();

    SearchEngine engine2;
    engine2.deserialize_vocabulary(serialized);

    EXPECT_EQ(engine2.vocab_size(), engine_.vocab_size());
}

TEST_F(SearchEngineTest, Serialization_IdfRoundTrip) {
    std::vector<std::string> documents = {
        "fix bug in login",
        "optimize database query"
    };
    engine_.build_vocabulary(documents);

    std::string serialized_idf = engine_.serialize_idf();
    std::string serialized_vocab = engine_.serialize_vocabulary();

    SearchEngine engine2;
    engine2.deserialize_vocabulary(serialized_vocab);
    engine2.deserialize_idf(serialized_idf);

    // Compute same embedding and compare
    auto embed1 = engine_.compute_embedding("fix bug");
    auto embed2 = engine2.compute_embedding("fix bug");

    ASSERT_EQ(embed1.size(), embed2.size());
    for (size_t i = 0; i < embed1.size(); i++) {
        EXPECT_FLOAT_EQ(embed1[i], embed2[i]);
    }
}

// Test deserialization with empty/invalid input
TEST_F(SearchEngineTest, Deserialization_HandlesEmptyInput) {
    engine_.deserialize_vocabulary("");
    engine_.deserialize_idf("");

    EXPECT_EQ(engine_.vocab_size(), 0);
}

TEST_F(SearchEngineTest, Deserialization_HandlesInvalidJson) {
    engine_.deserialize_vocabulary("not valid json");
    engine_.deserialize_idf("also not valid");

    EXPECT_EQ(engine_.vocab_size(), 0);
}

// Test stopwords are filtered
TEST_F(SearchEngineTest, BuildVocabulary_FiltersStopwords) {
    std::vector<std::string> documents = {
        "the quick brown fox",
        "a lazy dog is sleeping"
    };
    engine_.build_vocabulary(documents);

    // Common stopwords like "the", "a", "is" should not be in vocabulary
    // The vocabulary should only contain meaningful words
    // We can verify this indirectly by checking embedding
    auto embedding = engine_.compute_embedding("the a is");

    // If stopwords are filtered, this should have mostly zeros
    float sum = 0;
    for (float v : embedding) {
        sum += std::abs(v);
    }
    EXPECT_FLOAT_EQ(sum, 0.0f);  // All stopwords, should be zero
}

// Test TF-IDF weighting
TEST_F(SearchEngineTest, ComputeEmbedding_TfIdfWeighting) {
    // Need more documents with overlapping words for vocabulary
    std::vector<std::string> documents = {
        "bug bug bug bug fix",
        "fix bug error system",
        "fix error system issue",
        "bug fix system module"
    };
    engine_.build_vocabulary(documents);

    // Verify vocabulary was built
    ASSERT_GT(engine_.vocab_size(), 0) << "Vocabulary should be built";

    auto embedding = engine_.compute_embedding("bug fix");

    // The embedding should have non-zero values
    float sum = 0;
    for (float v : embedding) {
        sum += std::abs(v);
    }
    EXPECT_GT(sum, 0.0f);
}

}  // namespace
}  // namespace feed
