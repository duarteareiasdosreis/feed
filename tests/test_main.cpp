#include <gtest/gtest.h>

// Main entry point for Google Test
// Note: Using gtest_main, so this file can be minimal
// Custom main is available if needed for global setup/teardown

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
