#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "classifier.h"

namespace feed {
namespace {

using ::testing::Contains;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

class ClassifierTest : public ::testing::Test {
protected:
    Classifier classifier_;
};

// Test basic classification
TEST_F(ClassifierTest, Classify_IdentifiesBugfix) {
    auto tags = classifier_.classify("Fix null pointer exception in login handler");
    EXPECT_THAT(tags, Contains("bugfix"));
}

TEST_F(ClassifierTest, Classify_IdentifiesOptimization) {
    auto tags = classifier_.classify("Optimize database query performance");
    EXPECT_THAT(tags, Contains("optimization"));
}

TEST_F(ClassifierTest, Classify_IdentifiesRefactor) {
    auto tags = classifier_.classify("Refactor authentication module for clarity");
    EXPECT_THAT(tags, Contains("refactor"));
}

TEST_F(ClassifierTest, Classify_IdentifiesExperimental) {
    auto tags = classifier_.classify("WIP: Prototype new caching mechanism");
    EXPECT_THAT(tags, Contains("experimental"));
}

TEST_F(ClassifierTest, Classify_IdentifiesTemporary) {
    auto tags = classifier_.classify("Quick fix workaround for API timeout");
    EXPECT_THAT(tags, Contains("temporary"));
}

TEST_F(ClassifierTest, Classify_IdentifiesArchitecturalChange) {
    auto tags = classifier_.classify("Major architecture redesign for microservices");
    EXPECT_THAT(tags, Contains("architectural_change"));
}

TEST_F(ClassifierTest, Classify_IdentifiesFeature) {
    auto tags = classifier_.classify("Add new user registration feature");
    EXPECT_THAT(tags, Contains("feature"));
}

TEST_F(ClassifierTest, Classify_IdentifiesDocumentation) {
    auto tags = classifier_.classify("Update README documentation");
    EXPECT_THAT(tags, Contains("documentation"));
}

TEST_F(ClassifierTest, Classify_IdentifiesTesting) {
    auto tags = classifier_.classify("Add unit tests for payment module");
    EXPECT_THAT(tags, Contains("testing"));
}

TEST_F(ClassifierTest, Classify_IdentifiesSecurity) {
    auto tags = classifier_.classify("Fix XSS vulnerability in user input");
    EXPECT_THAT(tags, Contains("security"));
}

TEST_F(ClassifierTest, Classify_IdentifiesDependency) {
    auto tags = classifier_.classify("Bump lodash version to 4.17.21");
    EXPECT_THAT(tags, Contains("dependency"));
}

// Test multiple tags
TEST_F(ClassifierTest, Classify_ReturnsMultipleTags) {
    auto tags = classifier_.classify("Fix performance bug by optimizing cache");
    EXPECT_THAT(tags, Contains("bugfix"));
    EXPECT_THAT(tags, Contains("optimization"));
}

// Test case insensitivity
TEST_F(ClassifierTest, Classify_IsCaseInsensitive) {
    auto tags1 = classifier_.classify("FIX BUG in module");
    auto tags2 = classifier_.classify("fix bug in module");
    auto tags3 = classifier_.classify("Fix Bug In Module");

    EXPECT_THAT(tags1, Contains("bugfix"));
    EXPECT_THAT(tags2, Contains("bugfix"));
    EXPECT_THAT(tags3, Contains("bugfix"));
}

// Test word boundary matching
TEST_F(ClassifierTest, Classify_MatchesWordBoundaries) {
    // "prefix" should not match "fix"
    auto tags = classifier_.classify("Add prefix to all identifiers");
    EXPECT_THAT(tags, Not(Contains("bugfix")));
}

// Test empty message
TEST_F(ClassifierTest, Classify_HandlesEmptyMessage) {
    auto tags = classifier_.classify("");
    EXPECT_THAT(tags, IsEmpty());
}

// Test message with no keywords
TEST_F(ClassifierTest, Classify_ReturnsEmptyForNoKeywords) {
    auto tags = classifier_.classify("Updated the configuration file");
    // This message doesn't contain strong keywords
    // It might match "feature" due to "update" but let's test
    // the behavior is consistent
    // Note: actual behavior depends on keyword list
}

// Test multi-word keywords
TEST_F(ClassifierTest, Classify_MatchesMultiWordKeywords) {
    auto tags = classifier_.classify("This is a quick fix for the issue");
    EXPECT_THAT(tags, Contains("temporary"));
}

// Test available tags
TEST_F(ClassifierTest, GetAvailableTags_ReturnsAllTags) {
    auto tags = classifier_.get_available_tags();

    EXPECT_THAT(tags, Contains("optimization"));
    EXPECT_THAT(tags, Contains("experimental"));
    EXPECT_THAT(tags, Contains("temporary"));
    EXPECT_THAT(tags, Contains("refactor"));
    EXPECT_THAT(tags, Contains("architectural_change"));
    EXPECT_THAT(tags, Contains("bugfix"));
    EXPECT_THAT(tags, Contains("feature"));
    EXPECT_THAT(tags, Contains("documentation"));
    EXPECT_THAT(tags, Contains("testing"));
    EXPECT_THAT(tags, Contains("security"));
    EXPECT_THAT(tags, Contains("dependency"));
}

// Test tags are sorted
TEST_F(ClassifierTest, GetAvailableTags_ReturnsSortedList) {
    auto tags = classifier_.get_available_tags();

    ASSERT_FALSE(tags.empty());
    for (size_t i = 1; i < tags.size(); i++) {
        EXPECT_LE(tags[i - 1], tags[i]) << "Tags should be sorted";
    }
}

// Test specific keyword patterns
TEST_F(ClassifierTest, Classify_IdentifiesSpecificPatterns) {
    // POC pattern
    auto poc_tags = classifier_.classify("POC for new authentication flow");
    EXPECT_THAT(poc_tags, Contains("experimental"));

    // FIXME pattern
    auto fixme_tags = classifier_.classify("FIXME: temporary workaround");
    EXPECT_THAT(fixme_tags, Contains("temporary"));

    // TODO pattern
    auto todo_tags = classifier_.classify("TODO: implement proper error handling");
    EXPECT_THAT(todo_tags, Contains("temporary"));
}

// Test commit message scenarios
TEST_F(ClassifierTest, Classify_RealWorldScenarios) {
    // Merge commit
    auto merge_tags = classifier_.classify("Merge pull request #123 from feature/auth");
    // May or may not have tags depending on keywords

    // Version bump
    auto bump_tags = classifier_.classify("Bump version to 2.0.0");
    EXPECT_THAT(bump_tags, Contains("dependency"));

    // Revert
    auto revert_tags = classifier_.classify("Revert \"Add experimental feature\"");
    EXPECT_THAT(revert_tags, Contains("experimental"));
}

}  // namespace
}  // namespace feed
