#!/bin/bash

# Script name: spp (Search C/C++)
# Purpose: Extract a function's source code from a C/C++ file using clang-check AST tools

# Default values
verbose=false
language=""
files=()

cur_file=$(basename "$0");

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)
            echo "Usage: $cur_file [OPTIONS] [FILES...] FUNCTION_NAME"
            echo "Extract a function's source code from a C/C++ file using clang-check AST tools."
            echo ""
            echo "Options:"
            echo "  --help, -h          Display this help message and exit"
            echo "  --lang c|c++        Force language instead of detecting from file extension"
            echo "  --c                 Force C mode"
            echo "  --c++, --cpp        Force C++ mode"
            echo "  --verbose, -v       Enable verbose output"
            echo ""
            echo "Arguments:"
            echo "  FILES               Optional list of files to search (default: all C++ files in repo)"
            echo "  FUNCTION_NAME       Name of the function to extract"
            echo ""
            echo "Example:"
            echo "  $cur_file --verbose is_canonically_ordered"
            echo "  $cur_file file1.cpp file2.h calculate"
            exit 0
            ;;
        --verbose|-v)
            verbose=true
            shift
            ;;
        --lang|--language)
            if [[ "$2" != "c" && "$2" != "c++" && "$2" != "cpp" && "$2" != "cxx" ]]; then
                echo "Error: --lang must be c or c++" >&2
                exit 1
            fi
            language="$2"
            [[ "$language" = "cpp" || "$language" = "cxx" ]] && language="c++"
            shift 2
            ;;
        --c)
            language="c"
            shift
            ;;
        --c++|--cpp)
            language="c++"
            shift
            ;;
        -*)
            echo "Error: Unknown option $1" >&2
            exit 1
            ;;
        *)
            # If we have more than one argument left, treat as files
            if [[ $# -gt 1 ]]; then
                files+=("$1")
            else
                function_name="$1"
            fi
            shift
            ;;
    esac
done

# Check if function name is provided
if [[ -z "$function_name" ]]; then
    echo "Error: Missing function name" >&2
    echo "Use '$cur_file --help' for usage information" >&2
    exit 1
fi

# Check if we're in a git repository
if ! git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
    echo "Error: Not in a git repository" >&2
    exit 1
fi

# Get git root directory
git_root=$(git rev-parse --show-toplevel)

# Read extra compiler arguments from .clang file if it exists
extra_args=()
clang_file="$git_root/.clang"
if [[ -f "$clang_file" ]]; then
    if [[ "$verbose" = true ]]; then
        echo "Using extra compiler arguments from $clang_file" >&2
    fi
    while IFS= read -r line; do
        # Skip comments and empty lines
        [[ "$line" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$line" ]] && continue
        # Add each argument individually
        for arg in $line; do
            extra_args+=("$arg")
        done
    done < "$clang_file"
fi

# Get list of files to search
if [[ ${#files[@]} -eq 0 ]]; then
    # Find C/C++ files in the repository that contain the function
    if [[ "$language" = "c" ]]; then
        cpp_files=$(git grep -l "$function_name" -- '*.c' '*.h')
    elif [[ "$language" = "c++" ]]; then
        cpp_files=$(git grep -l "$function_name" -- '*.cpp' '*.cxx' '*.cc' '*.c++' '*.h' '*.hpp' '*.hh' '*.hxx' '*.ixx')
    else
        cpp_files=$(git grep -l "$function_name" -- '*.c' '*.h' '*.cpp' '*.cxx' '*.cc' '*.c++' '*.hpp' '*.hh' '*.hxx' '*.ixx')
    fi
else
    # Use provided files, checking they exist
    cpp_files=()
    for file in "${files[@]}"; do
        if [[ ! -f "$file" ]]; then
            echo "Error: File '$file' not found" >&2
            exit 1
        fi
        cpp_files+=("$file")
    done
fi

if [[ -z "$cpp_files" ]]; then
    echo "Error: No C++ files found containing function '$function_name'" >&2
    exit 1
fi

if [[ "$verbose" = true ]]; then
    echo "Searching in files:" >&2
    for file in $cpp_files; do
        echo "  $file" >&2
    done
fi

# Try to extract the function from each file
found_file=$(mktemp)

cleanup() {
    rm -f "$found_file"
}
trap cleanup EXIT

for file in $cpp_files; do
    (
        if [[ "$verbose" = true ]]; then
            echo "Checking $file for function definition..." >&2
        fi

        file_language="$language"
        if [[ -z "$file_language" ]]; then
            case "$file" in
                *.c|*.h) file_language="c" ;;
                *) file_language="c++" ;;
            esac
        fi

        std_arg="-std=c23"
        [[ "$file_language" = "c++" ]] && std_arg="-std=c++26"

        filtered_args=()
        for arg in "${extra_args[@]}"; do
            if [[ "$arg" == -std=* ]]; then
                if [[ "$file_language" = "c" && "$arg" == *++* ]]; then
                    continue
                fi
                if [[ "$file_language" = "c++" && "$arg" != *++* ]]; then
                    continue
                fi
            fi
            filtered_args+=("$arg")
        done

        ast_list=$(clang-check -ast-list "$file" -- "$std_arg" "${filtered_args[@]}" 2>/dev/null)
        if [ -z "$ast_list" ]; then
            [[ "$verbose" = true ]] && echo "Warning: Failed to get AST list for $file" >&2
            exit 1
        fi

        qualified_names=$(echo "$ast_list" | grep "$function_name" | sort -u)
        if [[ -z "$qualified_names" ]]; then
            [[ "$verbose" = true ]] && echo "Warning: Function '$function_name' not found in AST list for $file" >&2
            exit 1
        fi

        [[ "$verbose" = true ]] && {
            echo "Found qualified names in $file:" >&2
            echo "$qualified_names" >&2
        }

        for qualified_name in $qualified_names; do
            [[ "$verbose" = true ]] && echo "Attempting to extract '$qualified_name' from $file..." >&2
            
            extracted_code=$(clang-check -ast-dump-filter="$qualified_name" -ast-print "$file" -- "$std_arg" "${filtered_args[@]}" 2>/dev/null \
                | grep -v "Printing " | clang-format)

            if [[ ${PIPESTATUS[0]} -eq 0 && -n "$extracted_code" ]]; then
                # Write result once, avoiding race condition
                (
                    flock 200
                    if [[ ! -s "$found_file" ]]; then
                        echo "$extracted_code" > "$found_file"
                    fi
                ) 200>"$found_file.lock"
                exit 0
            fi
        done

        [[ "$verbose" = true ]] && echo "Warning: Failed to extract function(s) from $file" >&2
        exit 1
    ) &
done

# Wait for first success
wait

# Print result if any
if [[ -s "$found_file" ]]; then
    cat "$found_file"
    exit 0
else
    exit 1
fi


echo "Error: Failed to extract function '$function_name' from any file" >&2
exit 1
