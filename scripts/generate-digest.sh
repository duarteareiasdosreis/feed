#!/bin/bash
# Feed Daily Digest Generator (Claude-powered)
# Uses Claude Code to analyze commits via feed MCP and generate a beautiful report
#
# Usage: generate-digest.sh [OPTIONS]
#   -d, --dir DIR        Obsidian vault directory for output (required)
#   -f, --folder FOLDER  Subfolder within vault (default: "Feed Digests")
#   -l, --limit N        Number of recent commits to analyze (default: 50)
#   --days N             Only include commits from last N days (default: 1)
#   -h, --help           Show this help message
#
# Environment variables:
#   OBSIDIAN_VAULT       Default Obsidian vault path (can be overridden with -d)
#
# Example:
#   generate-digest.sh -d ~/Documents/Obsidian/MyVault -f "Daily Digests"
#   generate-digest.sh -d "$OBSIDIAN_VAULT" --days 7 -l 100

set -e

# Defaults
OBSIDIAN_DIR="${OBSIDIAN_VAULT:-}"
FOLDER="Feed Digests"
LIMIT=50
DAYS=1

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--dir)
            OBSIDIAN_DIR="$2"
            shift 2
            ;;
        -f|--folder)
            FOLDER="$2"
            shift 2
            ;;
        -l|--limit)
            LIMIT="$2"
            shift 2
            ;;
        --days)
            DAYS="$2"
            shift 2
            ;;
        -h|--help)
            head -17 "$0" | tail -15
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Validate Obsidian directory
if [ -z "$OBSIDIAN_DIR" ]; then
    echo "Error: Obsidian vault directory required"
    echo "Use -d/--dir or set OBSIDIAN_VAULT environment variable"
    exit 1
fi

if [ ! -d "$OBSIDIAN_DIR" ]; then
    echo "Error: Obsidian vault not found: $OBSIDIAN_DIR"
    exit 1
fi

# Ensure output folder exists
OUTPUT_DIR="$OBSIDIAN_DIR/$FOLDER"
mkdir -p "$OUTPUT_DIR"

# Generate filename with date
DATE=$(date "+%Y-%m-%d")
OUTPUT_FILE="$OUTPUT_DIR/$DATE.md"

# Check if claude CLI is available
if ! command -v claude &> /dev/null; then
    echo "Error: claude CLI not found"
    echo "Install Claude Code: https://docs.anthropic.com/claude-code"
    exit 1
fi

echo "Generating digest for $DATE..."
echo "Output: $OUTPUT_FILE"

# Build the prompt for Claude
PROMPT="You have access to the feed MCP tools. Please:

1. First sync the latest commits using sync_commits
2. Then get recent commits (limit: $LIMIT) using get_recent_commits
3. Analyze the commits and create a beautiful, well-organized markdown digest

Format the output as a clean Obsidian-compatible markdown page with:
- YAML frontmatter with: date, tags (feed, digest, daily-log), and a generated title
- An executive summary (2-3 sentences) of what happened
- Group commits logically by THEME or AREA (not just by repo), such as:
  - Infrastructure & DevOps
  - New Features
  - Bug Fixes & Maintenance
  - Documentation
  - Refactoring & Tech Debt
  - etc.
- For each group, provide:
  - A brief summary of the changes in that area
  - Bullet points with the key commits (author, repo, concise description)
- Highlight any notable patterns, large changes, or items worth attention
- End with a 'Key Takeaways' section if there's anything significant

Keep it concise but informative - this is a daily news feed for staying updated on org activity.
Focus on the last $DAYS day(s) of activity.

Output ONLY the markdown content, nothing else."

# Run Claude with the prompt and write directly to file
claude -p "$PROMPT" --allowedTools "mcp__feed__*" > "$OUTPUT_FILE"

echo "Digest generated: $OUTPUT_FILE"
