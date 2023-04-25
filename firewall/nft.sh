#!/usr/bin/env bash

filename=$(basename "$0")


function bashify {
    code="$1"
    echo -n "$code";
}

function create_bash {
    nft_file="$1"
    if [ ! "$nft_file" ]; then
        echo "nft file $nft_file doesn't exists.";
        exit 1;
    fi

    export -f bashify; # make run_bash available to sed
    cat "$nft_file" | 
        sed -re '/#\$/! s/"/\\"/g' | 
        sed -re '/#\$/! s/^[^\n]*$/echo "&";/' | 
        sed -re 's/(^(.*?))#\$([^\n]*)/echo "\1"\n\3/';
}

function print_help {
    echo "What is this?"
    echo "  This script lets you preprocess a nftables script with bash script."
    echo
    echo "Shebang usage:"
    echo "  Add this to your nftables' script and make your script executable for ease of use:"
    echo "    #!/usr/bin/env -S $filename nft"
    echo "  Make them executable with:"
    echo "    $ chmod +x yourfile.sh.nft"
    echo "  If you want to just print it and not run it through nft, then use this shebang:"
    echo "    #!/usr/bin/env -S $filename"
    echo "  Make sure $filename is in your \$PATH environment, if you don't want that, then"
    echo "  use this kinda shebang:"
    echo "    #!/path/to/$filename nft"
    echo
    echo "File Syntax:"
    echo "  The lines that start with '#$' will be executed as a bash script, "
    echo "  the rest of the lines are just 'echo'ed."
    echo
    echo "Example (Check if a user exists):"
    echo "  chain test {"
    echo "    #$ if id $USER &>/dev/null; then"
    echo "        skuid $USER counter;"
    echo "    #$ fi;"
    echo "  }"
    echo
    echo "Usage:"
    echo "$filename [file]       # Render the file";
    echo "       bash [file]  # Generate the bash script, don't run it"
    echo "       run  [file]  # Render the file"
    echo "       nft  [file]  # Render the file run it through nft"
    echo "       help         # Print Help"
}

case $1 in
    --bash|--sh|bash|sh)
        create_bash $2;
        ;;
    --run|run|exec|execute)
        create_bash $2 | bash;
        ;;
    --nft|nft|nftables)
        create_bash $2 | nft -f -;
        ;;
    --help|-h|help)
        print_help;
        ;;
    *)
        create_bash $1 | bash;
        ;;
esac
