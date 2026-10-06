FROM docker.io/library/node:22-alpine

ARG OPENCODE_VERSION=1.18.34

ENV HOME=/home/agent

RUN apk add --no-cache \
        bash \
        curl \
        diffutils \
        fzf \
        git \
        git-lfs \
        jq \
        openssh-client \
        ripgrep \
        tini \
        unzip

# XDG dirs live on /profile so a single named volume holds config, auth and sessions.
RUN mkdir -p /home/agent /profile /workspace \
    && chown -R 1000:1000 /home/agent /profile

RUN curl -fsSL https://opencode.ai/install \
        | HOME="$HOME" bash -s -- --version "$OPENCODE_VERSION" --no-modify-path \
    && ln -s /home/agent/.opencode/bin/opencode /usr/local/bin/opencode \
    && opencode --version

ENV PATH="/home/agent/.opencode/bin:$PATH" \
    XDG_CONFIG_HOME=/profile/config \
    XDG_DATA_HOME=/profile/data \
    XDG_CACHE_HOME=/profile/cache \
    XDG_STATE_HOME=/profile/state

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

WORKDIR /workspace

EXPOSE 4096

ENTRYPOINT ["/sbin/tini", "--", "/usr/local/bin/entrypoint.sh"]
CMD ["opencode", "web", "--hostname", "0.0.0.0", "--port", "4096"]
