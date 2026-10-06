# POD_PORT 8080
# POD_SERVICE code-server=/usr/local/bin/vscode.start
# vscode - full VS Code (code-server) inside the project dev container: opens
# /workspace directly, never asks for workspace trust, and installs Open VSX
# extensions for the languages detected in /workspace (C/C++, Bash, ...).
# Requires a bash-capable base with tar/gzip (dockerfile.gen bases qualify).
ARG BASE
FROM ${BASE}

USER root

RUN set -eux; \
    if command -v pacman >/dev/null 2>&1; then \
        pacman -Sy --noconfirm --needed ca-certificates curl tar gzip; \
        pacman -Scc --noconfirm; \
    elif command -v apt-get >/dev/null 2>&1; then \
        apt-get update; \
        apt-get install -y --no-install-recommends ca-certificates curl tar gzip; \
        rm -rf /var/lib/apt/lists/*; \
    elif command -v dnf >/dev/null 2>&1; then \
        dnf install -y ca-certificates curl tar gzip; \
        dnf clean all; \
    fi; \
    command -v curl >/dev/null

RUN curl -fsSL https://code-server.dev/install.sh \
        | sh -s -- --method=standalone --prefix=/usr/local \
    && code-server --version

# Same defaults as the POD_SERVICE flags for every launch (also covers a
# manual `code-server` inside the container); the wrapper passes the trust
# flag again on the command line.
RUN mkdir -p /root/.config/code-server \
    && printf '%s\n' \
        'bind-addr: 0.0.0.0:8080' \
        'auth: none' \
        'disable-workspace-trust: true' \
        > /root/.config/code-server/config.yaml

RUN <<'EOF'
set -eux
cat > /usr/local/bin/vscode.start <<'SCRIPT'
#!/usr/bin/env bash
# vscode.start - code-server launcher for the "vscode" pod fragment.
#
# - re-execs with its own argv[0] so `pod service code-server` can detect it:
#   service_running matches the first word of the service command against
#   /proc/*/cmdline, and a shebang script would show up as "bash" otherwise
# - detects the languages used by /workspace and installs the matching
#   extensions (Open VSX) and helper binaries before the server starts
# - opens /workspace and disables workspace trust
set -u

LOG=/var/log/vscode.start.log
log() { printf '%s %s\n' "$(date -u +%FT%TZ)" "$*" >>"$LOG"; }

if [[ -z "${VSCODE_START_REEXEC:-}" ]]; then
    export VSCODE_START_REEXEC=1
    exec -a "$0" bash "$0" "$@"
fi

log "start (args: $*)"
command -v code-server >/dev/null 2>&1 || { log "ERROR: code-server not found"; exit 1; }
[[ -d /workspace ]] || log "WARNING: /workspace is missing"

# name | find -name globs | shebang regex | helper bin:pacman:apt:dnf | extensions
# (a row matches when a glob hits or the shebang regex matches some file;
#  fields are pipe-separated, so regexes must not contain "|")
LANGS=(
    'c-cpp|*.c,*.cc,*.cpp,*.cxx,*.h,*.hh,*.hpp,*.hxx,CMakeLists.txt,meson.build,compile_commands.json,.clangd,.clang-format,.clang-tidy||clangd:clang:clangd:clang-tools-extra|llvm-vs-code-extensions.vscode-clangd'
    'bash|*.sh,*.bash,*.bats,.shellcheckrc|^#!.*[^[:alnum:]_](ba)?sh||mads-hartmann.bash-ide-vscode,timonwong.shellcheck'
    'editorconfig|.editorconfig|||EditorConfig.EditorConfig'
)

PRUNE=(-name .git -o -name node_modules -o -name build -o -name dist \
       -o -name vendor -o -name .cache)

have_exts=""

ext_installed() { # $1 = extension id
    printf '%s\n' "$have_exts" | grep -qiFx "$1"
}

ensure_bin() { # $1=bin $2=pacman-pkg $3=apt-pkg $4=dnf-pkg (retries once)
    command -v "$1" >/dev/null 2>&1 && return 0
    local attempt
    for attempt in 1 2; do
        log "installing $1 (attempt $attempt)"
        if command -v pacman >/dev/null 2>&1; then
            pacman -Sy --noconfirm --needed "$2" >>"$LOG" 2>&1 && return 0
        elif command -v apt-get >/dev/null 2>&1; then
            { apt-get update && apt-get install -y --no-install-recommends "$3"; } >>"$LOG" 2>&1 && return 0
        elif command -v dnf >/dev/null 2>&1; then
            dnf install -y "$4" >>"$LOG" 2>&1 && return 0
        else
            break
        fi
    done
    log "WARNING: could not install $1"
}

install_ext() { # $1 = extension id (retries once)
    ext_installed "$1" && return 0
    local attempt
    for attempt in 1 2; do
        log "installing extension $1 (attempt $attempt)"
        if code-server --install-extension "$1" --force >>"$LOG" 2>&1; then
            have_exts+=$'\n'"$1"
            log "installed extension $1"
            return 0
        fi
    done
    log "WARNING: failed to install $1"
}

if [[ -d /workspace ]]; then
    have_exts="$(code-server --list-extensions 2>/dev/null)"
    for row in "${LANGS[@]}"; do
        IFS='|' read -r name globs shebang helper exts <<<"$row"
        hit=""
        if [[ -n "$globs" ]]; then
            IFS=',' read -ra names <<<"$globs"
            gargs=()
            for g in "${names[@]}"; do
                [[ ${#gargs[@]} -gt 0 ]] && gargs+=(-o)
                gargs+=(-name "$g")
            done
            hit="$(find /workspace -maxdepth 4 \( "${PRUNE[@]}" \) -prune -o \
                -type f \( "${gargs[@]}" \) -print -quit 2>/dev/null)"
        fi
        if [[ -z "$hit" && -n "$shebang" ]]; then
            hit="$(find /workspace -maxdepth 3 \( "${PRUNE[@]}" \) -prune -o \
                -type f -exec grep -Il -m1 -E "$shebang" {} + 2>/dev/null | head -n1)"
        fi
        [[ -n "$hit" ]] || continue
        log "detected $name ($hit)"
        if [[ -n "$helper" ]]; then
            IFS=':' read -r hbin hpkg hapkg hdpkg <<<"$helper"
            ensure_bin "$hbin" "$hpkg" "$hapkg" "$hdpkg"
        fi
        IFS=',' read -ra ids <<<"$exts"
        for id in "${ids[@]}"; do
            [[ -n "$id" ]] && install_ext "$id"
        done
    done
fi

log "launching code-server on /workspace"
if [[ -t 1 ]]; then
    code-server --bind-addr 0.0.0.0:8080 --auth none \
        --disable-workspace-trust /workspace "$@"
else
    code-server --bind-addr 0.0.0.0:8080 --auth none \
        --disable-workspace-trust /workspace "$@" >>"$LOG" 2>&1
fi
SCRIPT
chmod 755 /usr/local/bin/vscode.start
EOF
