# Blazes Apk mod menu addon

Community mods for **Yeeps (blaze build)**. The game ships only BNM (ByNameModding)
and a small mod loader; everything else is made by players, here.

**Official download, help and mod requests: https://discord.gg/UfYxE9W9dX**
Only get the game and mods from the Discord or this repo.

## Install a mod (players)

1. Have the newest blaze APK from the Discord installed, and start Yeeps once. The
   game makes its `mods` folder the first time it runs.
2. Download the mod's `.so` file on your headset (from the Discord, from this repo's
   **Actions** build artifacts, or build it yourself).
3. Open **CX File Explorer** (or any file explorer) and move the `.so` into:
   `Android/data/com.TrassGames.Yeeps/files/mods/`
4. Restart Yeeps. The mod loads by itself.

If your file explorer can't open the `Android/data` folder, plug the headset into a
PC and drop the file into the same folder with SideQuest.

To remove a mod, delete its file. If the game won't start, make an empty file
named `DISABLE` in that `mods` folder: every mod is skipped until you delete it.
A mod that crashes the game is skipped automatically on the next start.

## Get the Blaze Menu (no building needed)

Download it on your headset: **https://live.yeeps-tools.org/download/menu**
(it saves as `Blaze-Menu.so`), then move it into the mods folder as above and
restart Yeeps. That link always gives the newest version.

## Starter Menu

`mods/starter-menu/` is the example everyone builds on:

The **Blaze Menu**, in the style of ii's menu. Hold **Y** and the board appears on
your left hand (turn your palm toward you); press buttons with your **right index
finger**, and use the **<** and **>** bars for more pages.

- **Movement Mods**: Platforms, Fly, Fast Fly, Velocity Fly, Car, Teleport Gun,
  Iron Man, Noclip, Speed Boost, No Gravity, Low Gravity, Save Spot, Go To Spot.
- **Rig Mods** (pick one, hold left trigger; everyone sees it): Wave, Helicopter,
  T-Pose, Windmill, Swim, Jumping Jacks, Spaz, Beyblade.
- **Items** (uses the item in your left hand; everyone sees them): Spawn Item Gun,
  Duplicate Items (3x3), Duplicate Items (3x3x3), Spawn On Hand. Every button has a
  cooldown so rooms can't be flooded.
- **Settings**: pointer position, Iron Man and Car speed, right-hand menu, themes,
  rainbow menu, FPS, clock, notifications, Turn Everything Off.
- **Enabled Mods**: everything you have on.

Movement mods only change your own game. Rig mods and items are seen by everyone in
the room; AGENTS.md has the limits for those.

## Make a mod (with Codex, Claude or by hand)

1. Install the Android NDK (r27c), CMake, Ninja and Python 3.
2. Copy `mods/starter-menu/` to `mods/<your-mod>/` and rename it in its `CMakeLists.txt`.
3. Build: `ANDROID_NDK=/path/to/ndk ./build.sh` (Windows: `set ANDROID_NDK=...` then `build.bat`).
   Your mod is in `out/<your-mod>.so`.

AI assistants: open this folder in Codex or Claude Code. They read `AGENTS.md`
(Claude also reads `CLAUDE.md`), which holds the rules and a full how-to.

**Check your copy isn't edited** before you use it (the AI does this first too):
`python3 tools/verify_repo.py` compares everything outside `mods/` with this repo on
GitHub. If it says `EDITED`, download a fresh copy from
https://github.com/Blazeisodd/Blazes-Apk-mod-menu-addon.

Read before you start:
- [AGENTS.md](AGENTS.md): what is and isn't allowed
- [docs/GUIDE.md](docs/GUIDE.md): how to avoid lag and crashes
- [docs/API.md](docs/API.md): the mod API

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
