# no longer urob's zmk-config

This is my personal [ZMK firmware](https://github.com/zmkfirmware/zmk/)
configuration. It consists of a 6x3x2 base layout that is used for the eyelash corne and a nice nano-based sofle from ergomech.store.
I originally forked this from urob's config and use many of his features but have edited enough that documentation has suffered. I exclusively use local builds but am attempting to keep the github workflow functional to allow other folks to play with it with minimum effort required. The local build uses a dynamically-generated macro that tells me when the build happened and includes some macros with more personal information, the "real" version of which is not checked in to this repository.


My configuration currently builds against `v0.2` of upstream ZMK.
Custom functionality is added through various
[ZMK modules](https://github.com/search?q=topic%3Azmk-module+fork%3Atrue+owner%3Aurob+&type=repositories).
The state of the entire firmware is pinned in my `west`
[manifest](https://github.com/urob/zmk-config/blob/main/config/west.yml).

## Notes

- smart numbers and caps need some care, particularly an explicit cancel key.
- look into how urob does smart mouse layer
- consider using long press on page up/down for home/end
- some combos do not fire. notably, those for @, ^, 9, and the bottom row em-dash. these do not seem to be timing- or HRM-related (i tested with a wide range of timing settings as well as on the gaming layer which lacks HRMs. in addition, the colon combo only works with the keys specified in a particular order: reversing them in the macro invocation means the combo does not work.
- the remainder of this doc is mostly edited notes from urob's original; I've edited out many things that are already integrated into my usage, and just left things I want to investigte (further) or of which I might want to remind myself later.


![](draw/keymap.png)
([Click here](https://raw.githubusercontent.com/urob/zmk-config/refs/heads/main/draw/base.svg)
for a breakdown by layer - powered by
[keymap-drawer](https://github.com/caksoylar/keymap-drawer).)


### Troubleshooting

Hopefully, the HRM configuration "just works". If it doesn't, here's a few
smaller (and larger) things to try.

- **Noticeable delay when tapping HRMs:** Increase `require-prior-idle-ms`. As a
  rule of thumb, you want to set it to at least `10500/x` where `x` is your
  (relaxed) WPM for English prose.[^3]
- **False negatives (same-hand):** Reduce `tapping-term-ms` (or disable
  `hold-trigger-key-positions`)
- **False negatives (cross-hand):** Reduce `require-prior-idle-ms` (or set
  flavor to `hold-preferred` -- to continue using `hold-trigger-on-release`, you
  must apply this
  [patch](https://github.com/celejewski/zmk/commit/d7a8482712d87963e59b74238667346221199293)
  to ZMK
- **False positives (same-hand):** Increase `tapping-term-ms`
- **False positives (cross-hand):** Increase `require-prior-idle-ms` (or set
  flavor to `tap-preferred`, which requires holding HRMs past tapping term to
  activate)


## Smart layers and other gimmicks


##### Magic Repeat/Shift/Capsword

My right thumb triggers three variations of shift as well as repeat: Tapping
after any alpha key yields key-repeat (to reduce SFUs). Tapping after any other
keycode yields sticky-shift (used to capitalize alphas). Holding activates a
regular shift, and double-tapping (or equivalently shift + tap) activates ZMK's
Caps-word behavior.

One minor technical detail: While it would be possible to implement the
double-tap functionality as a tap-dance, this would add a delay when using
single taps. To avoid the delays, I instead implemented the double-tap
functionality as a mod-morph.


##### Swapper

I am using [Nick Conway](https://github.com/nickconway)'s fantastic
[tri-state](https://github.com/zmkfirmware/zmk/pull/1366) behavior for a
one-handed Alt-Tab switcher (`PWin` and `NWin`).

## Local build environment

I streamline my local build process using `nix`, `direnv` and `just`. This
automatically sets up a virtual development environment with `west`, the
`zephyr-sdk` and all its dependencies when `cd`-ing into the ZMK-workspace. The
environment is _completely isolated_ and won't pollute your system.

### Setup

#### Pre-requisites

1. Install the `nix` package manager:

   ```bash
   # Install Nix with flake support enabled
   curl --proto '=https' --tlsv1.2 -sSf -L https://install.determinate.systems/nix |
      sh -s -- install --no-confirm

   # Start the nix daemon without restarting the shell
   . /nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh
   ```

2. Install [`direnv`](https://direnv.net/) (and optionally but recommended
   [`nix-direnv`](https://github.com/nix-community/nix-direnv)[^4]) using your
   package manager of choice. E.g., using the `nix` package manager that we just
   installed[^5]:

   ```
   nix profile install nixpkgs#direnv nixpkgs#nix-direnv
   ```

3. Set up the `direnv` [shell-hook](https://direnv.net/docs/hook.html) for your
   shell. E.g., for `bash`:

   ```bash
   # Install the shell-hook
   echo 'eval "$(direnv hook bash)"' >> ~/.bashrc

   # Enable nix-direnv (if installed in the previous step)
   mkdir -p ~/.config/direnv
   echo 'source $HOME/.nix-profile/share/nix-direnv/direnvrc' >> ~/.config/direnv/direnvrc

   # Optional: make direnv less verbose
   echo '[global]\nwarn_timeout = "2m"\nhide_env_diff = true' >> ~/.config/direnv/direnv.toml

   # Source the bashrc to activate the hook (or start a new shell)
   source ~/.bashrc
   ```

#### Set up the workspace

1. Clone _your fork_ of this repository. I like to name my local clone
   `zmk-workspace` as it will be the toplevel of the development environment.

   ```bash
   # Replace `urob` with your username
   git clone https://github.com/urob/zmk-config zmk-workspace
   ```

2. Enter the workspace and set up the environment.

   ```bash
   # The first time you enter the workspace, you will be prompted to allow direnv
   cd zmk-workspace

   # Allow direnv for the workspace, which will set up the environment (this takes a while)
   direnv allow

   # Initialize the Zephyr workspace and pull in the ZMK dependencies
   # (same as `west init -l config && west update && west zephyr-export`)
   just init
   ```

### Usage

After following the steps above your workspace should look like this:

```
zmk-workspace
├── config
├── firmware (created after building)
├── modules
├── zephyr
└── zmk
```

#### Building the firmware

To build the firmware, simply type `just build all` from anywhere in the
workspace. This will parse `build.yaml` and build the firmware for all board and
shield combinations listed there.

To only build the firmware for a specific target, use `just build <target>`.
This will build the firmware for all matching board and shield combinations. For
instance, to build the firmware for my Corneish Zen, I can type
`just build zen`, which builds both `corneish_zen_v2_left` and
`corneish_zen_v2_right`. (`just list` shows all valid build targets.)

Additional arguments to `just build` are passed on to `west`. For instance, a
pristine build can be triggered with `just build all -p`.

(For this particular example, there is also a `just clean` recipe, which clears
the build cache. To list all available recipes, type `just`. Bonus tip: `just`
provides
[completion scripts](https://github.com/casey/just?tab=readme-ov-file#shell-completion-scripts)
for many shells.)

#### Drawing the keymap

The build environment packages
[keymap-drawer](https://github.com/caksoylar/keymap-drawer). `just draw` parses
`base.keymap` and draws it to `draw/base.svg`.

#### Hacking the firmware

To make changes to the ZMK source or any of the modules, simply edit the files
or use `git` to pull in changes.

To switch to any remote branches or tags, use `git fetch` inside a module
directory to make the remote refs locally available. Then switch to the desired
branch with `git checkout <branch>` as usual. You may also want to register
additional remotes to work with or consider making them the default in
`config/west.yml`.

#### Updating the build environment

To update the ZMK dependencies, use `just update`. This will pull in the latest
version of ZMK and all modules specified in `config/west.yml`. Make sure to
commit and push all local changes you have made to ZMK and the modules before
running this command, as this will overwrite them.

To upgrade the Zephyr SDK and Python build dependencies, use `just upgrade-sdk`. (Use with care --
Running this will upgrade all Nix packages and may end up breaking the build environment. When in
doubt, I recommend keeping the environment pinned to `flake.lock`, which is [continuously
tested](https://github.com/urob/zmk-config/actions/workflows/test-build-env.yml) on all systems.)

## Bonus: A (moderately) faster Github Actions Workflow

Using the same Nix-based environment, I have set up a drop-in replacement for
the default ZMK Github Actions build workflow. While mainly a proof-of-concept,
it does run moderately faster, especially with a cold cache.


## Related resources

- The
  [collection](https://github.com/search?q=topic%3Azmk-module+fork%3Atrue+owner%3Aurob+&type=repositories)
  of ZMK modules used in this configuration.
- A ZMK-centric
  [introduction to Git](https://gist.github.com/urob/68a1e206b2356a01b876ed02d3f542c7)
  (useful for maintaining your own ZMK fork with a custom selection of PRs).
