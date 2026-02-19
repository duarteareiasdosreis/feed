# Feed - Engineering Intelligence Tool
# Convenience Makefile for common operations

BUILD_DIR := build
CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Release

.PHONY: all build debug clean test run help install configure rebuild

# Default target
all: build

# Configure and build in release mode
build: configure
	@cmake --build $(BUILD_DIR) -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Configure and build in debug mode
debug: CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Debug
debug: configure
	@cmake --build $(BUILD_DIR) -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Configure the build
configure:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) ..

# Clean build artifacts
clean:
	@rm -rf $(BUILD_DIR)
	@rm -f commits.db
	@rm -f test_*.db
	@echo "Cleaned build directory and databases"

# Run the test suite
test: build
	@cd $(BUILD_DIR) && ./feed_tests

# Run tests with verbose output
test-verbose: build
	@cd $(BUILD_DIR) && ./feed_tests --gtest_color=yes

# Run specific test suite
test-storage: build
	@cd $(BUILD_DIR) && ./feed_tests --gtest_filter="StorageTest.*"

test-classifier: build
	@cd $(BUILD_DIR) && ./feed_tests --gtest_filter="ClassifierTest.*"

test-search: build
	@cd $(BUILD_DIR) && ./feed_tests --gtest_filter="SearchEngineTest.*"

test-api: build
	@cd $(BUILD_DIR) && ./feed_tests --gtest_filter="ApiTest.*"

# Run the CLI tool
run: build
	@$(BUILD_DIR)/feed $(ARGS)

# Show help
run-help: build
	@$(BUILD_DIR)/feed --help

# Initialize with organization (requires ORG and TOKEN env vars)
init: build
	@$(BUILD_DIR)/feed init --org $(ORG) --token $(TOKEN)

# Sync commits from GitHub
sync: build
	@$(BUILD_DIR)/feed sync

# Show available tags
tags: build
	@$(BUILD_DIR)/feed tags

# Full rebuild
rebuild: clean build

# Install to /usr/local/bin (requires sudo)
install: build
	@sudo cp $(BUILD_DIR)/feed /usr/local/bin/feed
	@echo "Installed feed to /usr/local/bin/feed"

# Generate compile_commands.json for IDE integration
compile-commands: configure
	@echo "compile_commands.json generated in $(BUILD_DIR)/"

# Help
help:
	@echo "Feed - Engineering Intelligence Tool"
	@echo ""
	@echo "Build targets:"
	@echo "  make build        - Build in release mode (default)"
	@echo "  make debug        - Build in debug mode"
	@echo "  make rebuild      - Clean and rebuild"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make install      - Install to /usr/local/bin"
	@echo ""
	@echo "Test targets:"
	@echo "  make test         - Run all tests"
	@echo "  make test-verbose - Run tests with verbose output"
	@echo "  make test-storage - Run storage tests only"
	@echo "  make test-classifier - Run classifier tests only"
	@echo "  make test-search  - Run search tests only"
	@echo "  make test-api     - Run API tests only"
	@echo ""
	@echo "Run targets:"
	@echo "  make run ARGS='<args>' - Run feed with arguments"
	@echo "  make run-help     - Show feed help"
	@echo "  make init ORG=<org> TOKEN=<token> - Initialize"
	@echo "  make sync         - Sync commits from GitHub"
	@echo "  make tags         - Show available classification tags"
	@echo ""
	@echo "Examples:"
	@echo "  make run ARGS='recent --limit 10'"
	@echo "  make run ARGS='similar \"fix bug\"'"
	@echo "  make init ORG=myorg TOKEN=ghp_xxx"
