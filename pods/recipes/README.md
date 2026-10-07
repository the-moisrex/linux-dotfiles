# Dev-container recipes

Recipes are **executable scripts** that print a complete Dockerfile to stdout;
`bin/pod` runs them, parses their `# POD_*` headers, and builds the results as
a chain (project image first, each recipe layered on top):

```
pod .base .oc work

  .base     -> localhost/pod-<project>:base          (project Dockerfile, or
                                                      dockerfile.gen output)
  .oc work  -> localhost/pod-<project>:base-oc-work  (opencode + profile work)
```

No textual merging happens: one `podman build` per stage (cache-checked;
logs are streamed and erased again on success, `--verbose` keeps them).

## The contract

- pod runs `recipes/<name> OPT1 OPT2 ...` with `POD_PROJECT_DIR=<project>` in
  the environment; **argv = the selection's options** (`.oc work` -> `$1=work`).
  The host environment is inherited, so a recipe may read host files (the ssh
  recipe bakes `~/.ssh/*.pub`).
- **stdout = one complete Dockerfile**, headers included. A recipe must work
  with no options at all (`pod recipes` lists them that way).
- **`--help` / `-h`**: print a short help (usage, options, examples) on stdout
  and exit 0. pod forwards `pod .<name> --help` to the recipe instead of
  printing pod's own help.
- Output is the build cache key: same options -> same output -> no rebuild.
  A recipe that ignores its options must say so in `POD_DESC`.
- `ARG BASE` / `FROM ${BASE}` links a stage to the previous image; pod only
  passes `BASE` when the recipe declares it, and only a first stage without
  it gets `$POD_DEFAULT_BASE` (default `docker.io/library/archlinux:base`).
  Any other declared `ARG` is forwarded from the host environment
  (`--build-arg K=V` wins).

## Header contract (parsed by `bin/pod` from the printed Dockerfile)

```dockerfile
# POD_DESC ...            one-liner for `pod recipes`
# POD_PORT <P>            1-part: 127.0.0.1:P:P (same port in and out)
# POD_PORT <H:C>          2-part: 127.0.0.1:H:C (host:container)
# POD_PORT <ip:H:C>       3-part: used as-is
# POD_SERVICE name=cmd    `pod service name` runs cmd (detached; --fg for fg)
# POD_AUTOSTART name      run service `name` synchronously once, right when
                          the container is created (restarts skip it via a
                          marker file; a recreated container runs it again)
# POD_VOLUME vol:/path    bind host path vol (~ expands) at /path, or
                          create the named volume vol when vol has no slash
# POD_ENV K[=V]           pass K through from the host when set, else set K=V
# POD_NAMETAG s           container-name suffix: pod-<workspace>-s-<dish>
# POD_HINT [service=n]t   printed by info/service; {port} {container} {dish}
# POD_BUILD_ARG NAME      listed by `pod recipes` (forwarding follows ARG lines)
```

## Bundled recipes

| script | adds | ports / services / volumes |
|---|---|---|
| `base` | the project's image: its own `Dockerfile`, else `dockerfile.gen` output | mounts `~/.gitconfig` + `~/.ssh` read-only (opts: `no-git`, `no-ssh`) |
| `dotfiles` | this dotfiles repo linked read-only at `/dotfiles` + an `install.sh` runner (opts: components, default `shells`); fish installed as root's default shell | volume `<repo>:/dotfiles:ro`, service `setup` (runs automatically at container creation via `POD_AUTOSTART`) |
| `oc` | opencode (pinned) | volume `oc-profile-<name>:/profile` + XDG env; `--with`-style API keys pass through |
| `vscode` | code-server (opens `/workspace`, trust off, language-aware extensions) | port 8080, service `code-server` |
| `ssh` | OpenSSH server, host public keys baked from `~/.ssh/*.pub` | port 2222→container 22, service `sshd` |

## Notes

- **`POD_AUTOSTART` installs configs at creation**: the declared service runs
  synchronously inside `pod` before it returns (so `pod run`/`shell` attach
  only afterwards) and touches `/tmp/.pod-autostart-done` on success —
  restarts skip it, a recreated container runs it again. Failure only warns;
  rerun with `pod service <name>`. Changing the header recreates the
  container (like a changed mount set).
- **Host binds are declared by recipes** (`POD_VOLUME ~/...`), never hardwired
  in pod: `base` binds `~/.gitconfig` and `~/.ssh` read-only unless its
  `no-git`/`no-ssh` options say otherwise (`pod .base no-git`). A changed
  mount set recreates the container (like a changed image).
- **Ports are fixed at `podman run`**: publish via `POD_PORT` or `-p`. With
  `--pod NAME` the whole netns is shared, so the pod's ports are fixed when the
  pod is *created* (first container) — later joiners get a warning instead.
- Services must start with an **absolute path** if the program demands it
  (Debian's sshd refuses `argv[0]` without one): `name=/usr/sbin/sshd -D -e`.
- The host's `~/.ssh` is mounted read-only at `/root/.ssh` by default, which
  *shadows* a baked `/root/.ssh/authorized_keys` — the ssh recipe therefore
  points sshd at `AuthorizedKeysFile /etc/ssh/authorized_keys/%u` (baked at
  build, unaffected by the mount).
- The `vscode` recipe's service runs `/usr/local/bin/vscode.start`, not
  code-server directly: the wrapper detects the languages used in `/workspace`
  (C/C++, Bash, `.editorconfig`), installs the matching Open VSX extensions
  plus helper packages (clangd, shellcheck) and only then launches code-server
  on `/workspace` with workspace trust disabled. The first start needs network
  and can take a minute; progress lands in `/var/log/vscode.start.log`.
- Bases must be bash-capable with `curl`/`git` (dockerfile.gen bases qualify);
  the bundled recipes install the rest themselves for pacman/apt/dnf.

## Adding a recipe

1. Copy an existing script; keep the `cat <<'DOCKER'` body with
   `ARG BASE` / `FROM ${BASE}` and `USER root`.
2. Declare the `# POD_*` headers above (and a `POD_DESC`).
3. Make the `RUN` steps idempotent-ish and distro-agnostic (probe `pacman` /
   `apt-get` / `dnf`) unless you only ever use one base.
4. Use it: `pod .base .<name> [opts...]`, chain further stages after it, or
   `oc --with <name>`.

Recipes live here because this repo is the source of truth for both the
images and the tooling that builds them.
