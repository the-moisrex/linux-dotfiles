# Shell Commands

Useful shell scripts and commands for this dotfiles repository, grouped by category.

The scripts are small utilities that each have their own requirements (check the script's
header comments or `--help` for details). Unless a script is documented below in detail,
assume it takes no arguments and prints its output to the terminal.

## Categories

- [Git & VCS](#git--vcs)
- [Networking / Proxies / VPN](#networking--proxies--vpn)
- [Web / Downloading / Media](#web--downloading--media)
- [Media / Audio / Video](#media--audio--video)
- [System / Hardware / Performance](#system--hardware--performance)
- [Process / Task Management](#process--task-management)
- [File / Filesystem Utilities](#file--filesystem-utilities)
- [Text / Markdown / Clipboard](#text--markdown--clipboard)
- [AI / LLM / Prompt Tools](#ai--llm--prompt-tools)
- [Programming / Compiler / Dev Tools](#programming--compiler--dev-tools)
- [Shell / Terminal Utilities](#shell--terminal-utilities)
- [Security / Privacy / Cleaning](#security--privacy--cleaning)
- [Package Management](#package-management)
- [Productivity / Personal](#productivity--personal)

---

## Git & VCS

### `gtask`

Runs `task` (taskwarrior) with the local `.taskrc`/`.task` found by walking up the
directory tree, so each git project can have its own task list.

### Other Git tools

- __git.sort__ — sort/partition a list of files by whether they're in the git repo or git-tracked (`--in-repo`, `--tracked`)
- __github.issues__ — fetch GitHub issues (full/body/comments) via `gh`, optionally restricted to explicit issue numbers
- __github.repos__ — shallow-clone or update+truncate many git repositories from a URL list
- __gitlab.pipelines.clean__ — delete old GitLab CI pipelines via the GitLab API (TOKEN/PROJECT env)
- __info.github.user__ — OSINT-style profile of a GitHub user: repos, contributor emails, avatar, followers, etc.
- __commit__ — suggest/format git commit messages from staged diffs, issue context and templates (see [AI / LLM](#ai--llm--prompt-tools))

## Networking / Proxies / VPN

### `net.tor.proxy`

Runs N instances of `tor` and load-balances them, either via iptables/nftables rules or
via a haproxy + privoxy HTTP proxy. See also `net.tor`, `net.tor.proxy.kill`, and
`net.tor.proxy.termux`.

### Other Networking tools

- __ipof__ — resolve a domain to its IPv4 address in a short format (`dig`)
- __isup__ — check if a website/domain/ip is up or not (Ping + HTTP + HTTPS)
- __myip__ — show your public IP and related info (ASN, country, city, timezone)
- __net__ — set up macvlan + network-namespace infrastructure to "skip the VPN" (source library for the `net.*` tools)
- __net.ns__ — manage the skip-VPN network namespaces: `run/exec [cmd]`, `add`, `clean [all]`
- __net.tor__ — run multiple tor (and privoxy) instances inside network namespaces with per-port data dirs
- __net.tor.proxy.kill__ — kill the load-balanced tor instances while preserving tor browser (9150) and tor.service (9050)
- __net.tor.proxy.termux__ — start the tor proxy load balancer on Termux (port 8090, 5 instances)
- __ns.emulator__ — run the Android emulator inside a network namespace with nftables NAT to bypass the host VPN
- __mtproto-load-balancer__ — standalone TCP load balancer (haproxy/nginx) for Telegram MTProto proxies
- __vpn-healthcheck__ — watch connectivity to a (censored) domain and restart Mikrotik IPSec peers over SSH when stuck
- __bashify__ — preprocess nftables scripts with embedded bash (`#$` lines) before running them through `nft`
- __check_domain__ — parallel HTTP status/size check of domains read from stdin (`curl` + GNU parallel)
- __cidr-merge.py__ — merge overlapping/adjacent CIDR blocks from stdin using `netaddr`
- __ip.replace__ — use nftables to DNAT/route one IPv4 address to another (add/list/disable/enable/remove)
- __ping.all__ — ping multiple hosts concurrently, colorizing latency and printing per-target results

## Web / Downloading / Media

### `download`

Download stuff off the internet using `yt-dlp` and `wget`, with quality presets,
playlists, audio-only, and a pending list.

### Temporary Browsers (`brave.tmp`, `chromium.tmp`, `edge.tmp`, `firefox.tmp`, and `vivaldi.tmp`)

These scripts launch a brand new process of the browser whose profile is newly created
in the `/tmp/...` directory (deleted afterwards). Optionally route through a tor proxy.
`firefox.copy` instead opens Firefox using a copy of an existing profile with a random
theme color.

### `transfer`

Share a file from the terminal by uploading it to transfer.sh and printing the download URL.

```bash
transfer /tmp/test.md
cat /tmp/test.md | transfer test.md
```

### Other Web tools

- __blogs__ — print random blog post links from indieblog.page
- __crawl__ — crawl web pages to a depth and extract links / save page contents
- __curl.browser__ — fetch a page's rendered DOM via headless Chromium
- __curl.bypass__ — curl with browser-like headers/TLS settings to bypass naive filtering
- __diff.urls__ — fetch two URLs and open them in a diff editor
- __favicon__ — fetch favicons as data-URI/base64/raw, optionally saved to a dir
- __filter-urls__ — filter URLs from stdin by topic keywords (images, news, security, social, etc.)
- __form.post__ — generate an HTML form that auto-submits a POST (also as a data URI)
- __http.curl__ — execute `.http` request files with curl; save responses by content-type
- __ix__ — paste files/stdin to ix.io pastebin (with delete/replace/read-count options)
- __janfada__ — print janfada.net's live visitor counter (delta vs. previous reading)
- __openin__ — open URLs in a browser, walking a fallback chain of installed browsers
- __q__ — search the web via `query` and open the results in the browser (`--post` converts to POST form)
- __query__ — generate search-URLs for dozens of engines/sites and categories
- __rss-finder__ — scrape URLs and print RSS/Atom feed links found in `<link>` tags
- __save-url__ — save web pages (PDF/HTML/mHTML/screenshots) using headless Chromium
- __telegram.links__ — fetch URLs posted in Telegram channels, with history of IDs/links
- __telewebion__ — search/play Iranian TV channels and episodes (prints m3u8 URLs, plays via mpv)
- __titles__ — fetch the `<title>` of many URLs in parallel
- __tv__ — search and play IPTV channels from a cached Free-TV playlist via mpv
- __url__ — dispatcher that sources user-defined URL helper functions from `urls/` or `~/.urls`
- __urlfix__ — fix/normalize URLs using the WHATWG URL parser (node) and print a chosen component
- __urls__ — extract absolute URLs from HTML stdin (a/link/img tags), resolving relative links with a base URL
- __virgool__ — search the Persian blogging platform Virgool for posts/tags (`--urls-only`)
- __youtube-links-of__ — reconstruct YouTube URLs from yt-dlp-style filenames (recursively)
- __yt.links__ — extract video (YouTube) links from filenames or media metadata via ffprobe+jq
- __yt.playlist__ — extract/download YouTube playlist links; also exports all playlists from a browser profile
- __zarebin__ — search the Zarebin search engine (pages, JSON output, URL-only, device type)

## Media / Audio / Video

### `buckle.pitched`

Pitch-shift the keyboard sound effects played by `bucklespring` with `sox`, then start
`buckle` with the modified sounds.

```bash
setsid buckle.pitched pitch 2000
```

### Other Media tools

- __extract-sub__ — extract one subtitle track (by language) from a video via ffmpeg
- __extract-subs__ — extract subtitles from every `.mkv` in the current directory
- __record.sh__ — FFmpeg screen recorder (x11grab + alsa audio)
- __srt2text__ — convert SRT subtitle files/stdin into cleaned plain text
- __stream-audio__ — stream system audio over TCP using a PulseAudio `module-simple-protocol-tcp` (start/stop)
- __subtitle__ — extract embedded or online subtitles (ffmpeg / yt-dlp) with language mapping, caching, save
- __vid.dur__ — print each video's duration and the combined total
- __vid.opt__ — optimize/resize video files with ffmpeg presets (categories), estimates, dry-run, trash-original
- __yt.playlist.subtitles__ — download subtitles for every video in a YouTube playlist

## System / Hardware / Performance

### `cpu`

Get CPU performance information and change the CPU profile.

```bash
cpu powersave
cpu performance
```

### `display-info`

Shows the **width**, **height**, and **diagonal** of the connected monitors in a table.

### `install-date`

Get the install date of the system.

### `should-restart`

Checks the libraries that have been updated but whose older versions are still loaded
into the system, giving you the idea of which applications need to be restarted or
whether the whole system needs to be restarted.

### Other System tools

- __actorun__ — daemon that runs commands on KDE Plasma activity changes (e.g., CPU governor, sig stop/cont of apps)
- __bluetooth.battery__ — print connected Bluetooth headphones' battery level (optionally follow)
- __cpu-limit__ — limit CPU usage of processes matching a name via `cpulimit`
- __cpu-priority__ — set nice priority (renice) of processes matching a name
- __keep-cpu-priority__ — loop keeping cpu-priority applied to the given processes every 5s
- __prime-intel__ — run a command with NVIDIA PRIME render offload forced to the Intel GPU
- __qdbus-info__ — dump D-Bus services, object paths, methods and properties (session or system bus)
- __zoom-minus__ / __zoom-plus__ — decrease/increase the GNOME magnifier zoom factor

## Process / Task Management

### `sig`

Send signals to a list of processes and all of their sub-processes.

`sig stop firefox` and then `sig cont firefox` for example pauses firefox and then continues firefox.

### Other Process tools

- __fkill__ — fzf-based multi-select process killer with configurable signal
- __open-files__ — find files currently opened by a process (optionally the active window)
- __sig-gui.py__ — PyQt6 GUI daemon for managing process signals (auto-toggle apps per KDE activity)
- __try__ — retry a command until it succeeds (optionally N times)
- __trynot__ — retry a command until it fails

## File / Filesystem Utilities

- __asfile__ — materialize stdin as a temp file and hand it to file-only commands (`--replace`, `--detach`, chmod)
- __done__ — toggle a `[done] ` prefix (plus `user.done` xattr) on a file
- __mv.prettify__ — prettify file/dir names (clean ads, artist-first for music), with dry-run, history, and reverse
- __paths__ — extract existing file paths (with `:line:col`) from text/stdin
- __recent-files__ — list recently modified/accessed files (git repo or stdin) with date filters and session grouping
- __snaputil__ — filesystem snapshot & undo utility (git-based snapshots of files/dirs with restore)

## Text / Markdown / Clipboard

### `strip-colors`

Remove colors from an input; use it like `software-that-prints-color | strip-colors`.

### Other Text tools

- __c.c__ / __c.p__ — clipboard **copy**/**paste** wrappers around the `clipboard` script
- __clipboard__ — copy/paste across Wayland/KDE/X11 clipboard tools
- __comment__ — add a comment line (begin/end, per-line) to stdin
- __dedup__ — deduplicate stdin lines via a persistent hash cache file
- __markdownify__ — wrap stdin/files in Markdown code blocks, inferring language from a git repo
- __strip-osc__ — strip OSC/CSI and other terminal escape sequences from stdin
- __zenity.show__ — render Markdown from stdin as plain text in a themed Zenity dialog

## AI / LLM / Prompt Tools

- __ai-find-skills__ — cross-registry (skills.sh + clawhub.ai + GitHub) AI agent-skill finder with security scan
- __ai-save__ — sync AI chat histories (gapgpt.app) to local Markdown and commit them to git
- __commit__ — suggest/format git commit messages from staged diffs, issue context and templates
- __content__ — fetch news/article URLs and extract clean, filtered article text (pandoc) — a content feed for prompts/AI
- __prompt__ — manage and run prompt files (`.sh/.txt/.md`) from XDG dirs; `prompt list`, stdin piped to prompt
- __prompt-compiler__ — autocomplete/compile prompts: slash-commands, `{{env}}`/`${var}` expansion, snippets
- __suggest__ — fetch search suggestions from Google, Zarebin, DuckDuckGo, Bing

## Programming / Compiler / Dev Tools

### `codeshell`

![CodeShell Demo](https://user-images.githubusercontent.com/12122474/149156571-dc48cde7-547a-4150-b5df-3cf6783b7976.png)

`codeshell` is a shell script that opens a `tmux` window, splits it in two, and opens the
editor in one pane, and runs the compiler/interpreter/make file in the other — and also
keeps re-running them when the files change.

It's a great tool for testing and benchmarking.

```bash
codeshell                      # Create a temp directory with the default template
codeshell -t=benchmark         # Use the benchmark template
codeshell -n=test.name         # Put the files in ~/codeshells/test.name instead of a temp directory or use that directory
codeshell -t=benchmark -n=test.name
```

CodeShell can also be run in Termux (Android).

Template directories are located in the `code-templates` directory from this repository.
You can use the directory names of those templates in the `-t=template_name` argument of this script.

You can see examples of `~/codeshells` directory in [my codeshells repository](https://github.com/the-moisrex/codeshells).

### `run`

A utility for C++ developers; finds the root of the git project, then finds the CMake
build directories, and lets you run CMake targets.

For example `run test-unicode` in **Web++** project will run the `test-unicode` cmake
target, and it doesn't matter in which sub-directory of webpp you're in.

### Other Dev tools

- __clang.deps__ — list a C++ file's dependencies (relative, deduped) via `clang++ -MM`
- __code.tmp__ — create a temp directory from a code template, copy files in, and open a shell there
- __compilers.sh__ — download and install GCC compilers from a mirror (wget/configure/make)
- __cpp.undeclared-identifiers__ — detect "use of undeclared identifier" errors via libclang diagnostics
- __cpp.undeclared-identifiers.sh__ — suggest missing headers for undeclared identifiers via clang-tidy
- __gdb.run__ — GDB wrapper that fuzzy-finds executables and gdb/python scripts, with `watch` restart via entr
- __gtest-case__ — find and print the source of Google Test cases (prefix/exact matching)
- __gtest-finder__ — locate gtest cases' files/lines from names or gtest output (`--failed`, `--names`)
- __ide.open__ — open `file:line:column` in a running IDE (VS Code, CLion, Neovim/nvr, QtCreator)
- __llvm.run__ — run clang/LLVM plugins with project flags from `.clang`/`.clangd` and header-TU handling
- __patch.fix__ — fix malformed unified diffs (esp. AI-generated) with fuzzy line matching; `-i` in-place
- __spp__ — parallel C/C++ function-source extractor via clang subprocess (Search C/C++)
- __spp.img__ — render `spp` output as an image via pygmentize + ImageMagick
- __spp.query__ — find C++ symbol declarations/definitions using `clang-query` + compile_commands.json
- __spp.sh__ — original bash `spp`: extract a function's source via clang-check AST tools
- __wg21__ — query wg21.link for C++ standards papers/issues (last, range, search, std, github, yaml, …)
- __whatwg-url-specs__ — browse/search the WHATWG URL spec (headings, definitions, algorithms) with cached spec

## Shell / Terminal Utilities

### `stopwatch`

A simple stopwatch, resumable from the last start time.

### Other Shell tools

- __arec__ — record a terminal session with asciinema (tmux-aware, project pre-text support)
- __editpipe__ — edit stdin in an interactive editor, write the result back to stdout
- __fzf1__ — shortcut for `fzf -f QUERY | head -1`
- __history-usage__ — analyze piped shell history and count command usage frequency (`--top N`)
- __play_pipe_sound__ — play a synthesized sound corresponding to a command's exit code (prompt helper)
- __runif__ — run a command in an interactive shell only if it exists in PATH
- __terminal-colors__ — print a 256-color foreground/background table

## Security / Privacy / Cleaning

### `clear-coredump`

Clears your logs and core dumps: rotates `journald` and purges systemd coredumps.

### `optimize`

- Clean the trash
- Update the system
- Discard unused blocks of the filesystem (`fstrim`)
- Clear caches and what not with `bleachbit`

You can run `optimize -tube; poweroff` to do all of the above and shutdown the system.

### Other Security tools

- __clean.privacy__ — redact personal info (home paths, usernames, hostnames, IPs, MACs, emails, phones) from text
- __cleanup__ — run category-based cleanup scripts (XDG dirs, trash) with dry-run, list, info, and colored logs
- __pass-gen__ — generate a username + password file entry under a directory
- __s3.ls__ — scan for publicly accessible S3 buckets matching a pattern and list their contents
- __slash-stego__ — encode/decode hidden binary messages as `/` and `\` in URLs (steganography)
- __vault__ — read/write/execute files in a gpg-encrypted vault directory (`edit`, `add`, `get`, `rm`, …)

## Package Management

### `pacman.sync`

Sync pacman packages (`/var/cache/pacman/pkg`) between two systems.

```bash
sudo pacman.sync from 192.168.1.2
pacman.sync to root@192.168.1.2
```

### `pacman.list.sortby.size`

List installed packages and sort them by size — a pacman utility that gives you an idea
which packages are big and useless.

### Other Package tools

- __npm.pack__ — pack npm packages in parallel, skipping existing tarballs; reads deps from package.json files
- __pacman-mirror-check__ — test and rank Arch mirrorlist mirrors by speed, sync recency, or country
- __pip-update__ — upgrade all outdated pip packages

## Productivity / Personal

### `welcomeback`

![image](https://user-images.githubusercontent.com/12122474/149163086-05ee270d-8820-4d07-a245-ec9f8fd99ab4.png)

It's just a welcome back message when it took you more than 24 hours to come back. You
can put it in your `.bashrc` or `.zshrc` file.

### Other Productivity tools

- __gtimew__ — run `timew` (timewarrior) with the local `.timewarrior` DB found by walking up directories
- __hacker-news__ — fetch Hacker News front-page links (multiple pages)
- __news__ — fetch news article URLs from many agencies (Iranian + international), with `list` of agencies
- __news.live__ — continuously fetch news links, dedup, and pop them up in a Zenity window
- __quote-of-the-day__ — fetch quotes/dad jokes/life advice from public APIs
- __save-speech__ — download a word's pronunciation audio (Google dictionary) and play it
- __sleep.until__ — sleep until a specified time (optionally timezone), with a countdown stopwatch
- __speech__ — text-to-speech via Google Translate's TTS endpoint
- __task-background.sh__ — write the current `task` list on an image and set it as the GNOME wallpaper
- __task-background-online.sh__ — download the Bing picture of the day, annotate with tasks, set as wallpaper (cron-safe)
- __token-sound__ — echo stdin and play a synthesized sound per token/word (streaming/a11y helper)
- __weather__ — show weather for a city via `wttr.in`

---

See the main [README](../README.md) for the rest of the repository.