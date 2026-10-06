# agents pod — AI agents in podman

Containerized AI coding agents. Currently one service: **opencode**
(<https://opencode.ai>), available both as a long-running web UI and as an
interactive TUI (`oc`) that runs against any directory on your machine.

## Quick start

```bash
# 1. build the image (also happens automatically on first start)
podman-compose -f pods/agents/compose.yaml build

# 2. start the service (or: pods/stack start agents)
pods/stack start agents

# 3. use it
xdg-open http://localhost:4096          # web UI, works without the router pod
xdg-open http://opencode.localhost      # web UI via traefik (router pod must run)
opencode attach http://opencode.localhost   # terminal TUI attached to that server
```

Optional: copy `.env.example` to `.env` and set `OPENCODE_SERVER_PASSWORD` to
protect the web UI with HTTP basic auth (user: `opencode`). Without it the
server is unauthenticated — it is only published on `127.0.0.1` and reachable
through traefik, but set a password if that is not enough.

> `opencode.localhost` also needs the router pod, and traefik mounts the podman
> API socket: if `/run/user/1000/podman/podman.sock` is missing, run
> `systemctl --user start podman.socket` first.

## Interactive TUI (`oc`)

`bin/oc` runs opencode in a container with your current directory mounted at
`/workspace`. Two modes: inside a **project** (own `Dockerfile`, or one
`dockerfile.gen` can generate) it reuses that project's dev container via
`bin/pod` (fragment `oc`, extra fragments with `--with vscode,ssh`); anywhere
else it falls back to this `pods/agents` image:

```bash
oc                          # TUI in $PWD (fresh, unseeded profile)
oc -p work --seed           # new profile, seeded from the host's auth/config
oc --with vscode,ssh        # project mode: add code-server + sshd
oc --agents                 # force the agents image even inside a project
oc -- --version             # everything after -- is passed to opencode
oc -b -- -s <session-id>    # rebuild image first, resume a session
```

It mounts `~/.gitconfig` and `~/.ssh` read-only (`--no-git` / `--no-ssh` to
skip) and forwards `*_API_KEY` variables that are set in your environment.
Project-mode containers skip this entrypoint: seeding is applied host-side by
`bin/oc-seed` with the same recorded rules.

## Profiles (multiple auths, no re-authing)

State lives in named volumes `oc-profile-<name>`, mounted at `/profile`
(`XDG_CONFIG_HOME=/profile/config`, `XDG_DATA_HOME=/profile/data`):

| Who | Profile |
|---|---|
| web service | `oc-profile-${OC_PROFILE:-default}` (seeds, `OC_SEED: "1"` in compose) |
| `oc` | `oc-profile-$profile` (`-p` / `$OC_PROFILE`, default `default`) — **unseeded by default** |

Seeding copies `~/.local/share/opencode/auth.json` plus `opencode.jsonc` /
`package.json` / `themes` from `~/.config/opencode`, and rewrites
`http://localhost:` → `http://host.containers.internal:` in the copied config
(inside a container `localhost` is the container itself).

Every profile records its seed decision once in `<volume>/.profile`:

```
seeded=yes|no        host config has (not) been applied
seeded_auth=...      host auth.json has (not) been copied
seeded_config=...    host opencode.jsonc has (not) been copied
seeded_at=<utc ts>
```

Rules that keep profiles consistent:

- Seeding is **opt-in and one-shot**: whatever the first start decides is
  recorded, so later runs with different flags can't produce a half-seeded
  profile (no "auth from host but stub config").
- `oc` never seeds unless you pass `--seed`; the web service opts in with
  `OC_SEED: "1"` in `compose.yaml`.
- `oc --seed` fills only what the marker says is missing and never overwrites
  a real (non-trivial) config; `oc --reseed` overwrites auth + config from the
  host and keeps sessions.
- Profiles created before markers existed get their state inferred on their
  next start; use `--seed` (or `--reseed`) if the inference is not what you want.
- Inspect: `oc-seed --print oc-profile-<name>` (or `cat "$(podman volume inspect oc-profile-one --format '{{.Mountpoint}}')/.profile"`)
- Start over: `podman volume rm oc-profile-work`

> Do not run the web service and `oc` on the **same** profile at the same time —
> two opencode servers would share one session database. Use a separate profile
> per concurrent instance.

## Networking

- The web service joins `router_frontend`, the same network traefik uses: it has
  internet egress (needed for LLM APIs) and is routed at `Host(opencode.localhost)`
  (`traefik.docker.network=router_frontend`).
- It is additionally published on `127.0.0.1:${OC_PORT:-4096}` so it works with
  the router pod stopped.
- `oc` uses the default podman network (also has internet).
- `host.containers.internal` points at the host, but only services listening on
  `0.0.0.0`/LAN are reachable — host servers bound to `127.0.0.1` (LM Studio,
  local proxies, …) must widen their bind address to be used from a container.
- Containers run with `--userns=keep-id`, so files the agent writes into mounted
  projects are owned by your user, not `101000`.

## Configuration knobs

Environment variables (shell or `.env` in this directory):

| Variable | Default | Meaning |
|---|---|---|
| `OC_PROFILE` | `default` | profile volume used by the web service |
| `OC_PORT` | `4096` | host port (bound to `127.0.0.1`) |
| `OC_HOST` | `opencode.localhost` | traefik `Host()` rule |
| `OC_WORKSPACE` | `../../../Projects` | directory served as the web workspace |
| `OPENCODE_SERVER_PASSWORD` | empty | HTTP basic auth for the web UI |

A second, parallel web instance:

```bash
OC_PROFILE=work OC_PORT=4097 OC_HOST=opencode-work.localhost \
  podman-compose -p agents-work -f pods/agents/compose.yaml up -d
```

## Updating

`pods/stack update agents` only restarts (it must not pull the local image), so
after changing the Dockerfile:

```bash
podman-compose -f pods/agents/compose.yaml build
pods/stack restart agents
```

Bump `OPENCODE_VERSION` in `opencode/Dockerfile` to pin a different release.

## Adding more agents

New agents are sibling directories/compose services in this pod (claude-code,
codex, …): an image built the same way, an `oc-profile-*` volume for state, and
optionally a small `bin/` launcher following the `oc` pattern.
