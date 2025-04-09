# Helix
- **Helix**, (from *hy-extended lix*) is an internal fork of lix with some patches that wouldn't fit in upstream (either because it's ugly, or because upstream doesn't want it).
- Everything that could be useful to other people is CL'd to lix first, and gets committed here if rejected.
- *hy* softworks provides **NO SUPPORT for helix**. Bugs you may find, *if they can be reproduced with stock lix*, should be reported there. Otherwise, you're on your own.
- We intend to follow lix main on an ad-hoc cadence. In the future, rebasing our patches will hopefully be automated.

Currently, we have the following patches
(TODO: automate generating this list as well):
- build with lto
- use mimalloc (UGLY!!!, you've been warned)
- :open in repl

The performance-related features make helix about 10% faster than stock lix.
There is also a binary cache on [Cachix](https://hysoftworks.cachix.org).
The original Lix readme follows below:
## Lix

**Lix** is an implementation of **Nix**, a powerful package management system for Linux and other Unix systems that makes package management reliable and reproducible.

Read more about us at https://lix.systems.

### Installation

On Linux and macOS the easiest way to install Lix is to run the following shell command
(as a user other than root):

```console
$ curl -sSf -L https://install.lix.systems/lix | sh -s -- install
```

For systems that **already have a Nix implementation installed**, such as NixOS systems, read our [install page](https://lix.systems/install)

### Building And Developing

See our [Hacking guide](https://git.lix.systems/lix-project/lix/src/branch/main/doc/manual/src/contributing/hacking.md) in our manual for instruction on how to set up a development environment and build Lix from source.

### Additional Resources

- The Lix reference manual:
  - [Stable](https://docs.lix.systems/manual/lix/stable/)
  - [Nightly](https://docs.lix.systems/manual/lix/nightly/) (NOTE: [not automatically updated, yet](https://git.lix.systems/lix-project/lix/issues/742))
- [Our wiki](https://wiki.lix.systems)
- [Matrix - #space:lix.systems](https://matrix.to/#/#space:lix.systems)

### License

Lix is released under [LGPL-2.1-or-later](./COPYING).
