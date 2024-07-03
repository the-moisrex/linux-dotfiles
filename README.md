# linux-dotfiles
My dot files, configurations, binaries, services, and other usefull utilities gathered in one place.

## Executables and shell scripts
Written in the `bin` directory and each have their own requirements you need to install.

- __coreshell__: simple IDE to test stuff in different languages
- __gtask__: taskwarrior for git projects
- __cpu__: get cpu performance informations and change cpu profile.
- __history-usage__: show which commands you've used the most
- __net.tor.proxy__: tor laod balancer proxy
- __weather__: get the weather of your city
- __clear-coredump__: clears your logs and core dumps
- __buble.patched__: change the sounds of the keyboard sounds played by `bucklespring` and start buckle
- __transfer__: share a file with terminal (Needs updating to a new website since it's going down soon)
- __welcomeback__: says welcome back
- __speech__: Text-To-Speech with Google Translate
- __compilers.sh__: a set of functions to download and install compilers (not finished yet)
- and __60+__ more little utilities just in the [./bin](./bin/README.md) directory.

## Firewall

Personal `nftables` firewall settings, other network related scripts.

- __nftop__: show a `nftables`' counters LIVE (super helpful if you've handwriten your firewall and you want to know what's what)
- __iptun__: create tunnel between two systems
- __tproxy__: create transparent proxies for `torloadbalancer` (which runs N number of tor instances and load balance between them), and similar uses.
- __ignore__: add domains and ip address to a lists; which will be used to route or not route those IP/Domains into VPN/Proxy/... that is configured like the `torloadbalancer` or OpenVPN and what not; works if the firewall is configured.
- and other scripts and configs that might be less useful to most people
