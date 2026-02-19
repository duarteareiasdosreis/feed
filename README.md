# Feed - Engineering Intelligence Tool

A lightweight local tool that scans GitHub organization repositories, stores commit metadata, and enables semantic search across commit messages.

## Features

- **GitHub Integration**: Fetches commits from all repositories in a GitHub organization
- **Automatic Classification**: Tags commits based on keywords (bugfix, optimization, refactor, etc.)
- **Semantic Search**: Find similar commits using TF-IDF based similarity
- **Local Storage**: SQLite database for efficient local storage and querying
- **MCP-Ready**: Designed for future Model Context Protocol integration

## Requirements

- CMake 3.14+
- C++17 compiler
- libcurl
- SQLite3

On macOS:
```bash
# These are typically pre-installed with Xcode Command Line Tools
xcode-select --install
```

On Ubuntu/Debian:
```bash
sudo apt-get install build-essential cmake libcurl4-openssl-dev libsqlite3-dev
```

## Building

```bash
# Build release version
make build

# Or build with debug symbols
make debug

# Run tests
make test
```

## Usage

### Initialize

```bash
# Set up with your GitHub organization
./build/feed init --org <organization> --token <github-token>
```

### Sync Commits

```bash
# Fetch latest commits from all repositories
./build/feed sync
```

### Query Commands

```bash
# List recent commits
./build/feed recent [--repo <name>] [--limit 50]

# Semantic search for similar commits
./build/feed similar "optimize database query" [--top 5]

# Get commits by classification tag
./build/feed tagged optimization [--days 7]

# Repository activity summary
./build/feed summary <repo-name> [--days 7]

# List available classification tags
./build/feed tags
```

## Classification Tags

Commits are automatically classified into these categories:

| Tag | Keywords |
|-----|----------|
| `bugfix` | fix, bug, issue, error, crash, patch |
| `optimization` | perf, optimize, speed, cache, memory |
| `refactor` | refactor, restructure, cleanup, simplify |
| `feature` | add, new, feature, implement, support |
| `experimental` | experiment, poc, prototype, wip, spike |
| `temporary` | temp, hack, workaround, fixme, todo |
| `architectural_change` | architect, design, migration, rewrite |
| `documentation` | doc, readme, comment, explain |
| `testing` | test, spec, unittest, coverage |
| `security` | security, vulnerability, auth, encrypt |
| `dependency` | dependency, upgrade, bump, version |

## Embedding Approach

Feed uses **TF-IDF (Term Frequency-Inverse Document Frequency)** for semantic search, a classical information retrieval technique that works well for commit message similarity:

### How It Works

1. **Tokenization**: Commit messages are split into words, converted to lowercase, and stopwords are removed.

2. **Vocabulary Building**: A vocabulary is built from all commit messages, keeping only words that appear in at least 2 documents to filter noise.

3. **TF-IDF Vectors**: Each commit message is represented as a sparse vector where:
   - **TF (Term Frequency)** = word count / total words in message
   - **IDF (Inverse Document Frequency)** = log(N / (1 + documents containing word))
   - **TF-IDF** = TF × IDF

4. **Similarity Search**: Cosine similarity is used to find commits with similar TF-IDF vectors.

### Why TF-IDF?

- **No External Dependencies**: Pure C++ implementation, no ML frameworks or API calls needed
- **Fast**: Vector operations are efficient for small-to-medium corpora
- **Interpretable**: Results are based on word overlap, easy to understand
- **Low Storage**: Sparse vectors only store non-zero terms
- **Offline**: Works entirely locally without network access

### Vocabulary Management

- Maximum vocabulary size: 10,000 terms
- Words must appear in ≥2 documents to be included
- Vocabulary is rebuilt when corpus grows by >10%
- Vocabulary and IDF scores are persisted in SQLite

### Storage Format

Embeddings are stored as binary BLOBs in SQLite:
- Each embedding is a float vector of size `vocab_size`
- Typical commit: ~2KB embedding + ~500 bytes metadata

### Example

```
Query: "optimize database query"
→ Tokens: ["optimize", "database", "query"]
→ TF-IDF vector: sparse vector with non-zero weights for matching vocabulary terms
→ Similarity: cosine similarity against all stored commit embeddings
→ Results: commits sorted by similarity score (0.0 - 1.0)
```

## Project Structure

```
feed/
├── CMakeLists.txt       # Build configuration
├── Makefile             # Convenience targets
├── include/             # Header files
│   ├── config.h         # Configuration constants
│   ├── storage.h        # SQLite storage layer
│   ├── github_client.h  # GitHub API client
│   ├── classifier.h     # Keyword-based tagging
│   ├── search.h         # TF-IDF search engine
│   └── api.h            # Public API functions
├── src/                 # Implementation files
├── tests/               # Unit tests (gtest/gmock)
├── third_party/         # Vendored dependencies
│   └── nlohmann/        # JSON library
└── mcp/                 # MCP server stub
```

## Testing

```bash
# Run all tests
make test

# Run specific test suite
make test-storage
make test-classifier
make test-search
make test-api

# Verbose output
make test-verbose
```

## License

MIT
