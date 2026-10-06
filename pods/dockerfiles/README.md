# Dev-container fragments

Fragment Dockerfiles layered on top of a project base image by `bin/pod`
(project dev containers). Each file is a **standalone Dockerfile** that starts
with `ARG BASE` / `FROM ${BASE}` — `pod` builds the chain sequentially and
substitutes `BASE` with the previous image:

```
project base (Dockerfile or dockerfile.gen output)
  └─ localhost/pod-<project>:base
       └─ ...:oc          (fragment "oc")
            └─ ...:oc-ssh     (fragments "oc" then "ssh")
```

No textual merging happens: layers simply stack, one `podman build` per step
(cached, and skipped silently when `pod` is called with `--quiet`, e.g. by
`bin/oc`; failures still print the full log).

## Bundled fragments

| file | adds | ports / services |
|---|---|---|
| `oc.Dockerfile` | opencode (pinned) + `oc` wrapper | `# POD_PROFILE 1` |
| `vscode.Dockerfile` | code-server (opens `/workspace`, trust off, language-aware extensions) | port 8080, service `code-server` |
| `ssh.Dockerfile` | OpenSSH server | port 2222→container 22, service `sshd` |

## Header contract (`bin/pod` parses these comment lines)

```dockerfile
# POD_PORT <P>            # 1-part: 127.0.0.1:P:P (same port in and out)
# POD_PORT <H:C>          # 2-part: 127.0.0.1:H:C (host:container)
# POD_PORT <ip:H:C>       # 3-part: used as-is
# POD_PROFILE 1           # mount volume oc-profile-<name> at /profile and set
                          #   XDG_CONFIG_HOME=/profile/config, XDG_DATA_HOME=...,
                          #   XDG_CACHE_HOME=..., XDG_STATE_HOME=...
# POD_BUILD_ARG NAME      # forward NAME at build; SSH_PUBKEY is special-cased:
                          #   filled from env SSH_PUBKEY/POD_SSH_PUBKEY or
                          #   ~/.ssh/*.pub (warns when neither exists)
# POD_SERVICE name=cmd    # "pod service name" runs cmd in the container
                          #   (detached; use --fg for foreground)
```

Notes:

- **Ports are fixed at `podman run`**: publish via `# POD_PORT` or `-p`. With
  `--pod NAME` the whole netns is shared, so the pod's ports are fixed when the
  pod is *created* (first container) — later joiners get a warning instead.
- Services must start with an **absolute path** if the program demands it
  (Debian's sshd refuses `argv[0]` without one): `name=/usr/sbin/sshd -D -e`.
- The host's `~/.ssh` is mounted read-only at `/root/.ssh` by default, which
  *shadows* a baked `/root/.ssh/authorized_keys` — the ssh fragment therefore
  points sshd at `AuthorizedKeysFile /etc/ssh/authorized_keys/%u` (baked at
  build, unaffected by the mount).
- The `vscode` fragment's service runs `/usr/local/bin/vscode.start`, not
  code-server directly: the wrapper detects the languages used in `/workspace`
  (C/C++, Bash, `.editorconfig` — one row per language in its `LANGS` table),
  installs the matching Open VSX extensions plus helper packages (clangd,
  shellcheck) and only then launches code-server on `/workspace` with workspace
  trust disabled. The first start needs network and can take a minute; progress
  lands in `/var/log/vscode.start.log`.
- Bases must be bash-capable with `curl`/`git` (dockerfile.gen bases qualify);
  the bundled fragments install the rest themselves for pacman/apt/dnf.

## Adding a fragment

1. Copy an existing file; keep `ARG BASE` / `FROM ${BASE}` and `USER root`.
2. Declare the `# POD_*` headers above.
3. Make the `RUN` steps idempotent-ish and distro-agnostic (probe `pacman` /
   `apt-get` / `dnf`) unless you only ever use one base.
4. Use it: `pod up <name>`, `pod up <name>,<other>`, `--with <name>,<other>`,
   or `oc --with <name>`.

Fragments live here because this repo is the source of truth for both the
images and the tooling that builds them.
