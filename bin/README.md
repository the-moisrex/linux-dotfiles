# Shell Commands
You can find shell commands here.

## CodeShell (`codeshell`)
![CodeShell Demo](https://user-images.githubusercontent.com/12122474/149156571-dc48cde7-547a-4150-b5df-3cf6783b7976.png)

`codeshell` is a shell script that will open a `tmux` window, splits it in two, and opens the editor in one pane, and runs the compiler/interpreter/make file in the other and also keeps re-running them when the files changes.

It's a greate tool for testing and benchmarking.

```bash
codeshell                      # Create a temp directory with the default template
codeshell -t=benchmark         # Use the benchmark template
codeshell -n=test.name         # Put the files in ~/codeshells/test.name instead of a temp directory or use that directory
codeshell -t=benchmark -n=test.name
```

CodeShell can also be run in Termux (Android).

Template directories are located in the `code-templates` directory from this repository. You can use the directory names of those templates in the `-t=template_name` argument of this script.

You can see examples of `~/codeshells` directory in [my codeshells repository](https://github.com/the-moisrex/codeshells).


## Welcome Back (`welcomeback`)
![image](https://user-images.githubusercontent.com/12122474/149163086-05ee270d-8820-4d07-a245-ec9f8fd99ab4.png)

It's just a welcome back message when it took you more than 24 hours to come back. You can put it in your `.bashrc` or `.zshrc` file.
