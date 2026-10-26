"""Turn an oversized prompt into an uploaded text file.

Every site caps a single chat message (ChatGPT answers ``HTTP 413``, GapGPT
reports ``token_limit``, …), and the cap is a *token* count, so the usable
character count depends on the language.  A prompt above a provider's inline
budget is therefore written to a temporary ``.txt`` file and sent through the
same upload path as ``--file``; the message itself becomes a short pointer to
that file.

The budgets are deliberately conservative and expressed in UTF-8 **bytes**: a
byte budget that is safe for CJK is safe for ASCII too.

``--prompt-file`` forces the file path, ``--no-prompt-file`` disables the
automatic switch (the site's own limit error is then reported with a hint), and
``--prompt-limit BYTES`` overrides the provider's budget.
"""

from __future__ import annotations

import os
import tempfile

#: Fallback inline budget for a provider that does not declare one.
DEFAULT_LIMIT = 64 * 1024

#: Error text that means "the site refused this because it is too big".
#: Covers ChatGPT's 413, GapGPT's ``token_limit`` and the Chinese messages the
#: sites use for their context/conversation limits.
TOO_LARGE_MARKERS = (
    "413",
    "request entity too large",
    "payload too large",
    "request body too large",
    "body too large",
    "too large",
    "too long",
    "token_limit",
    "context window",
    "context length",
    "maximum length",
    "length limit",
    "character limit",
    "超过",
    "过长",
    "字数",
    "上下文",
)

#: Messages that already explain the limit and the remedy (no extra hint).
_HANDLED = (
    "200,000-character limit",
    "200000-character limit",
)


def size(text):
    """The prompt size the budgets are compared against (UTF-8 bytes)."""
    return len(text.encode("utf-8"))


def human_size(count):
    """Render a byte count for a human-readable notice."""
    if count == 1:
        return "1 byte"
    if count < 1024:
        return f"{count} bytes"
    if count < 1024 * 1024:
        return f"{count / 1024:.1f} KiB"
    return f"{count / (1024 * 1024):.1f} MiB"


def limit_for(provider, override=None):
    """The inline budget for *provider*, honouring an explicit override."""
    if override is not None:
        return max(0, int(override))
    return int(getattr(provider, "inline_limit", DEFAULT_LIMIT))


def should_attach(prompt, provider, mode=None, limit=None):
    """Whether *prompt* should be uploaded as a file.

    ``mode`` is ``None`` (automatic), ``True`` (always) or ``False`` (never).
    Empty prompts are never attached.
    """
    if not prompt or mode is False:
        return False
    if mode is True:
        return True
    return size(prompt) > limit_for(provider, limit)


def write(prompt):
    """Write *prompt* to a temporary UTF-8 ``.txt`` and return its path."""
    descriptor, path = tempfile.mkstemp(prefix="ai-prompt-", suffix=".txt")
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as handle:
            handle.write(prompt)
            if not prompt.endswith("\n"):
                handle.write("\n")
        return path
    except BaseException:
        try:
            os.close(descriptor)
        except OSError:
            pass
        remove(path)
        raise


def remove(path):
    """Delete a temporary prompt file (best effort)."""
    if not path:
        return
    try:
        os.unlink(path)
    except OSError:
        pass


def reference(path, count):
    """The short message that points the model at the attached prompt file."""
    name = os.path.basename(path)
    return (f"The complete request is in the attached file `{name}` "
            f"({human_size(count)}). Read that file and respond to its contents "
            f"exactly as if it were this message; this message is only a pointer "
            f"to it.")


def looks_too_large(message):
    """True when *message* is a site complaining about the request size."""
    text = (message or "").lower()
    return any(marker in text for marker in TOO_LARGE_MARKERS)


def annotate_error(message, provider, prompt, used_file=False, disabled=False,
                   limit=None):
    """Append a concrete remedy to a site's size-limit error."""
    if not looks_too_large(message):
        return message
    if any(marker in message.lower() for marker in _HANDLED):
        return message
    count = size(prompt)
    budget = limit_for(provider, limit)
    lines = [message.rstrip()]
    if not getattr(provider, "prompt_file_upload", True):
        lines.append(
            f"[ai] The prompt is {human_size(count)} and was sent inline: "
            f"{getattr(provider, 'label', 'this provider')} has no upload "
            f"endpoint. Raise the model's context length in the server, or trim "
            f"the prompt (a prompt's --head option helps).")
    elif used_file:
        lines.append(
            f"[ai] The prompt was already uploaded as a text file "
            f"({human_size(count)}), but the site still refused it. Split it "
            f"into smaller prompts or trim it with the prompt's --head option.")
    elif disabled:
        lines.append(
            f"[ai] The prompt is {human_size(count)} and was sent inline "
            f"because of --no-prompt-file. Drop that flag (or add "
            f"--prompt-file) to upload it as a text file.")
    elif count > budget:
        lines.append(
            f"[ai] The prompt is {human_size(count)}, above the automatic "
            f"{human_size(budget)} file threshold. Re-run with --prompt-file "
            f"to upload it as a text file, or trim it.")
    else:
        lines.append(
            f"[ai] The prompt is {human_size(count)}, but the site's limit is "
            f"below the automatic {human_size(budget)} threshold. Re-run with "
            f"--prompt-file to upload it as a text file, or trim it.")
    return "\n".join(lines)
