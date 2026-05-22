#!/usr/bin/fish

function detatch
    if test (count $argv) -eq 0
        echo "Usage: detatch <command> [args...]"
        echo "Detatch the process and exit the current shell"
        return 1
    end

    # Fully detatch: new session, drop I/O, run in background
    setsid $argv[1] $argv[2..-1] </dev/null >/dev/null 2>&1 &

    # Exit the *current* shell
    exit 0
end

# Make a directory and immediately cd into it.
function mkcd
    if test (count $argv) -eq 0
        echo "Usage: mkcd <dirname>"
        return 1
    end
    mkdir -p $argv[1]
    cd $argv[1]
end

# Jump to the directory of a file.
function fcd
    if test (count $argv) -eq 0
        echo "Usage: cdf <file>"
        return 1
    end
    cd (dirname $argv[1])
end

# Go up N directories.
function up
    set count (math max 1 (string replace -r '[^0-9]' '' $argv 2>/dev/null))
    for i in (seq $count)
        builtin cd ..
    end
end

# See what process is listening on a port.
function whichport
    if test (count $argv) -eq 0
        echo "Usage: whichport <port>"
        return 1
    end
    lsof -i :$argv[1]
end



function cdf -d "Fuzzy jump to a directory"
    # 0. Handle help flag
    if contains -- -h $argv; or contains -- --help $argv
        echo "Usage: cdf [DIRECTORY] [FILTER]"
        echo "Fuzzy jump to a directory."
        echo ""
        echo "Arguments:"
        echo "  DIRECTORY  Start fzf in this directory (if it is a valid path)"
        echo "  FILTER     Bypass interactive fzf and select the first match"
        echo ""
        echo "Examples:"
        echo "  cdf                  # Interactive search in current directory"
        echo "  cdf /etc             # Interactive search in /etc"
        echo "  cdf proj             # Go to first result matching 'proj' in current directory"
        echo "  cdf /var/log nginx   # Go to first result matching 'nginx' in /var/log"
        echo "  cdf -                # Go to the last location"
        echo "  cdf -2               # Go to the one before the last location"
        echo "  cdf --               # Re-run last query from current location"
        echo "  cdf --2              # Re-run 2nd-to-last query from current location"
        return 0
    end

    set -l history_file ~/.config/cdf_history

    # Handle history navigation
    if string match -qr '^--?[0-9]*$' -- "$argv[1]"
        if not test -f "$history_file"
            echo "No history found" >&2
            return 1
        end

        set -l from_current (string match -q -- '--*' "$argv[1]")
        set -l index_str (string replace -r '^--?' '' "$argv[1]")
        set -l index (test -n "$index_str"; and echo $index_str; or echo 1)

        # Read history line (format: query\0original_dir\0target_dir\0)
        set -l history_line (tail -n "$index" "$history_file" | head -n 1)
        
        if test -z "$history_line"
            echo "No history entry at index $index" >&2
            return 1
        end

        # Split by null bytes
        set -l parts (string split0 -- $history_line)
        set -l query "$parts[1]"
        set -l original_dir "$parts[2]"
        set -l target_dir "$parts[3]"

        if test "$from_current" = 0
            # Single dash: go to original location first, then re-run query
            if not test -d "$original_dir"
                echo "Original directory no longer exists: $original_dir" >&2
                return 1
            end
            
            cd "$original_dir"
            
            if test -z "$query"
                # Was an interactive search, run interactively again
                cdf
            else
                # Re-run the filter query
                cdf $query
            end
        else
            # Double dash: re-run query from current location
            if test -z "$query"
                # Was an interactive search, run interactively again
                cdf
            else
                # Re-run the filter query from here
                cdf $query
            end
        end
        
        return 0
    end

    # 1. Parse arguments for directory and filter
    set -l search_dir "."
    set -l filter_query ""

    if test (count $argv) -gt 0
        if test -d "$argv[1]"
            set search_dir $argv[1]
            if test (count $argv) -gt 1
                set filter_query "$argv[2..-1]"
            end
        else
            set filter_query "$argv"
        end
    else
        cd $HOME
        return 0
    end

    # 2. Define the array of directories you want to exclude everywhere
    set -l excludes .git node_modules .cache .DS_Store

    # Resolve actual path to check if we are searching within HOME
    set -l resolved_dir $search_dir
    if type -q realpath
        set resolved_dir (realpath "$search_dir" 2>/dev/null)
    end

    # 3. Build the search command
    set -l search_cmd
    if type -q fd
        # fd natively respects .gitignore.
        set search_cmd fd --type d
        
        # Include hidden directories ONLY if the search root is NOT the home directory
        if test "$resolved_dir" != "$HOME"
            set -a search_cmd --hidden
        end
        
        for ex in $excludes
            set -a search_cmd --exclude "$ex"
        end
        
        # Tell fd to search for everything ('.') in the specified directory
        set -a search_cmd . "$search_dir"
    else
        # Fallback to standard find
        set search_cmd find "$search_dir"
        
        # Exclude directories starting with a dot if in the home directory
        if test "$resolved_dir" = "$HOME"
            set -a search_cmd -name ".*" -not -name "." -prune -o
        end
        
        for ex in $excludes
            set -a search_cmd -name "$ex" -prune -o
        end
        set -a search_cmd -type d -print
    end

    # 4. Handle the fzf execution
    set -l dir
    if test -n "$filter_query"
        # Input specified: use as filter and grab the first result
        set dir ($search_cmd 2>/dev/null | fzf --filter="$filter_query" | head -n 1)
    else
        # No input: run fzf interactively
        set dir ($search_cmd 2>/dev/null | fzf)
    end

    # 5. Change directory if a result was found
    if test -n "$dir"
        # Add to history (null-byte separated for safe whitespace handling)
        mkdir -p (dirname $history_file) 2>/dev/null
        printf '%s\0%s\0%s\0\n' "$filter_query" "$PWD" "$dir" >> $history_file

        cd "$dir"
    end
end


function root -d "Navigate to the root of the current git repository, or home"
    set -l git_root (command git rev-parse --show-toplevel 2>/dev/null)
    
    if test -n "$git_root"
        cd $git_root
    else
        cd ~
    end
end
