# _extract-help.awk — statically extract show_help() text from prompt scripts.
#
# Usage: awk -v mode=para -f _extract-help.awk file...    # one line per file
#        awk -v mode=full -f _extract-help.awk file...    # header + help body
#
# Locates the show_help() function and extracts its first heredoc, which
# holds the prompt's help text verbatim. This avoids running `bash file
# --help` for every prompt (the dominant cost of `prompt list`).
#
# mode=para prints one line per file:
#   <file>\x1f<first paragraph>   when a static heredoc exists (para may be empty)
#   <file>                        when there is no static help (caller falls back)
#
# mode=full prints one record per file:
#   \x1c<file>                    header line (FS char, ASCII 28)
#   ...help body lines...         present only when a static heredoc exists
# An empty body tells the caller to fall back to `bash <file> --help`.

function flush(    n, i, l, seen, para, b) {
    if (file == "")
        return
    if (mode == "full") {
        printf "%c%s\n", 28, file
        if (state == 2 || state == 3) {
            # Strip trailing newlines so the caller can print the body with
            # one trailing newline — byte-identical to `text="$(bash file
            # --help)"` + printf '%s\n'.
            b = body
            sub(/\n+$/, "", b)
            if (b != "")
                printf "%s\n", b
        }
        return
    }
    # mode == "para"
    if (state != 2 && state != 3) {
        printf "%s\n", file
        return
    }
    n = split(body, lines, "\n")
    para = ""
    seen = 0
    for (i = 1; i <= n; i++) {
        l = lines[i]
        if (seen && l ~ /^$/) break
        if (!seen && (l ~ /^Usage:/ || l ~ /^ +/ || l ~ /^$/)) continue
        seen = 1
        if (l ~ /^ +/) continue
        para = para l " "
    }
    sub(/ +$/, "", para)
    gsub(/\t/, " ", para)
    printf "%s%c%s\n", file, 31, para
}

FNR == 1 {
    flush()
    file = FILENAME
    state = 0          # 0 = before show_help, 1 = in show_help, 2 = in heredoc,
                       # 3 = heredoc complete, -1 = show_help without heredoc
    delim = ""
    dash = 0
    body = ""
}

state == 0 && /^show_help[ \t]*\(\)/ { state = 1; next }

state == 1 {
    if (match($0, /<<-?[ \t]*['"]?[A-Za-z_][A-Za-z0-9_]*['"]?/)) {
        tok = substr($0, RSTART, RLENGTH)
        dash = (tok ~ /^<<-/)
        sub(/^<<-?[ \t]*['"]?/, "", tok)
        sub(/['"]$/, "", tok)
        delim = tok
        state = 2
    } else if ($0 ~ /^[ \t]*}/) {
        state = -1
    }
    next
}

state == 2 {
    line = $0
    if (dash) sub(/^\t+/, "", line)
    if (line == delim) { state = 3; next }
    body = body line "\n"
    next
}

END { flush() }
