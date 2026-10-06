# POD_PORT 2222:22
# POD_BUILD_ARG SSH_PUBKEY
# POD_SERVICE sshd=/usr/sbin/sshd -D -e
# ssh - SSH into the dev container (CLion, Remote-SSH, scp). The host's public
# key(s) are baked in at build time through the SSH_PUBKEY build arg, which
# bin/pod fills from ~/.ssh/*.pub automatically.
ARG BASE
FROM ${BASE}

USER root

ARG SSH_PUBKEY=""

RUN set -eux; \
    if command -v pacman >/dev/null 2>&1; then \
        pacman -Sy --noconfirm --needed openssh ca-certificates; \
        pacman -Scc --noconfirm; \
    elif command -v apt-get >/dev/null 2>&1; then \
        apt-get update; \
        apt-get install -y --no-install-recommends openssh-server ca-certificates; \
        rm -rf /var/lib/apt/lists/*; \
    elif command -v dnf >/dev/null 2>&1; then \
        dnf install -y openssh-server ca-certificates; \
        dnf clean all; \
    fi; \
    command -v sshd >/dev/null; \
    ssh-keygen -A; \
    mkdir -p /run/sshd; \
    set_conf() { \
        if grep -qE "^[#[:space:]]*$1 " /etc/ssh/sshd_config; then \
            sed -i -E "s|^[#[:space:]]*$1 .*|$1 $2|" /etc/ssh/sshd_config; \
        else \
            printf '%s %s\n' "$1" "$2" >> /etc/ssh/sshd_config; \
        fi; \
    }; \
    set_conf PermitRootLogin without-password; \
    set_conf PasswordAuthentication no; \
    set_conf PubkeyAuthentication yes; \
    set_conf StrictModes no; \
    set_conf AuthorizedKeysFile /etc/ssh/authorized_keys/%u; \
    sshd -t

RUN if [ -n "$SSH_PUBKEY" ]; then \
        mkdir -p /etc/ssh/authorized_keys; \
        printf '%s\n' "$SSH_PUBKEY" > /etc/ssh/authorized_keys/root; \
        chmod 755 /etc/ssh/authorized_keys; \
        chmod 600 /etc/ssh/authorized_keys/root; \
    fi
