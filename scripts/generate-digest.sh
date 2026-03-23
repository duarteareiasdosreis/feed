#!/bin/bash
# Feed Daily Digest Generator (Claude-powered)
# Uses Claude Code to analyze commits via feed MCP and generate a beautiful report
# Optionally includes OPEX health report and meeting preparation notes.
#
# Usage: generate-digest.sh [OPTIONS]
#   -d, --dir DIR        Obsidian vault directory for output (required)
#   -f, --folder FOLDER  Subfolder within vault (default: "Feed Digests")
#   -l, --limit N        Number of recent commits to analyze (default: 50)
#   --days N             Only include commits from last N days (default: 1)
#   --opex-team TEAM     Team name for OPEX health report (e.g. "Shelob")
#   --todo FILE          Path to TODO markdown file for meeting prep context
#   --no-calendar        Skip calendar/meeting preparation section
#   -h, --help           Show this help message
#
# Environment variables:
#   OBSIDIAN_VAULT       Default Obsidian vault path (can be overridden with -d)
#
# Example:
#   generate-digest.sh -d ~/Documents/Obsidian/MyVault -f "Daily Digests"
#   generate-digest.sh -d "$OBSIDIAN_VAULT" --opex-team Shelob --todo ~/vault/TODO.md
#   generate-digest.sh -d "$OBSIDIAN_VAULT" --days 7 -l 100

set -e

# Defaults
OBSIDIAN_DIR="${OBSIDIAN_VAULT:-}"
FOLDER="Feed Digests"
LIMIT=50
DAYS=1
OPEX_TEAM=""
TODO_FILE=""
INCLUDE_CALENDAR=true

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
        --opex-team)
            OPEX_TEAM="$2"
            shift 2
            ;;
        --todo)
            TODO_FILE="$2"
            shift 2
            ;;
        --no-calendar)
            INCLUDE_CALENDAR=false
            shift
            ;;
        -h|--help)
            head -23 "$0" | tail -21
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

# Validate todo file if provided
if [ -n "$TODO_FILE" ] && [ ! -f "$TODO_FILE" ]; then
    echo "Warning: TODO file not found: $TODO_FILE (skipping meeting prep context)"
    TODO_FILE=""
fi

# Ensure output folder exists
OUTPUT_DIR="$OBSIDIAN_DIR/$FOLDER"
mkdir -p "$OUTPUT_DIR"

# Generate filename with date
DATE=$(date "+%Y-%m-%d")
TOMORROW=$(date -v+1d "+%Y-%m-%d" 2>/dev/null || date -d "+1 day" "+%Y-%m-%d")
OUTPUT_FILE="$OUTPUT_DIR/$DATE.md"

# Check if claude CLI is available
if ! command -v claude &> /dev/null; then
    echo "Error: claude CLI not found"
    echo "Install Claude Code: https://docs.anthropic.com/claude-code"
    exit 1
fi

echo "Generating digest for $DATE..."
echo "Output: $OUTPUT_FILE"

# Read TODO file content if provided
TODO_CONTENT=""
if [ -n "$TODO_FILE" ]; then
    TODO_CONTENT=$(cat "$TODO_FILE")
    echo "Including TODO context from: $TODO_FILE"
fi

# Build allowed tools list
ALLOWED_TOOLS="mcp__feed__*"
if [ -n "$OPEX_TEAM" ]; then
    ALLOWED_TOOLS="$ALLOWED_TOOLS,mcp__opex__*"
    echo "Including OPEX report for team: $OPEX_TEAM"
fi
if [ "$INCLUDE_CALENDAR" = true ]; then
    ALLOWED_TOOLS="$ALLOWED_TOOLS,mcp__google-calendar__*"
    echo "Including calendar events for meeting prep"
fi

# Build the prompt for Claude
PROMPT="You have access to several MCP tools. Today is $DATE and tomorrow is $TOMORROW.
Please generate a daily digest with the following sections:

---

## SECTION 1: Commit Activity

1. First sync the latest commits using sync_commits
2. Then get recent commits (limit: $LIMIT) using get_recent_commits
3. Analyze the commits and create a well-organized summary

For the commit section:
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
- Focus on the last $DAYS day(s) of activity"

# Add OPEX section if team specified
if [ -n "$OPEX_TEAM" ]; then
    PROMPT="$PROMPT

---

## SECTION 2: OPEX Health Report ($OPEX_TEAM)

Query the OPEX MCP tools for team '$OPEX_TEAM' (use the team filter). Check the following and report ONLY items that need attention (non-zero counts or items approaching/breaching SLA):
- Incidents (get_incidents)
- SLOs with depleted error budgets (get_slos)
- Remediation items out of SLA (get_remediation_items)
- Dependency issues (get_dependency_issues)
- Container vulnerabilities (get_container_vulnerabilities)
- Application vulnerabilities (get_application_vulnerabilities)
- GitHub secret alerts (get_github_secret_alerts)
- Repository findings (get_repository_findings)
- Jira escalated issues (get_jira_escalated_issues)

Format this section as:
- A quick health summary (e.g. 'All clear' or 'X items need attention')
- For each area with issues, list what's out of SLA or in warning with brief details
- End with a 'Keep in Mind' subsection highlighting the most urgent items and recommended actions for the day"
fi

# Add calendar/meeting prep section
if [ "$INCLUDE_CALENDAR" = true ]; then
    PROMPT="$PROMPT

---

## SECTION 3: Meeting Preparation (Tomorrow: $TOMORROW)

Use the Google Calendar MCP tools to fetch calendar events for tomorrow ($TOMORROW).
If the calendar tools are not available, skip this section gracefully and note that calendar access was unavailable.

For each meeting that likely requires preparation:
- List the meeting name, time, and attendees (if available)
- Focus especially on scrum ceremonies (standup, sprint planning, retro, refinement), 1:1s, and team syncs
- For scrum ceremonies, suggest an agenda or talking points"

    if [ -n "$TODO_CONTENT" ]; then
        PROMPT="$PROMPT
- Cross-reference with the TODO list below to suggest relevant items to bring up
- For sprint planning or refinement, suggest which open TODOs could be discussed or prioritized

### Current TODO List:
\`\`\`
$TODO_CONTENT
\`\`\`"
    fi
fi

# Final formatting instructions
PROMPT="$PROMPT

---

## OUTPUT FORMAT

Format the output as a clean Obsidian-compatible markdown page with:
- YAML frontmatter with: date, tags (feed, digest, daily-log), and a generated title
- An executive summary (2-3 sentences) covering the most important points across all sections
- Then each section with clear headers
- End with a 'Key Takeaways' section summarizing action items across all sections

Keep it concise but informative - this is a daily briefing for staying on top of work.

Output ONLY the markdown content, nothing else."

# Run Claude with the prompt and write directly to file
claude -p "$PROMPT" --allowedTools "$ALLOWED_TOOLS" > "$OUTPUT_FILE"

echo "Digest generated: $OUTPUT_FILE"
