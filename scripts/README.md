# Feed Scripts

Utility scripts for automating feed operations.

## generate-digest.sh

Generates a daily briefing as a markdown file, suitable for note-taking apps like Obsidian. Includes:

- **Commit activity** from tracked repos via the feed MCP
- **OPEX health report** for a team (optional, via opex MCP)
- **Meeting preparation notes** for the next day (optional, via Google Calendar MCP + TODO file)

### Usage

```bash
./scripts/generate-digest.sh [OPTIONS]
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-d, --dir DIR` | Obsidian vault directory for output (required) | `$OBSIDIAN_VAULT` |
| `-f, --folder FOLDER` | Subfolder within vault | `Feed Digests` |
| `-l, --limit N` | Number of recent commits to analyze | `50` |
| `--days N` | Only include commits from last N days | `1` |
| `--opex-team TEAM` | Team name for OPEX health report | |
| `--todo FILE` | Path to TODO markdown file for meeting prep | |
| `--no-calendar` | Skip calendar/meeting preparation section | |
| `-h, --help` | Show help message | |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `OBSIDIAN_VAULT` | Default Obsidian vault path (can be overridden with `-d`) |

### MCP Dependencies

| MCP Server | Required For | Tools Used |
|------------|-------------|------------|
| `feed` | Commit activity (always) | `sync_commits`, `get_recent_commits` |
| `opex` | OPEX health report (`--opex-team`) | Various `get_*` tools filtered by team |
| `google-calendar` | Meeting prep (unless `--no-calendar`) | Calendar event listing |

### Examples

```bash
# Basic usage - commit digest only
./scripts/generate-digest.sh -d "$OBSIDIAN_VAULT"

# Full daily briefing with OPEX and meeting prep
./scripts/generate-digest.sh -d "$OBSIDIAN_VAULT" \
  --opex-team Shelob \
  --todo ~/Documents/Obsidian\ Vault/TODO.md

# Just commits and OPEX, no calendar
./scripts/generate-digest.sh -d "$OBSIDIAN_VAULT" \
  --opex-team Shelob --no-calendar

# Week-in-review with more commits
./scripts/generate-digest.sh -d "$OBSIDIAN_VAULT" --days 7 -l 100
```

### macOS Keychain Integration

On macOS, the script can retrieve your GitHub token from Keychain:

```bash
# Store token in Keychain (one-time setup)
security add-generic-password -a "$USER" -s "github-feed-token" -w "ghp_your_token_here"

# Script will automatically use it
./scripts/generate-digest.sh
```

### Scheduling with launchd (macOS)

Create a plist at `~/Library/LaunchAgents/com.feed.digest.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.feed.digest</string>
    <key>ProgramArguments</key>
    <array>
        <string>/path/to/feed/scripts/generate-digest.sh</string>
        <string>-o</string>
        <string>/path/to/output/digest.md</string>
        <string>-t</string>
        <string>My Daily Digest</string>
    </array>
    <key>StartCalendarInterval</key>
    <dict>
        <key>Hour</key>
        <integer>9</integer>
        <key>Minute</key>
        <integer>0</integer>
    </dict>
    <key>StandardOutPath</key>
    <string>/tmp/feed-digest.log</string>
    <key>StandardErrorPath</key>
    <string>/tmp/feed-digest.err</string>
</dict>
</plist>
```

Load the job:

```bash
launchctl load ~/Library/LaunchAgents/com.feed.digest.plist
```

### Scheduling with cron (Linux)

```bash
# Edit crontab
crontab -e

# Add line to run at 9 AM daily
0 9 * * * GITHUB_FEED_TOKEN=ghp_xxx /path/to/feed/scripts/generate-digest.sh -o /path/to/digest.md
```
