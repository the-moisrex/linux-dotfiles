# POD_PROFILE 1
# oc - run the opencode AI agent inside a project dev container (bin/oc).
# Requires a bash-capable base (dockerfile.gen emits arch/debian/ubuntu/fedora).
ARG BASE
FROM ${BASE}

USER root

ARG OPENCODE_VERSION=1.18.34

RUN set -eux; \
    if command -v pacman >/dev/null 2>&1; then \
        pacman -Sy --noconfirm --needed \
            ca-certificates curl git ripgrep fzf jq openssh; \
        pacman -Scc --noconfirm; \
    elif command -v apt-get >/dev/null 2>&1; then \
        apt-get update; \
        apt-get install -y --no-install-recommends \
            ca-certificates curl git ripgrep fzf jq openssh-client; \
        rm -rf /var/lib/apt/lists/*; \
    elif command -v dnf >/dev/null 2>&1; then \
        dnf install -y ca-certificates curl git ripgrep fzf jq openssh-clients; \
        dnf clean all; \
    fi; \
    command -v curl >/dev/null; \
    command -v git >/dev/null

# Standalone opencode binary; /profile (config, auth, sessions) is a volume
# mounted by pod/oc and selected through XDG_* at exec time.
RUN curl -fsSL https://opencode.ai/install \
        | HOME=/root bash -s -- --version "$OPENCODE_VERSION" --no-modify-path \
    && ln -sf /root/.opencode/bin/opencode /usr/local/bin/opencode \
    && opencode --version
