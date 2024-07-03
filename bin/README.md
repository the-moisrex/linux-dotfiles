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

## Signaling to a process and its subprocesses (`sig`)

Send signals to a list of processes and all of their sub-processes.

`sig stop firefox` and then `sig cont firefox` for example pauses firefox and then continues firefox.

## `ipof`

Get the ip address of a domain in a short format

## `isup`

Check if a website/domain/ip is up or not (Ping + HTTP + HTTPS).

## `download`

Download stuff off the internet using `yt-dlp` and `wget`.

## Temporary Browsers (`brave.tmp`, `chromium.tmp`, and `firefox.tmp`)

These scripts will launch a brand new process of the browser that its profile is newly created in the `/tmp/...` directory.

## `display-info`

Shows the **width**, **height**, and **diagonal** of the connected monitors in table.

## `stopwatch`

A simple stopwatch

## Get and or change CPU profiles (`cpu`)

`cpu powersave` or `cpu performance`

## `install-date`

Get the install date of the system

## `run`

A utility for C++ developers; finds the root of the git project, then finds the CMake build directories, and lets you run CMake Targets.

For example `run test-unicode` in **Web++** project will run the `test-unicode` cmake target, and it doesn't matter in which sub-directory of webpp you're in.

## `should-restart`

Checks the libraries that have been updated, but older versions are loaded
into the system and thus gives you the idea of which applications need to be restarted or the whole system needs to be restarted.

## `optimize`

- Clean the trash
- Update the system
- Discard unusded blocks of the filesystem (`fstrim`)
- Clear caches and what not with `bleachbit`

You can run `optimize -tube; poweroff` to do all of the above and shutdown the system.

## `strip-colors`

Remove colors from an input; use it like `software-that-prints-color | strip-colors`.

## `pacman.sync`

Sync pacman packages (`/var/cache/pacman/pkg`) between two systems.

```bash
sudo pacman.sync from 192.168.1.2
pacman.sync to root@192.168.1.2
```

## List installed packages, and sort them by size (`pacman.list.sortby.size`)

Pacman utility that gives you an idea which packages are big and useless.

