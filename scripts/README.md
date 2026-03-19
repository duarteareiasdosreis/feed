# Feed Scripts

Utility scripts for automating feed operations.

## generate-digest.sh

Syncs commits and generates a markdown digest file, suitable for note-taking apps like Obsidian.

### Usage

```bash
./scripts/generate-digest.sh [OPTIONS]
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-o, --output FILE` | Output markdown file path | `./feed-digest.md` |
| `-l, --limit N` | Number of recent commits to include | `100` |
| `-t, --title TITLE` | Digest title in the generated file | `Feed Digest` |
| `--tags TAGS` | Comma-separated tags for frontmatter | `feed,daily-digest` |
| `-h, --help` | Show help message | |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `GITHUB_FEED_TOKEN` | GitHub personal access token (required). On macOS, can also be stored in Keychain. |
| `FEED_DIR` | Feed installation directory. Defaults to the script's parent directory. |

### Examples

```bash
# Basic usage - outputs to ./feed-digest.md
./scripts/generate-digest.sh

# Custom output for Obsidian vault
./scripts/generate-digest.sh -o ~/Documents/Obsidian/Daily/feed.md

# Custom title and more commits
./scripts/generate-digest.sh -t "Zendesk Ruby Digest" -l 200 -o ~/notes/zendesk.md

# With custom tags for frontmatter
./scripts/generate-digest.sh --tags "work,ruby,daily" -o ~/notes/work-digest.md
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
