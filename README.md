# Yeeps Mods

Community mods for **Yeeps (blaze build)**. The game ships only BNM (ByNameModding)
and a small mod loader; everything else is made by players, here.

**Official download, help and mod requests: https://discord.gg/UfYxE9W9dX**
Only get the game and mods from the Discord or this repo.

## Install a mod (players)

1. Have the newest blaze APK from the Discord installed.
2. Get the mod's `.so` file (from this repo's **Actions** build artifacts, or build it yourself).
3. Connect your headset to SideQuest and copy the `.so` into
   `Android/data/com.TrassGames.Yeeps/files/mods/`
4. Restart Yeeps.

To remove a mod, delete its file. If the game won't start, make an empty file
named `DISABLE` in that `mods` folder: every mod is skipped until you delete it.
A mod that crashes the game is skipped automatically on the next start.

## Starter Menu

`mods/starter-menu/` is the example everyone builds on:

- Hold **Y** to show the menu next to your left hand.
- Touch a button with your **right index finger**.
- **Fly**: hold A to fly where you look.
- **Platforms**: hold a grip to stand on a platform under that hand.
- **Menu color**: changes the theme.

Everything in it is local: nothing is sent to other players.

## Make a mod (with Codex, Claude or by hand)

1. Install the Android NDK (r27c), CMake, Ninja and Python 3.
2. Copy `mods/starter-menu/` to `mods/<your-mod>/` and rename it in its `CMakeLists.txt`.
3. Build: `ANDROID_NDK=/path/to/ndk ./build.sh` (Windows: `set ANDROID_NDK=...` then `build.bat`).
   Your mod is in `out/<your-mod>.so`.

AI assistants: open this folder in Codex or Claude Code. They read `AGENTS.md`
(Claude also reads `CLAUDE.md`), which holds the rules.

Read before you start:
- [AGENTS.md](AGENTS.md): what is and isn't allowed
- [docs/GUIDE.md](docs/GUIDE.md): how to avoid lag and crashes
- [docs/API.md](docs/API.md): the mod API

## Rules in one line

Mods may only change **your own game**. No laggers, crashers, kicks, bans, spam,
free items, account stuff or spoofing. These are refused by the AI rules, blocked
by the automatic check on every pull request, and reviewed by the owner.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
