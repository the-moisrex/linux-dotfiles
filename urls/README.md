# URL Functions

URL helper scripts for the `url` dispatcher. Each file defines a bash function that
generates a URL and an optional `_help` function. They are sourced by `url` and invoked
as subcommands.

## Usage

```bash
url <function> [args...]
url <function> --help
url list
```

## Functions

### Search Engines

| Function | Usage | Example |
|----------|-------|---------|
| `google` | `<query>` | `url google "bash scripting"` |
| `ddg` | `<query>` | `url ddg "cmake fetchcontent"` |
| `bing` | `<query>` | `url bing "neovim config"` |

### Translation

| Function | Usage | Example |
|----------|-------|---------|
| `translate` | `<src_lang> <tgt_lang> <text>` | `url translate en fa "hello world"` |
| `deepl` | `<src_lang> <tgt_lang> <text>` | `url deepl EN FA "hello world"` |

### GitHub

| Function | Usage | Example |
|----------|-------|---------|
| `github` | `[user] [repo]` | `url github moisrex` |
| `gist` | `[user] [gist_id]` | `url gist moisrex abc123` |

### GitLab

| Function | Usage | Example |
|----------|-------|---------|
| `gitlab` | `<user/repo> [type]` | `url gitlab gitlab-org/gitlab issues` |

Valid types: `repo`, `issues`, `pipelines`, `merge_requests`, `wiki`.

### Reddit

| Function | Usage | Example |
|----------|-------|---------|
| `reddit` | `<query> [subreddit]` | `url reddit "cmake" linux` |
| `subreddit` | `<name>` | `url subreddit cpp` |
| `reddituser` | `<username>` | `url reddituser spez` |

### X / Twitter

| Function | Usage | Example |
|----------|-------|---------|
| `xsearch` | `<query>` | `url xsearch "from:elonmusk"` |
| `xuser` | `<username>` | `url xuser elonmusk` |
| `xpost` | `<username> <status_id>` | `url xpost elonmusk 1234567890` |

### YouTube

| Function | Usage | Example |
|----------|-------|---------|
| `yt` | `<query>` | `url yt "cmake tutorial"` |
| `ytch` | `<channel>` | `url ytch @ThePrimeagen` |

### Package Registries

| Function | Usage | Example |
|----------|-------|---------|
| `npm` | `<package>` | `url npm express` |
| `pypi` | `<package>` | `url pypi requests` |
| `crates` | `<package>` | `url crates tokio` |
| `aur` | `<package>` | `url aur yay` |

### Developer Docs

| Function | Usage | Example |
|----------|-------|---------|
| `so` | `<query>` | `url so "bash array"` |
| `mdn` | `<query>` | `url mdn "fetch api"` |
| `cppref` | `<query>` | `url cppref "std::vector"` |
| `archwiki` | `<query>` | `url archwiki "systemd"` |
| `tldr` | `<command>` | `url tldr tar` |
| `wiki` | `<query>` | `url wiki "recursion"` |
| `wikifa` | `<query>` | `url wikifa "بازگشت"` |

### Marketplaces

| Function | Usage | Example |
|----------|-------|---------|
| `docker` | `<image>` | `url docker nginx` |
| `flathub` | `<app>` | `url flathub org.mozilla.firefox` |
| `vscode` | `<extension>` | `url vscode ms-vscode.cpptools` |

### Other

| Function | Usage | Example |
|----------|-------|---------|
| `estekhare` | `[page_number]` | `url estekhare 215` |

---

## Adding New Functions

Each file in `urls/` is sourced by `url`. A script needs at minimum a single function:

```bash
#!/bin/bash
# Description shown in `url list`
# Usage: myfunction <arg>
# Example: myfunction foo

myfunction() {
    local arg="$*"
    echo "https://example.com/$arg"
}
```

The `url` dispatcher discovers functions by:
1. Scanning for `function_name()` patterns (ignoring `_help`/`_info` variants)
2. Falling back to the filename

Include a `_help()` function for `--help` support:

```bash
myfunction_help() {
    echo "Usage: myfunction <arg>"
    echo ""
    echo "Description of what it does."
    echo ""
    echo "Arguments:"
    echo "  arg  What this argument is"
    echo ""
    echo "Examples:"
    echo "  url myfunction foo"
}
```

---

See the main [README](../README.md) for the rest of the repository.
