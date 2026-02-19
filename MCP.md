# Feed MCP Server

This document describes the Model Context Protocol (MCP) interface for Feed, allowing AI assistants to query commit data and perform semantic search across your organization's repositories.

## Overview

Feed exposes its functionality as MCP tools that can be called by AI assistants like Claude. The MCP server communicates via stdio using JSON-RPC 2.0.

## Recommended Workflow

For MCP clients, follow this sequence:

1. **Check status** (fast): Call `get_sync_status` to see tracked repos, last sync time, and commit counts
2. **Initialize if needed**: If not initialized, call `init_feed` with the user's GitHub organization
3. **Sync commits**: Call `sync_commits` to fetch commits from GitHub (only if needed)
4. **Query data**: Use `get_recent_commits`, `find_similar_commits`, `get_tagged_commits`, or `get_repo_summary`

**Tip:** Use `get_sync_status` instead of `get_config` to quickly check what repos are tracked - it's faster and includes commit counts.

**Important:** Use small limits (10) for initial queries to avoid large responses that fill context.

## Setup

### 1. Build the MCP Server

```bash
make build
```

This builds two executables:
- `build/feed` - CLI tool for manual use
- `build/feed_mcp` - MCP server for AI assistants

### 2. Set GitHub Token

The MCP server needs a GitHub token via environment variable:

```bash
export GITHUB_FEED_TOKEN=ghp_xxx
```

**Note:** Initialization can be done via CLI or through the MCP `init_feed` tool.

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

Get recent commits, optionally filtered by repository. **Use limit=10 for initial queries to avoid large responses.**

**Input Schema:**
```json
{
  "type": "object",
  "properties": {
    "repo": {
      "type": "string",
      "description": "Repository name to filter by (recommended to reduce response size)"
    },
    "limit": {
      "type": "integer",
      "description": "Max commits to return. Start with 10, increase if needed.",
      "default": 10
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

Find commits with similar messages using TF-IDF semantic search. Returns top matches ranked by similarity.

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
      "description": "Number of results (keep low: 3-5 recommended)",
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

Get commits with a specific classification tag. **Use small values for days (7) and limit (10) to avoid large responses.**

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
      "description": "Look back period in days (default 7, max recommended 14)",
      "default": 7
    },
    "limit": {
      "type": "integer",
      "description": "Max commits to return (default 10, increase only if needed)",
      "default": 10
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

### get_repo_summary

Get activity summary and statistics for a repository. Lightweight - returns aggregated stats, not full commits.

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

### init_feed

Initialize feed with a GitHub organization. **Required before sync_commits.**

**Input Schema:**
```json
{
  "type": "object",
  "properties": {
    "org": {
      "type": "string",
      "description": "GitHub organization name (required)"
    },
    "languages": {
      "type": "array",
      "items": { "type": "string" },
      "description": "Filter by programming languages (e.g., ['go', 'python'])"
    },
    "max_repos": {
      "type": "integer",
      "description": "Maximum repositories to track (recommended: 10-50)",
      "default": 50
    },
    "include_repos": {
      "type": "array",
      "items": { "type": "string" },
      "description": "Specific repo names to include"
    },
    "active_days": {
      "type": "integer",
      "description": "Only repos with commits in last N days",
      "default": 0
    }
  },
  "required": ["org"]
}
```

**Example Response:**
```json
{
  "success": true,
  "org": "myorg",
  "db_path": "commits.db",
  "token_source": "environment_variable",
  "languages": ["go", "python"],
  "max_repos": 50
}
```

---

### get_config

Get current feed configuration. **Call this first to check if feed is initialized.** If error "Not initialized", use `init_feed` to set up.

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
  "org": "myorg",
  "token": "***from GITHUB_FEED_TOKEN env***",
  "filter": {
    "languages": ["go"],
    "topics": [],
    "include_repos": ["api-server"],
    "exclude_repos": [],
    "active_days": 0,
    "max_repos": 50,
    "min_stars": 0,
    "include_archived": false,
    "include_forks": true
  }
}
```

---

### get_sync_status

**FAST, local-only query** - no API calls. Returns tracked repos, last sync time, commit counts, and current filters. Use this to quickly check what's being tracked before querying or deciding to add repos.

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
  "org": "myorg",
  "last_sync": "2024-01-15T10:30:00Z",
  "repo_count": 3,
  "total_commits": 450,
  "repos": [
    {
      "name": "api-server",
      "last_fetch": "2024-01-15T10:30:00Z",
      "commit_count": 200
    },
    {
      "name": "web-client",
      "last_fetch": "2024-01-15T10:29:55Z",
      "commit_count": 150
    }
  ],
  "filter": {
    "languages": ["go"],
    "topics": [],
    "include_repos": [],
    "exclude_repos": [],
    "active_days": 0,
    "max_repos": 50
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

### list_repos

List repositories matching the current filter configuration.

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
  "count": 3,
  "repositories": [
    {
      "name": "api-server",
      "language": "Go",
      "stars": 45,
      "topics": ["backend", "api"],
      "last_push": "2024-01-15T10:30:00Z",
      "fork": false,
      "archived": false
    }
  ]
}
```

---

### sync_commits

Trigger a sync to fetch new commits from GitHub. **Requires `init_feed` to be called first.**

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

### rebuild_index

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
