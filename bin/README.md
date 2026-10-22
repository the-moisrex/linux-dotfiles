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

### `vpnswarm`

Runs every VPN listed in a swarm file as its own network namespace, so many
OpenVPN tunnels can be up at once while the host keeps its normal routing. Each
tunnel exposes a SOCKS5 + HTTP proxy (`socks5h://127.0.0.1:11001`), a
load-balancing proxy over all healthy tunnels (`127.0.0.1:1080`), and
`exec <name> cmd` to run a single command inside one tunnel. `route <name>`
optionally sends all host traffic through a chosen tunnel (nftables fwmark +
policy routing; `unroute` puts it back) — it is off unless asked for, switching
between tunnels is safe (`route` drops whatever was routed before it installs
its own rules, even when the state file is gone), and it refuses to install a
route it could not keep its own tunnel alive through: the VPN server's address
comes from the live socket or the line's `--config`/`--remote` `remote` entries,
and without one `route` stops and says so instead of quietly cutting the
connection (`--allow-no-server-exclusion` overrides, at your own risk). Every
failure prints the `unroute` that undoes it, and the route is recorded before it
goes in, so a half-finished one is still undone. While it is on IPv6 is killed
rather than leaked (`VPNSWARM_ROUTE_IPV6=pass` to let it out).

The swarm file is declarative — one line per VPN holding its name and the whole
`openvpn` command (a leading `--` pins a patched/openvpn3 binary); `add` writes
that line for you, quoting every word so a path with a space survives, and
`remove` takes it out again. A line may also read `name <program> args…` to run
any OpenVPN-compatible program, such as a launcher wrapper that does DPI evasion
(`any1 ./surfshark-connect.sh --ladder udp:2433,tcp:443 --`) — vpnswarm appends
its own options to the end, so a wrapper must hand what follows its own `--` to
openvpn. A tunnel's name comes from its `--config` file stem unless the
line names one (a line that spells out the binary, `openvpn --config x.ovpn`,
is named after that file too). Tunnels run in the host netns with a generated
`--up`/`--down`
script that moves the tun into its namespace and applies the address, routes and
DNS there: the address is read as a peer under `topology net30` and as
`local/prefix` under `topology subnet` (a netmask is never used as a peer), and
a full tunnel whose server pushed no gateway still gets a default route — at the
device itself — so a subnet-style VPN is not quietly limited to its own /24.
`supervise up` restarts dead tunnels (but not ones you stopped),
`status --check` probes each tunnel and records the verdict, and `prune` tears
everything down: it stops the supervisor and the tunnels, takes host routing
back before the tunnel it points at disappears, then sweeps what a lost state
directory would otherwise leave running — the namespaces (`vsw-*`), their
`/etc/netns` directories and the veth pairs — and says where the logs
(`logs/`) and the saved credentials (`creds/`) were kept, since neither is
deleted. Pass the swarm file with `-c`; without one the last used file is reused,
and a relative `-c` is made absolute before the root re-exec so it means the
same file there.

`start` brings the tunnels up one at a time and waits for each to become
healthy, printing what it is waiting for and where the log is (`--no-wait`
returns immediately instead) — a wrapper line is never waited for at all, since
it negotiates on its own clock and the supervisor is the right owner. The
supervisor tells a client that is still working from a stuck one by watching its
log: it waits as long as the launcher keeps writing, gives up after
`VPNSWARM_STALL_TIMEOUT` seconds of silence (60) or the hard cap
`VPNSWARM_UP_TIMEOUT` seconds (180) whatever happens, restarts a client that is
gone at once, and backs off further after each failure while resetting the
backoff as soon as a probe succeeds. `reload` compares both
the openvpn arguments of a line *and* its meta options (`--socks-port`,
`--no-proxy`, …), so editing either restarts that tunnel, only drops tunnels
that came from the file it is given, so tunnels belonging to another swarm file
are left alone, and never resurrects a tunnel you stopped; `supervise` is
likewise shared by all files.

The file is data, not a shell script: quotes and backslashes group words, but
nothing in it is expanded, globbed or executed. `status`, `logs`, `env`, `proxy`,
`exec`, `rr` and `stop` work from the recorded state alone — even after the swarm
file is moved or deleted — while `start`/`reload`/`list` say plainly that the file
is gone (`list` names the swarm file it is reading and tags every tunnel `up`,
`down`, `off` or `-` when it was never started). `env <name>` prints every
setting as an `export`, quoted so `eval "$(vpnswarm env x)"` puts the whole set
into the shell, and `proxy [name]` prints the proxy urls of one tunnel or of the
whole swarm — a line with `--no-proxy` says "no proxy (--no-proxy)" instead of an
empty port. `status` reports the client process and the tunnel device separately
(`CLIENT`/`TUN`, so a launcher that is still negotiating is visible as such) next
to the age of its last health verdict; `status --check` re-probes and records
that verdict, `logs <name|all>` takes `-f`/`--full` in any position, and `-q`
drops the banners. `--help` needs no root (it is also the only thing that runs
before the root check), and `help <command>` prints just that command's synopsis
and the paragraph about it. The state directory is root-only (0700, credentials
0600); the balancer's upstreams are the per-tunnel SOCKS proxies only, and
`VPNSWARM_STRATEGY` picks `roundrobin` (default), `random` or `sticky`
(one tunnel per destination host); `lb up` refuses to start a balancer when no
tunnel is healthy yet (it would have nothing to send through) and `lb restart`
warns the same before starting one anyway. A tunnel is healthy when a TCP
connect to `VPNSWARM_PROBE_HOST:VPNSWARM_PROBE_PORT` (1.1.1.1:443) succeeds from
inside its namespace — no DNS, no TLS — and, when its proxies are on, a SOCKS5
CONNECT through the published `127.0.0.1` port succeeds as well, so the verdict
covers the whole path a client actually takes. A proxy that died is restarted by the
supervisor (a pidfile is only believed when the process behind it really is that
proxy, which a rebuilt namespace otherwise makes easy to get wrong), and one that
cannot bind marks its tunnel down instead of looking alive; `status` shows how
long ago the verdict was taken.

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

### `browser.find`

Discover the browsers installed on the machine, their profiles, which profiles are
currently open (with PIDs), and the system default. Covers both Chromium-family
(Chrome, Chromium, Brave, Edge, Vivaldi, …) and Firefox-family (Firefox, Developer
Edition, Nightly) browsers. Output is human-readable by default and JSON with
`--json`, so other scripts can consume it.

```bash
browser.find list                  # installed browsers + profile counts
browser.find profiles [browser]    # every profile, with default/open flags
browser.find profiles --open-only  # only profiles of running browsers
browser.find open                  # running browsers with PIDs + profiles
browser.find default               # system default browser + profile
browser.find list --json           # machine-readable snapshot
```

### `cookies`

Extract cookies and saved logins for a site from the first matching browser/profile.
It resolves browsers through `browser.find` (Chromium-family first, then Firefox-family),
preferring currently open profiles and the system default. Databases locked by a running
browser are copied (with their `-wal`/`-shm` sidecars) before being read.

```bash
cookies grok.com                        # cookies for the site (first match)
cookies https://x.com --open-only       # only currently running browsers
cookies grok.com --browser firefox --profile default-3
cookies grok.com --kind both --all      # cookies + logins from every match
cookies grok.com --format header        # "name=value; ..." for curl/requests
cookies grok.com --format netscape      # cookies.txt for curl/yt-dlp/wget
cookies grok.com --list                 # which profiles have data, no secrets
```

Chromium cookies/logins are decrypted with the key from the login keyring
(`secret-tool`, falling back to the `peanuts` password-store key); Firefox cookies are
read directly. Saved logins come from Chromium's `Login Data` and from Firefox's
`logins.db` (the PBES2-wrapped `key4.db` key plus the `as-logins-key` JWE) or legacy
`logins.json`. A Firefox primary password is prompted for unless `--master-password`
is given. When a site matches nothing, a hint on stderr points at `browser.storage`
if the same site keeps a token in `localStorage` (disable with `--no-hint`).

### `browser.storage`

Read a site's web storage — `localStorage` (default) or `sessionStorage` — from the
first matching browser/profile, the counterpart to `cookies` for SPAs that keep
their login token in `localStorage` rather than the cookie jar. Like `cookies` it
resolves browsers through `browser.find`; storage is keyed by *origin*
(`scheme://host[:port]`), so a bare host matches across schemes/ports unless you
pin one. Firefox reads `storage/{default,permanent}/<origin>/ls/data.sqlite`
(Snappy values decoded in pure Python); Chromium reads its LevelDB store
(`Local Storage/leveldb`) with a small built-in LevelDB reader — SST tables and
write-ahead log — so no external `plyvel`/`snappy` is needed and locked stores are
still safe to read.

```bash
browser.storage linom.net                  # storage entries (first match)
browser.storage https://linom.net --exact  # exact origin, no subdomains
browser.storage linom.net --key Authorization --value-only
browser.storage linom.net --grep 'auth|token'
browser.storage linom.net --kind all --all # localStorage + sessionStorage, every match
browser.storage linom.net --format json    # machine-readable, values included
browser.storage --list                     # profiles with storage, no secrets
```

`--format` is `table` (default), `json` or `raw`; `--no-values` shows only key names
(`--format json` then reports `keys`/`count`), and `--value-only` prints just values.

### `browser.session`

Compose a complete authenticated HTTP session for one site — cookies (via `cookies`),
`localStorage`/`sessionStorage` (via `browser.storage`) and a matching `User-Agent`
(via `browser.find`) — so a script can replay the site's own API calls without a
browser. It is what the `ai.*` chat commands (below) use to sign in.

```bash
browser.session grok.com                      # json summary (best match)
browser.session https://x.com --format header # ready-to-paste request headers
browser.session grok.com --format cookie      # "name=value; ..." cookie header
browser.session grok.com --family firefox --open-only
browser.session grok.com --ua 'Mozilla/5.0 …' # override the User-Agent
browser.session grok.com --all                # every matching profile
```

`--format` is `json` (default), `cookie` or `header`.

### `browser.record`

**Development-only** recorder for reverse-engineering a site's own API: it drives a
real browser (Playwright + the system Chromium/Firefox) against the signed-in session
and saves every XHR/fetch/SSE request with its headers, body, response and — for
`text/event-stream` — the full streamed body, plus any WebSocket frames
(`websockets.json`). It is what the `ai.*` adapters were derived from; nothing here
runs at chat time.

```bash
browser.record https://chatgpt.com --session chatgpt.com --prompt "hi" \
               --wait 60 --idle 6 --out /tmp/rec
browser.record https://grok.com --session grok.com --prompt "say pong" --headful
browser.record https://example.com --all-requests   # include scripts/images too
```

`--js` evaluates JavaScript after load (before sending the prompt) and `--input` pins
the composer selector, otherwise autodetected.

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
- __ctl__ — smart systemctl wrapper: picks the user or system manager for the unit you name and only uses sudo for mutating system commands (`ctl --help`)
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
- __clipboard__ — byte-exact copy/paste/clear across Wayland (`wl-clipboard`), X11 (`xclip`/`xsel`) and KDE (Klipper); stays within one clipboard family per session (Wayland and X11 clipboards are different clipboards), reports failures via exit codes, adds `history --clear` (Klipper), and guards every backend with `CLIPBOARD_TIMEOUT` (default 3s)
- __comment__ — add a comment line (begin/end, per-line) to stdin
- __dedup__ — deduplicate stdin lines via a persistent hash cache file
- __markdownify__ — wrap stdin/files in Markdown code blocks, inferring language from a git repo
- __md.extract__ — pull blocks out of Markdown (stdin or files): fenced code, sections, GFM tables and frontmatter. Every block is parsed with its context — enclosing heading path plus the prose paragraph before and after — so selection is a filter chain: `--lang diff`, `--kind csv --before "monthly totals"`, `--heading Install`, `--under Details`, `--after "note"`, `--title parser`, `--grep PAT`. `--lang` compares families (`c++`/`cxx`/`hpp` → `cpp`, `bash`/`console` → `sh`, `patch` → `diff`, `py` → `python`), `--guess` sniffs untagged bodies, `--kind any` reaches every kind. `--list` numbers the rows of the current selection (index, kind, language, heading, label), so `-n N` (also `2-5`, `1,4`, `-1`, `--first`/`--last`) re-selects exactly that row when the same flags are used; `--pick` opens fzf, and `--one` fails unless exactly one block matches. Output is the body alone by default, so it pipes straight into `git apply`/`jq`/an editor; `--fence` re-emits the original fence, `--with-heading`/`--with-intro` prepend provenance, `--json`/`--jsonl` emit full records, and `-o FILE` or `--out-dir DIR` (named from `title=`, the heading slug or `<lang>-<index>`, with `--force`/`--dry-run`) writes matches to disk. Exit status is grep-like: `0` matched, `1` nothing, `2` usage error. Example: `md.extract --lang diff answer.md | git apply`, `md.extract --kind csv --before "monthly totals" report.md > totals.csv`.
- __strip-osc__ — strip OSC/CSI and other terminal escape sequences from stdin
- __zenity.show__ — render Markdown from stdin as plain text in a themed Zenity dialog

## AI / LLM / Prompt Tools

### `oc`

Run the [opencode](https://opencode.ai) AI coding agent in a container, in two
modes:

- **project mode** — when `$PWD` is a project (has a `Dockerfile`, or one can
  be generated from `CMakeLists.txt` / `package.json` / `Cargo.toml` / ...),
  `oc` runs opencode inside that project's dev container: it calls `pod name
  .base .oc [<profile>]` (add more recipes with `--with vscode,ssh`), so the
  agent sees the project's real toolchain at `/workspace`.
- **agents mode** — anywhere else it falls back to the `pods/agents` image.

Both mount `~/.gitconfig` and `~/.ssh` read-only (`--no-git` / `--no-ssh`),
forward set `*_API_KEY` variables, and share named profile volumes
`oc-profile-<name>`. Profiles are unseeded by default; `--seed` (one-shot,
recorded in the volume) copies the host's auth/config in.

```bash
oc                       # interactive TUI (project mode if in a project)
oc -p work --seed        # new profile, seeded from host auth/config
oc --with vscode,ssh     # project mode: code-server + sshd around opencode
oc run "fix the tests"   # one-shot headless prompt; flags after -- go to opencode
oc --agents              # force the pods/agents image even inside a project
```

The pod's web UI service is managed with `pods/stack start agents`
(<http://opencode.localhost>), see `pods/agents/README.md`.

- __oc-seed__ — apply/record an opencode profile's seed decision on the host
  (`--seed` / `--reseed` / `--print`, takes a volume name or directory); this is
  what `oc` runs for project-mode containers, same recorded rules as the agents
  entrypoint

### `ai.chatgpt` / `ai.grok` / `ai.dseek` / `ai.gap` / `ai.kimi`

Ask ChatGPT, Grok, DeepSeek, GapGPT or Kimi from the shell by replaying each site's own web
API with the session already in your browser — **no API key, no official API, and no
browser at run time**. Each command reads a prompt from its arguments or stdin,
streams the answer (reasoning and tool calls included) and renders Markdown on a TTY
(or with `--tty`, which treats a pipe as a terminal — `prompt`'s sinks use it).
It can also keep the conversation going: `-c`/`--continue` reopens your last
conversation with that provider and `-i`/`--interactive` keeps it open for more turns.
`-f`/`--file` uploads a local file and attaches it to the first message (repeatable).
A prompt above the provider's inline budget (ChatGPT 48 KiB, GapGPT 24 KiB,
Grok 64 KiB, DeepSeek/Kimi 256 KiB, in UTF-8 bytes) is written to a temporary
`.txt` and uploaded through that same path, with a short pointer message instead
of the full request; `--no-prompt-file` keeps it inline, `--prompt-file` forces
the file, and `--prompt-limit BYTES` overrides the budget. That is what keeps a
large `prompt .verify | ai.chatgpt` pipe from hitting the site's 413.

The five are thin wrappers over one shared engine (`bin/ai/`); each provider adapts
its site's transport:

- **ChatGPT** replays `/backend-api/f/conversation`, passing the sentinel gate
  (requirements token, proof-of-work, turnstile) and the conduit, then reads the
  answer from the SSE stream — or from `GET /backend-api/conversation/<id>` when the
  turn is handed off to the site's WebSocket.
- **Grok** drives the site's application WebSocket (`wss://grok.com/ws/mgw/`) with
  `session.create` / `response.create`, turning `response.chunk` frames into text,
  reasoning and tool calls.
- **DeepSeek** creates a chat session, solves the `DeepSeekHashV1` proof-of-work (in
  pure Python, parallelised across cores) and reads the site's fragment-patch stream.
- **GapGPT** (gapgpt.app) drives its WebSocket (`wss://gapgpt.app/ws/salam`), sending
  a `text_message` and diffing the `new_message` snapshots' `response` fragments
  (`text`, `reasoning`/`agent_turn`, `canvas`); it keeps the socket open between
  turns and falls back to `GET /api/v1/get_chat/token/<id>` if the socket goes quiet.
  Its `--model` takes the site's own ids (`A-GAP`, `A-GPT4O`, `A-GEMINI-3-PRO`, …) or
  friendly labels (`gap`, `gpt-4o`, `sonnet`, `deepseek-r1`, …).
- **Kimi** (www.kimi.ai) creates a chat with `POST /api/chat` and reads the
  `completion/stream` SSE events (`cmpl` carries the text); the server threads the
  transcript on the chat id, so a follow-up sends only the new message. Its 15-minute
  `localStorage` access token is renewed from the refresh token before a turn, uploads
  go to `/apiv2-files/file/upload` with the ids in the body's `refs`, and `--system` is
  folded into the first message because the site rejects a `system` role. A cut-short
  answer (the site stores it as `MESSAGE_STATUS_TRUNCATED`, its Continue button) is
  finished through the Connect `ContinueChat` stream and appended, so the text is whole.

```bash
ai.chatgpt "explain this stack trace"           # prompt as an argument
cat main.rs | ai.dseek                          # …or piped on stdin
ai.grok -m expert "compare mTLS and JWT"
ai.chatgpt -s "be terse" "what is 2+2?"         # system/custom instructions
ai.chatgpt --json "hi" | jq -r 'select(.kind=="text").text'
ai.dseek --show-thinking --copy "summarise this" # reasoning->stderr, copy answer
ai.dseek -i                                      # chat in a loop (a new conversation)
ai.chatgpt -c "and what about now?"              # continue the last conversation
ai.chatgpt -c -i                                 # resume it, then keep chatting
ai.dseek --new "start over"                      # ignore the saved conversation
ai.gap -m gpt-4o "compare mTLS and JWT"          # GapGPT, by friendly model label
ai.kimi "summarise this article"                 # Kimi, same engine
ai.chatgpt -f report.pdf "summarise this"        # upload + attach a file
ai.grok -f a.png -f b.txt "what do these show?"  # …more than one
prompt .verify | ai.chatgpt                      # oversized prompt -> text file
cat big.diff | ai.chatgpt --no-prompt-file       # force inline (site's limit)
ai.chatgpt --prompt-file "small but as a file"   # force the file path
```

### `ai.debate`

Ask two or more AIs the same question and let them check each other. The first
provider answers; its answer goes to the next provider as `Another AI said: …`
together with a verification request; each participant keeps its **own**
conversation, so later turns only carry the other AI's latest answer. That is
the two-tab "paste one answer into the other" loop, automated.

```bash
ai.debate grok dseek "is this migration safe?"       # two AIs, two rounds
cat design.md | ai.debate grok dseek                 # question on stdin
ai.debate grok dseek kimi --rounds 3 "review this"   # three AIs, round-robin
ai.debate grok dseek --until-agree "is it correct?"  # stop when both say AGREE
ai.debate chatgpt kimi --instruction "find the bug" "…"
ai.debate grok dseek --context all --copy "…"        # transcript on the clipboard
ai.debate grok dseek -m grok=expert -m dseek=deepseek_reasoner "…"
ai.debate grok dseek -c "now check the rollback plan"  # continue that panel
ai.debate grok dseek --new "fresh take on the same"    # forget saved chats
ai.debate grok dseek --print-prompt "…"              # offline dry run
```

The leading positionals are providers (`chatgpt`, `grok`, `deepseek`/`dseek`,
`gap`, `kimi`), the rest is the question. `--rounds N` (default 2) counts full
passes through the panel, so two providers and two rounds make four calls.
`--until-agree` appends a `VERDICT: AGREE|DISAGREE` request and stops after a
pass in which everyone still agrees (the answer must contain the line; a
missing verdict keeps the relay going until `--rounds`). `--instruction` / `--no-instruction`
override the verification request; `--context last|all` (or `--transcript`)
picks the last answer (default) or the labelled transcript for the sink context
and `--copy`. Sessions are loaded for every provider up front; a failed or empty
turn stops the relay and leaves the last complete answer in the context.

Every debate starts fresh chats. `-c/--continue` instead reopens each
participant's chat from the last debate between the same providers (stored under
`$XDG_STATE_HOME/ai/debate-<providers>.json`, keyed order-independently), so the
new question is a follow-up in those threads; `--new` forgets that state and
starts clean. The panel is saved after every successful turn, and a provider with
no saved chat simply starts one.

The same five providers (plus `.ai.debate`) are native `prompt` **sinks**, so
they can sit anywhere in a prompt chain: everything before the sink is sent as
context and the answer becomes the context for what follows. They render exactly
like a direct run — prompt passes `--tty` when its own stdout is a terminal —
while the chain text and clipboard come from a hidden plain-text side channel,
so no ANSI/Markdown frames leak into either.

```bash
prompt .note "review this" .files src/main.cpp .ai.dseek
prompt .note "list the risks" .files a.cpp .ai.dseek .note "rank them" .ai.grok
prompt .note "compare" .files old.cpp new.cpp .ai.dseek --with-context .note "summarise" .ai.kimi
prompt .note "review this" .files a.cpp .save review.md .ai.dseek
prompt .note "review this" .files a.cpp .ai.dseek --print-prompt   # offline dry run
prompt .note "is this migration safe?" .files db/schema.sql .ai.debate grok dseek
prompt .note "audit this" .files app.py .ai.debate grok dseek --rounds 3 --until-agree
prompt .note "now check the rollback" .ai.debate grok dseek -c   # continue those chats
prompt .note "compare" .files old.py new.py .ai.debate grok dseek --context all .save debate.md
```

Sink flags are shared by every sink and handled by the dispatcher:
`--on-error abort|continue|skip` (default `abort`; a failed source batch taints
the context and each later sink applies its own policy), `--with-context`
(append the answer instead of replacing), `--show-context` (print the source
segments too; default output is sink answers plus trailing segments no later
sink consumed) and `--head N`. Reasoning stays on stderr and never enters the
chain. `--print-prompt` prints exactly what the sink would send without opening
a session. Put instructions in a preceding `.note` or in `--system`; positional
prompt text is rejected because stdin carries the chain.

Sessions load through `browser.session` (the browser must have been logged in once);
`--browser`/`--profile`/`--family`/`--open-only`/`--exact` narrow the match and
`--print-session` shows what would be used. Tool calls are **display-only** — they are
never executed. Reasoning shows on a TTY by default (`--no-thinking` hides it); with
piped output the answer goes to stdout and reasoning/tools to stderr, and `--json`
emits one normalised event per line. `--model` selects a site model (`auto`/`gpt-*`/
`o*` for ChatGPT, `fast`/`expert`/`grok-*` for Grok, `deepseek_reasoner`/
`deepseek_chat` for DeepSeek, a site id or label like `A-GPT4O`/`gpt-4o` for GapGPT)
and `--timeout` bounds a turn.

`--file` uploads through each site's own endpoint (ChatGPT's
`/backend-api/files` create → pre-signed blob `PUT` → mark uploaded; Grok's
`/rest/app-chat/upload-file` base64 JSON; DeepSeek's `/api/v0/file/upload_file`
multipart behind its own proof-of-work; GapGPT's `/api/v1/upload_file` multipart;
Kimi's `/apiv2-files/file/upload` multipart) and references the returned id in
that turn's payload. The automatic oversized-prompt file uses the same endpoint,
so the model receives an ordinary text attachment. Each uploaded file is
announced as a `tool_call` event named `upload` (display-only, like the rest);
the switch to a prompt file is also reported on stderr (or as that `upload`
event with `--json`).

When a site still reports its own limit (`HTTP 413`, `token_limit`, a context
window message), the error is annotated with the prompt size and the remedy:
drop `--no-prompt-file`, lower `--prompt-limit`, or trim the prompt. Prompt
files are temporary and deleted once the turn has uploaded them.

`-i` needs a terminal on stdin (and rejects `--json`); it reads one prompt per line,
with `:q` to quit and `:new` to start a fresh conversation. Every run remembers its
conversation — id, model and a short transcript — under
`$XDG_STATE_HOME/ai/<provider>.json`, so `-c` continues it from a later process.
ChatGPT, DeepSeek and GapGPT resume it by id; Grok's socket lives only for one
process, so a resumed Grok conversation replays the stored transcript as context
instead.

Requires Python 3 with `curl_cffi` (or `httpx`), `rich`/`pygments` for the TTY
renderer and `websocket-client` for Grok and GapGPT; a free account's own rate limits
still apply.

- __ai-find-skills__ — cross-registry (skills.sh + clawhub.ai + GitHub) AI agent-skill finder with security scan
- __ai.save__ — local, git-backed Markdown store for AI conversations across ChatGPT, DeepSeek, Grok, GapGPT and Kimi. Uses the browser's signed-in session (via `browser.session`, no `auth.json`), writes one file per chat under `topics/<topic>/<YYYY-MM>/<date>_<title>.md` with JSON frontmatter and invisible per-message markers, and regenerates a committed `months/<YYYY-MM>/<topic>` symlink view. `sync` is incremental (list/summary then fetch only new or changed chats; `--full`, `--limit`, `--jobs`, `--retries`, `--retry-delay`, `--dry-run`, `--provider`, `--force`), categorises by ordered rules in `.ai/config.json` (topic patterns, code-block thresholds, GapGPT projects) and keeps manual categories/tags. `last` answers "what was the newest saved message?" by scanning frontmatter (current year plus older month dirs touched this month; `--all`, `--by saved`, `--role`, `--kind` (defaults to `text`), `--chat`, `--json`), `list`/`show` browse, and `add`/`reply` save prompts/responses from files or stdin (`add --chat` = `reply`, appends to the body and marks local edits so sync won't overwrite them). Also `categorize`, `tag`/`untag`, `recategorize`, `categories`, `rename-category`, `remove`, `reload`, `verify`, `migrate` (imports the old `ai-save` cache and markdown), `relink`, `providers` and `config`. Every mutation commits to git (`--no-commit` opts out); no database or index — Markdown/frontmatter is the source of truth. `--provider` takes the registry keys (`chatgpt`, `grok`, `deepseek`, `gap`, `kimi`) plus aliases (`gapgpt`/`gap-gpt` → `gap`, `dseek` → `deepseek`, `openai` → `chatgpt`); unknown names are rejected for `sync`, `list` and `last`. A stalled GapGPT project/page is retried in place and then skipped (reported in the sync JSON under `incomplete`) rather than failing the whole listing.
- __commit__ — suggest/format git commit messages from staged diffs, issue context and templates
- __content__ — fetch news/article URLs and extract clean, filtered article text (pandoc) — a content feed for prompts/AI
- __prompt__ — native prompt dispatcher (C++ source in `prompts/`, installed here): `prompt <name> [args]` with chained prompts (`.note`, `.cli`, `.files`; independent segments run in parallel and are appended in argv order), prompt **sinks** (`.ai.chatgpt`/`.ai.dseek`/`.ai.gap`/`.ai.grok`/`.ai.kimi` wait for the preceding chain, send it to the site, and continue with the answer; shared `--with-context`/`--show-context`/`--on-error`/`--head` flags and `--print-prompt`, plus the passthrough `.save [FILE|DIR]` sink that writes the same context to disk — deriving `tse-<symbol>.md`, the snapshot stem or the first `.files` stem when no target is given, treating an existing directory, trailing `/`, or extensionless target as a directory (created automatically) and a name with an extension as the exact file (`--dir`/`--file` override), preflighting the destination before any prompt runs, refusing overwrites unless `--force`, and leaving the chain unchanged for a later sink; `.flush [FILE|DIR]` is that same save followed by `.clear`, and `.clear` resets the accumulated context — including the derived filename hint and any source-batch failure taint — so one chain can run several independent sections while already-printed output and the clipboard are kept), `prompt list` (short descriptions), `prompt list-prompts` (full help), stdin piped to a prompt. It ships a fixed prompt set and never scans for prompt files
- __prompt-compiler__ — autocomplete/compile prompts: slash-commands, `{{env}}`/`${var}` expansion, snippets
- __tse.resolve__ — resolve Persian, finglish or fuzzy instrument names to TSETMC identifiers (symbol, ISIN, insCode, market/board) so every `tse.*` tool shares one lookup path: `tse.resolve فولاد`, `tse.resolve foolad`/`khodro` (Latin is reverse-transliterated through `bin/finglish` and matched against the forward finglish forms baked into the cache, including phonetic spellings like `mobin` for the literal key `mbin`), a company-name fragment, an ISIN, an insCode, a URL, or a composite `فولاد-IRO1FOLD0001` token (`-`, `:`, `(…)`, `@` and whitespace separators are all accepted). Exact → finglish → fuzzy (`rapidfuzz`, `difflib` fallback); candidates are ranked by market tier so a duplicated symbol picks the traded/main-market instrument (`فنفت` resolves to `بازار دوم` over `بازار پایه`) and only a real tie is reported as ambiguous. The active-instrument index comes from the bulk market watch, cached at `${XDG_CACHE_HOME:-~/.cache}/tse/instruments.json` for one week (`--refresh`/`--clear`/`--no-cache`/`--ttl`/`--cache`), with a live instrument-search backfill for anything it misses (`--offline` disables the network). `--json`/`--jsonl`/`--token`/`--best` for scripting, `--complete PREFIX` for shell completion (Latin input completes to `finglish-ISIN`, and a partial `-IRO1F` completes the ISIN), `--type`/`--market` filters. Every `tse*` tool resolves identifiers through it (`tse`, `tse.codal`, `tse.intraday`, `tse.snapshot`, `tse.find --watchlist`), and `shell/completions.fish` completes `tse`, `tse.codal`, `tse.intraday`, `tse.resolve` and `prompt tse|tse.codal|tse.intraday` arguments with the readable `SYMBOL-ISIN` token (`finglish-ISIN` for Latin input).
- __tse.codal__ — the only Codal.ir route, and the opt-in companion to `tse` (announcements, financial statements, fund portfolio reports, and ratios derived from the extracted statement rows): searches `search.codal.ir` and, because that host answers HTTP 429 until Codal's DDoS captcha is solved, runs the same flow the codal.ir web app uses (`begin` → captcha JPEG → OCR → `end`, then the `X-Captcha-Token` header, re-solved about every 3 minutes and after a repeat 429) with a cookie session, which Codal requires — without it searches silently return zero rows instead of failing. Statement tables are pulled from `www.codal.ir/Reports/Decision.aspx` pages, which need no captcha, and are spaced at least 4 seconds per host. Subcommands: `tse.codal search <symbol>` (`--letter-type`, `--category`, `--limit`, `--raw`), `tse.codal statements <symbol>`, `tse.codal snapshot <symbol> [--top N] [--fund] [--markdown]`, and `tse.codal cache path|list|clear`. Identifiers are resolved through `tse.resolve` (Persian/finglish/fuzzy names, ISIN, insCode, easytrader/tsetmc/codal URL, composite tokens), and `snapshot --markdown` renders the prompt-ready dataset used by `prompt tse.codal` (symbol header, Codal block, `codal.derived_ratios`). Requires Python 3.9+, `tesseract` with English traineddata (pacman: `tesseract tesseract-data-eng`, apt: `tesseract-ocr tesseract-ocr-eng`) and an Iranian IP. Codal's edge resets connections (ECONNRESET, errno 104) and tarpits requests at random, so every request — captcha `begin`/`Captcha`/`end`, searches and report pages — is retried with jittered backoff inside a wall-clock budget (`CODAL_RETRIES` attempts, default 5; `CODAL_TIMEOUT` per attempt, 30s; `CODAL_DEADLINE` per request, 60s), on fresh sockets but the same cookie session, with 5xx/408/425 treated as retryable and a per-host cooldown after a failure. Successful artifacts are cached under `${XDG_CACHE_HOME:-~/.cache}/tse/codal` (`CODAL_CACHE_DIR` overrides): an entry younger than `CODAL_MAX_AGE` (default 6h, `--max-age DURATION`) is served from the cache, an older one is refetched but still used as a fallback when the live fetch keeps failing; those results carry a `cache` note (`cached`, `fetched_at` Jalali, `age`, `stale`, `live_error`) and the Markdown prints the fetch time above its Codal block. `--refresh` forces a live fetch, `--cache-only` never touches the network. Dates are Jalali (Shamsi): the announcements'/statements' `published_at`/`sent_at` come from Codal already Jalali (Persian digits), and the snapshot's `retrieved_at` is a Jalali stamp.
- __tse__ — fetch TSETMC quotes, order book, individual/legal (retail/institutional) flows, benchmark indexes, market context (free-market FX rates, gold & coin prices from Tasnim News, World Bank annual CPI inflation and GDP growth, and whole-market breadth — the exchange's own per-instrument daily rows for the newest published session and the five sessions before it, ~1,900–2,300 instruments per day with advancers/decliners/unchanged, the median and average move, and, when a symbol is being analysed, where it sits in that distribution, falling back to a tracked large-cap proxy only when those rows are unavailable), per-instrument statistics (the 3- and 12-month averages of traded value, volume and daily trade count, the last session's value/volume/trades and its VWAP both with and without حجم مبنا, company value and its market rank, and the 3-month per-capita real/legal buy/sell volumes and trader counts), history, derived technicals (returns over 1w/1m/3m/6m/1y, realised volatility, ATR(14), MA20/50/200 and the distance from them, RSI(14), 52-week position, max drawdown, median overnight gap), historical corrections (structurally enumerated mid-trend correction and sideways-consolidation episodes with their حقیقی/حقوقی net flow, flow coverage and measured outcomes, plus an unconditioned baseline — the precomputed table `.tse.analyze.history` reads instead of re-summing the CSV), the last completed session's opening-auction and close order books (the same `BestLimits` level stream `tse.intraday` replays, validated against the session and retried across CDN edges), fundamentals (EPS/P-E, 52-week range, average volume, price limits, fund NAV) for `prompt tse` and `prompt tse.intraday` (the data-free analyst lens `.tse.analyze` chains after `tse`). Codal filings are deliberately not fetched here: use `tse.codal`/`prompt tse.codal` and chain it when they matter. Requires Python 3.9+ and an Iranian IP. Run `bin/tse all <symbol>` for JSON (add `--without book,statistics,breadth,corrections` to skip a section), or `prompt .tse <symbol> .tse.analyze` for an AI prompt. A symbol may be a Persian ticker, a finglish name (`foolad`, `khodro`), a fuzzy/company-name fragment, an ISIN (e.g. `IRT1DARA0001`), a TSETMC insCode, an easytrader/tsetmc/codal URL, or a composite `فولاد-IRO1FOLD0001` token — all resolved through `tse.resolve` (cached index, finglish and fuzzy matching, with ambiguous symbols ranked by market tier); `prompt tse`, `prompt tse.codal` and `prompt tse.intraday` borrow a neighbouring `.tse*` argument in a chain, then fall back to the clipboard, when no symbol is given. Full unadjusted daily history (potentially a large output) is the default — pass `--days N` to limit it. Every traded history row also carries the daily individual/legal (حقیقی/حقوقی) breakdown — `individual_buy_shares` … `legal_sellers` buy/sell volume, the rial values that go with them (`individual_buy_value_rial` … `legal_net_value_rial`; buy and sell each sum to the session's traded value) and trader counts, empty where the source has no record (the series starts 2018-11-26, and a few later sessions are missing upstream). Fetches run concurrently across the independent TSETMC sources; unavailable sources are marked as missing. Codal filings (`tse.codal`, cached under `${XDG_CACHE_HOME:-~/.cache}/tse/codal` for 6h unless `--refresh`) are a separate opt-in dataset, and old `.stock` snapshots that already contain a Codal block still render it. Dates and timestamps in the output are Jalali (Shamsi), e.g. `1405/07/26 14:45:26` — the quote's `trade_date`, the `date` column of the daily history and corporate-action events, `nav_as_of`, and `fetched_at`; clock-only fields (`trade_time`, `nav_as_of_time`, index `time`) stay `HH:MM:SS`.
- __tse.find__ — screen the whole TSETMC market (~3,900 instruments) against flag-defined filters: one bulk market-watch request supplies quotes, P/E, EPS, volume/value, trade count, bid/ask spread, daily price limits, sector/board codes and market cap, and filtering, sorting and ranking happen locally — no per-symbol crawling. Filters that need extra data enrich only the survivors, in parallel and capped by `--max-enrich`: instrument info (`--ps-*`, `--sector-pe-*`, `--avg5d-*`, `--near-52w-high/low`, `--free-float-min`), daily history (`--rsi-*`, `--above-sma/--below-sma`, `--cross/--cross-below`, `--macd`, `--streak-*`, `--vol-surge`) and the 5-level order book (`--book`, `--bid-wall-min`, `--ask-wall-min`, `--imbalance`, from a second bulk request). Universe/price/valuation/liquidity groups: `--type`, `--market`, `--board`, `--sector`, `--match`, `--watchlist`, `--traded/--untraded`, `--states`, `--price-*`, `--chg-*`, `--up/--down`, `--at-ceiling/--at-floor`, `--near-limit`, `--pe-*`, `--eps-*`, `--profitable/--loss-making`, `--mcap-*`, `--vol-*`, `--val-*`, `--trades-*`, `--spread-*`. Output is table (default), json, jsonl (one object per line, fzf-friendly), csv, markdown or `tse` (a `.tse` snapshot with `meta.args` embedded so the screen can be re-run) with `--columns`, `--sort`, `--limit`, `--asc` and an optional `--stats` summary; amounts take `k/m/b/t/p` suffixes (`10b` = 10,000,000,000 rial, `1t` = 1,000,000,000,000), percentages are plain numbers, and an Iranian IP is required. Requires Python 3.9+. FILE arguments ending in `.tse`/`.tse.args` load their embedded screen arguments first (command-line flags override them). Pair it with `prompt tse.find "<request>"`: chat mode (default) emits one ready-to-run `tse.find` command, `--mode agentic` runs it and relaxes filters one at a time until the screen matches (`--max-iter N`, `--dry-run`), and passing a `.tse` file, `tse.find` flags or piped `tse.find` output switches to review mode (rank the matches with their numbers, suggest `prompt .tse … .tse.analyze`/`prompt tse.intraday` deep dives).
- __tse.snapshot__ — fetch full `tse all` data for screened symbols and save each as a dated `.stock` file (pretty JSON: `meta` + `data`, named `YYYYMMDD_<symbol>_<ISIN>.stock`, where `YYYYMMDD` is the Jalali date); reads `tse.find` output (json/jsonl) or plain symbol lines from stdin — e.g. `tse.find ... --format jsonl | fzf --multi | tse.snapshot stocks/`. Fetches run in parallel (`--jobs N`, default 4; each fetch is one `tse all` process with `--days/--full`, `--unadjusted` passed through), files already saved today are skipped unless `--force`, `--note TEXT` is embedded in every `meta`, and per-symbol progress plus a `saved/skipped/failed` summary go to stderr (failures don't stop the batch). Requires Python 3.9+; analyse the result with `prompt .tse <file>.stock .tse.analyze`; fetch Codal filings separately with `tse.codal snapshot <symbol> --markdown` (cached, stale >24h warning).
- __tse.intraday__ — intraday (per-minute) OHLCV bars for one TSETMC instrument, for day-to-day charting: `tse.intraday فولاد`, `--days N` (the last N sessions, capped at 30; the live session counts as the most recent), `--date YYYY/MM/DD`, or `--from`/`--to`. TSETMC publishes no historical intraday aggregate, so every session — including the live one — is rebuilt from the per-trade tape (`Trade/GetTradeHistory` for completed days, `Trade/GetTrade` for the live session; canceled rows dropped, rows deduplicated by trade number, trades sorted by time then trade number, aggregated locally into N-minute bars); only if the live tape is empty does it fall back to TSETMC's native 2-minute bars (`Trade/GetTradeIntraDay`, marked `source: live`). Consecutive bars with identical OHLC collapse into one `from`..`to` row — but only once a run grows past `--collapse-threshold` (default 120 rows), so a small live or partial day keeps every minute while a full session or a locked limit day is condensed; `--collapse auto|on|off` overrides the rule and `--no-collapse` is an alias for `off`. Collapsed rows carry a trailing `bars` column counting the bars they swallowed. `--interval auto` (the default) starts at 1 minute and coarsens through 2/3/5/6/10/15/30/60 until the collapsed total fits `--max-rows` (default 2000), reporting the chosen interval; `--interval M` forces a size. The live session additionally prints the real-time `ClosingPriceInfo` quote (last price/time, running trades and volume, day range, running قیمت پایانی) and how far the lagging `GetTrade` tape trails it, since TSETMC's live tape is served from CDN replicas that can sit minutes behind. Every bar carries `vwap_rial` — its own volume-weighted average price, computed from the tape (TSETMC's 2-minute fallback publishes no turnover, so that cell stays empty) — and every completed session an `Official:` line with TSETMC's daily row: قیمت پایانی (`pClosing`, the day's VWAP capped by حجم مبنا) and its timestamp, the last trade with its change against the previous close, first/range, and the trade, volume and value totals. The same tape also yields each session's own `Session:` line (the high and low with their times, the close's location in the day's range, the share of volume at or above the tape VWAP, the gap against the previous close), a `Structure:` line (the 5/15/30-minute opening ranges and the market-profile value area — POC, the 70% band and its width in price levels) and `Largest prints:` (the three biggest trades by rial value); today's static price limits are printed in the header, with the live price's distance from them. Each session also carries its daily حقیقی/حقوقی breakdown (`individual_buy_shares` … `legal_sellers` in shares, plus the matching rial values and trader counts); the output states up front what TSETMC does not publish at all — per-minute (or per-bar) حقیقی/حقوقی, closing-auction detail (قیمت پایانی is a volume-weighted average, not an auction print) and per-instrument NAV history (only the current NAV is exposed). `--depth` adds the order book to every bar — best bid/ask with their sizes, the spread, the cumulative 5-level bid/ask depth and the imbalance (`depth_imbalance_pct`), sampled at the end of each row's span — and `--depth-ladder` prints the whole ladder at each row boundary (`time,level,bid_rial,bid_shares,bid_orders,bid_cum_shares,ask_…`) with running cumulative depth, its `--max-rows` budget split across the five levels. TSETMC publishes 5 levels and no more, from a per-level update stream (`BestLimits/{insCode}/{date}`, ~440 KB gzipped for a busy day) that appears only after the close and reaches back roughly two months, so the live session shows just its current book (`BestLimits/{insCode}`); the stream starts pre-open, so the 09:00 row carries the opening-auction book, and post-12:30 updates are dropped because the exchange keeps replacing levels individually after the close. Beyond the bars, a run carries an extras registry: `book` prints an end-of-session order-book summary twice — the opening auction sampled at the first bar's end and the close at the last trade, each listing best bid/ask with their sizes and exchange order counts (a saturated count prints as `>=9,999`), the spread, the 5-level depth and the imbalance; `orders` controls those order counts and the implied average order size; `opening` controls the auction line; `stats` adds a `Context:` line with the instrument's 3-month averages of volume, trades and real buyers plus company value and rank beside the latest published session's numbers; `index` adds a `Market:` line from the live market overview (TEDPIX and equal-weighted change, market trade/volume/value totals, market state); `breadth` adds a per-session `Breadth (date):` line from the whole market's own daily rows (advancers/decliners/unchanged, the median move, and this symbol's percentile); and `flow` prints a `Flow (estimated):` line and, with `--flow`, adds `buy_initiated_shares`, `sell_initiated_shares`, `net_shares`, `cum_net_shares` (the running cumulative delta) and `buy_pct` columns to every bar — each trade classified against the best bid/ask at its moment (buyer-initiated at or above the ask, seller-initiated at or below the bid, an inside-the-spread print to the side of the midpoint), with the unclassified remainder reported rather than guessed, since TSETMC publishes no aggressor flag. Defaults: `book`, `orders`, `opening` and `stats` on; `index` only when a live session is selected; `breadth` only up to five published sessions (each costs ~175 KB); `flow` off. `--with NAME[,NAME…]`, `--without NAME[,NAME…]` (accepts `all`) and `--only NAME[,NAME…]` override that per item, `--extras` prints the registry with its defaults, current state and cost, and `--depth`/`--depth-ladder`/`--flow` turn the book on. An extra that cannot answer is dropped with a printed reason (`Skipped extras: …`) and recorded in the JSON `extras` provenance (`used`/`skipped`). The order-book stream is validated against the session it will be replayed on — a partial stream that starts after trading or stops early (the endpoint resolves to several CDN edges that can disagree while a day is still being published) is refetched on a fresh connection and, if it is still incomplete, refused with a note rather than mislabelled as the close. `--ticks` prints the raw tape, `--profile` a price-level volume profile, `--canceled` keeps canceled rows, `--no-live` skips the current session, `--json` prints JSON instead of Markdown. Prices and traded value are in rial, volume in shares, times/dates are Jalali, and the CDN's gzip keeps a busy day to ~0.5 MB on the wire. Same identifier forms as `tse`; sessions are fetched in parallel with retries and a failed session is listed under `errors` without failing the run. Requires Python 3.9+ and an Iranian IP; no cache.
- __suggest__ — fetch search suggestions from Google, Zarebin, DuckDuckGo, Bing

## Programming / Compiler / Dev Tools

### `pod`

Project dev containers (the compose stack manager for service stacks lives at
`pods/stack`). Recipes in `pods/recipes/` print a Dockerfile each; `pod` builds
them as a chain (`base` = the project's own `Dockerfile`, or one generated by
`dockerfile.gen` for your host OS; bundled: `dotfiles`, `oc`, `vscode`, `ssh`) and keeps
one persistent labeled container per workspace — the project directory by
default, `--copy` (rsync into `~/wt`), `--worktree`, or a `git worktree`
(`pod wt NAME`).

```bash
pod                       # project image (.base) + shell in its container
pod .base .vscode .ssh    # selection: project image + code-server + sshd
pod .base .oc work        # opencode with profile "work" (what oc runs)
pod .base .dotfiles       # RO /dotfiles + mapped core pkgs; configs at creation
pod build .base .oc       # images only; --as NAME names the dish yourself
pod recipes               # list recipes with ports/services/volumes
pod recipes --porcelain   # name<TAB>description (drives fish completions)
pod info; pod services    # container labels / declared services
pod service code-server   # start a service, print its hint (the URL)
pod service setup --fg    # dotfiles: rerun install.sh (auto-ran at creation)
pod exec cmake --build build --target tests
pod shell base-vscode     # attach to a specific dish
pod ide                   # print the VS Code dev-container attach URI
pod list; pod down; pod rm; pod prune --images   # lifecycle
pod down all              # stop every managed dev container (all projects)
pod --pod devel .base     # optional: share one pod (shared netns)
pod --verbose build .base # keep build logs + debug detail (default erases them)
```

The **selection language**: `.recipe [opts ...]` tokens chain in order, bare
words are existing images to start FROM, no selection = `.base`; the selection
distills into a **dish** name (image tag `localhost/pod-<project>:<dish>` and
container identity `pod-<workspace>[-<nametag>]-<dish>`). Containers are
recreated automatically when their image or mounts change. See `pod --help`
(CONCEPTS) for the full model.

Containers carry `pod.*` labels (dish, selection, image id, ports, services,
hints), publish recipe ports on `127.0.0.1`, and mount declared volumes/env.
Build logs stream while running and are erased on success (`✓` timing
summaries remain); `--verbose` keeps them. See `pods/recipes/README.md` for
the recipe contract.

### `dockerfile.gen`

Generate a Dockerfile for a project matching the host OS (reads
`/etc/os-release`), auto-detecting the toolchain from its files (CMake+Qt →
`archlinux:base-devel` with qt packages, `package.json` → nodejs, Go, Rust,
Python, or a generic base). Used by `pod`'s `base` recipe when the project has
no own `Dockerfile`; also runnable standalone (`--print`, `--info`, `-o DIR`,
`--force`).

```bash
dockerfile.gen            # writes $TMPDIR/dockerfile.gen/<slug>/Dockerfile
dockerfile.gen --print .   # print to stdout (what the base recipe uses)
```

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