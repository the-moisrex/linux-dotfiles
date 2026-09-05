#!/bin/bash

mirror="http://mirror.koddos.net/gcc/releases"

show_help() {
    cat <<'EOF'
Usage: compilers.sh [command] [options]

Download and install GCC compilers from a mirror.

Commands:
  download <compiler> <version> <source_dir>  Download a GCC compiler tarball
  install <compiler> <version> <source_dir> [install_dir]  Install a GCC compiler

Options:
  -h, --help    Show this help message

Examples:
  compilers.sh download gcc 13.2.0 /tmp/src
  compilers.sh install gcc 13.2.0 /tmp/src /opt/compilers
EOF
}

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
    show_help
    exit 0
fi

if [[ $# -lt 2 ]]; then
    echo "Error: Missing command and arguments." >&2
    echo "Use --help for usage." >&2
    exit 1
fi

command="$1"
shift

download() {
    local compiler="$1" version="$2" source_dir="$3"

    if [[ -z "$compiler" ]]; then
        echo "Please specify the compiler you want to download" >&2
        return 1
    fi

    if [[ -z "$version" ]]; then
        echo "Please specify the version" >&2
        return 1
    fi

    if [[ -z "$source_dir" ]]; then
        echo "Please specify the source directory" >&2
        return 1
    fi

    mkdir -p "$source_dir"
    wget -O "$source_dir/$compiler-$version.tar.gz" "${mirror}/gcc-${version}/gcc-$version.tar.gz"
}


install() {
    local compiler="$1" version="$2" source_dir="$3" install_dir="${4:-/opt/compilers}"

    if [[ -z "$compiler" ]]; then
        echo "Please specify the compiler you want to install" >&2
        return 1
    fi
    
    if [[ -z "$version" ]]; then
        echo "Please specify the version" >&2
        return 1
    fi

    if [[ -z "$source_dir" ]]; then
        echo "Please specify the source directory" >&2
        return 1
    fi

    local file=""
    if [[ -f "$source_dir/$compiler-$version.tar.xz" ]]; then
        file="$source_dir/$compiler-$version.tar.xz"
    elif [[ -f "$source_dir/$compiler-$version.tar.gz" ]]; then
        file="$source_dir/$compiler-$version.tar.gz"
    else
        download "$compiler" "$version" "$source_dir"
        install "$compiler" "$version" "$source_dir" "$install_dir"
        return
    fi

    local extracted_dir real_source_dir
    extracted_dir="$(mktemp -d)"
    real_source_dir="${extracted_dir}/${compiler}-${version}"
    
    echo
    echo "---------------------------------------------------------------"
    echo "Compiler: $compiler-$version"
    echo "Source File: $file"
    echo "Extracted files in: $extracted_dir"
    echo "Real Source Directory: $real_source_dir"
    echo "Installation Directory: $install_dir"
    echo "---------------------------------------------------------------"
    echo

    mkdir -p "$extracted_dir"
    mkdir -p "$install_dir/$compiler-$version"
    mkdir -p "$real_source_dir"

    tar xvf "$file" -C "$extracted_dir"
    cd "${real_source_dir}"
    ./configure --prefix="$install_dir/$compiler-$version/"
    make -j "$(nproc)"
    cd "$OLDPWD"
    rm -rf "${extracted_dir}"
}

case "$command" in
    download)
        download "$@"
        ;;
    install)
        install "$@"
        ;;
    *)
        echo "Error: Unknown command '$command'" >&2
        show_help
        exit 1
        ;;
esac
