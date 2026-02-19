# Feed MCP Server

This document describes the Model Context Protocol (MCP) interface for Feed, allowing AI assistants to query commit data and perform semantic search across your organization's repositories.

## Overview

Feed exposes its functionality as MCP tools that can be called by AI assistants like Claude. The MCP server communicates via stdio using JSON-RPC 2.0.

## Setup

### 1. Build the MCP Server

```bash
make build
```

This builds two executables:
- `build/feed` - CLI tool for manual use
- `build/feed_mcp` - MCP server for AI assistants

### 2. Initialize Feed (required first)

Before using the MCP server, initialize Feed with your organization:

```bash
export GITHUB_FEED_TOKEN=ghp_xxx
./build/feed init --org myorg --language go --max-repos 50
./build/feed sync
```

### 3. Configure Claude Desktop

Add to your Claude Desktop configuration (`~/Library/Application Support/Claude/claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "feed": {
      "command": "/absolute/path/to/feed/build/feed_mcp",
      "env": {
        "GITHUB_FEED_TOKEN": "your_github_token"
      }
    }
  }
}
```

### 4. Test the MCP Server

You can test the server manually:

```bash
# Test initialize
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./build/feed_mcp

# Test list tools
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | ./build/feed_mcp

# Test call a tool
echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_available_tags","arguments":{}}}' | ./build/feed_mcp
```

## Available Tools

### get_recent_commits

Get recent commits, optionally filtered by repository.

**Input Schema:**
```json
{
  "type": "object",
  "properties": {
    "repo": {
      "type": "string",
      "description": "Repository name to filter by (optional)"
    },
    "limit": {
      "type": "integer",
      "description": "Maximum number of commits to return",
      "default": 50
    }
  }
}
```

**Example Response:**
```json
{
  "commits": [
    {
      "id": 1,
      "repo_name": "api-server",
      "commit_hash": "abc123def456",
      "author": "jdoe",
      "timestamp": "2024-01-15T10:30:00Z",
      "message": "Fix database connection pooling issue",
      "tags": ["bugfix"]
    }
  ],
  "count": 1,
  "repo_filter": "api-server"
}
```

---

### find_similar_commits

Find commits with similar messages using TF-IDF semantic search.

**Input Schema:**
```json
{
  "type": "object",
  "properties": {
    "query": {
      "type": "string",
      "description": "Search query (e.g., 'optimize database query')"
    },
    "top_k": {
      "type": "integer",
      "description": "Number of results to return",
      "default": 5
    }
  },
  "required": ["query"]
}
```

**Example Response:**
```json
{
  "results": [
    {
      "id": 42,
      "repo_name": "api-server",
      "commit_hash": "def789abc012",
      "author": "jsmith",
      "timestamp": "2024-01-10T14:20:00Z",
      "message": "Optimize slow database queries in user service",
      "tags": ["optimization"],
      "similarity": 0.87
    }
  ],
  "query": "optimize database query",
  "count": 1
}
```

---

### get_tagged_commits

Get commits with a specific classification tag.

**Input Schema:**
```json
{
  "type": "object",
  "properties": {
    "tag": {
      "type": "string",
      "description": "Classification tag",
      "enum": [
        "bugfix",
        "optimization",
        "refactor",
        "feature",
        "experimental",
        "temporary",
        "architectural_change",
        "documentation",
        "testing",
        "security",
        "dependency"
      ]
    },
    "days": {
      "type": "integer",
      "description": "Look back period in days",
      "default": 7
    }
  },
  "required": ["tag"]
}
```

**Example Response:**
```json
{
  "commits": [
    {
      "id": 15,
      "repo_name": "web-client",
      "commit_hash": "789xyz123abc",
      "author": "adev",
      "timestamp": "2024-01-14T09:00:00Z",
      "message": "Refactor authentication middleware",
      "tags": ["refactor"]
    }
  ],
  "tag": "refactor",
  "days": 7,
  "count": 1
}
```

---

### get_repo_activity_summary

Get activity summary and statistics for a repository.

**Input Schema:**
```json
{
  "type": "object",
  "properties": {
    "repo": {
      "type": "string",
      "description": "Repository name"
    },
    "days": {
      "type": "integer",
      "description": "Analysis period in days",
      "default": 7
    }
  },
  "required": ["repo"]
}
```

**Example Response:**
```json
{
  "repo": "api-server",
  "total_commits": 156,
  "commits_last_n_days": 23,
  "days": 7,
  "top_authors": [
    {"author": "jdoe", "commit_count": 12},
    {"author": "jsmith", "commit_count": 8},
    {"author": "adev", "commit_count": 3}
  ],
  "tag_distribution": {
    "bugfix": 8,
    "feature": 7,
    "refactor": 5,
    "documentation": 3
  }
}
```

---

### get_available_tags

List all available classification tags.

**Input Schema:**
```json
{
  "type": "object",
  "properties": {}
}
```

**Example Response:**
```json
{
  "tags": [
    "architectural_change",
    "bugfix",
    "dependency",
    "documentation",
    "experimental",
    "feature",
    "optimization",
    "refactor",
    "security",
    "temporary",
    "testing"
  ]
}
```

---

### sync_commits

Trigger a sync to fetch new commits from GitHub.

**Input Schema:**
```json
{
  "type": "object",
  "properties": {}
}
```

**Example Response:**
```json
{
  "new_commits": 15,
  "repos_synced": 10,
  "total_repos": 10,
  "errors": [],
  "vocabulary_size": 2500
}
```

---

### rebuild_search_index

Rebuild the TF-IDF search index. Use after bulk imports or if search quality degrades.

**Input Schema:**
```json
{
  "type": "object",
  "properties": {}
}
```

**Example Response:**
```json
{
  "message": "Search index rebuilt",
  "vocabulary_size": 2847,
  "commits_indexed": 1523
}
```

## Classification Tags

Commits are automatically classified based on keywords in the message:

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

## Error Handling

All tools return errors in the following format:

```json
{
  "error": "Error message describing what went wrong"
}
```

Common errors:
- `"Not initialized. Run 'feed init' first."` - Feed hasn't been configured
- `"Unknown tag 'xxx'"` - Invalid tag name passed to get_tagged_commits
- `"GitHub API rate limit exceeded"` - Too many API requests

## Data Storage

Feed stores all data locally:
- **Database**: `commits.db` (SQLite)
- **Config**: `~/.feed_config` (JSON)

No data is sent to external services except GitHub API calls during sync.
