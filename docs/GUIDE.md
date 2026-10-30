# Making a good mod: no lag, no crashes

## How a menu works (see `mods/starter-menu`)

1. **One per-frame callback** (`ym::onUpdate`) runs everything: read controllers,
   show/hide the menu, run the features that are switched on.
2. **Controllers**: `InputDevices.TryGetFeatureValue_bool` for Primary/Secondary/Grip/Trigger.
3. **Buttons are cubes** in a menu object. The menu is built **once** per room, then only
   shown, hidden and moved.
4. **Pressing**: a small sphere on your right index finger. Each frame, its position is
   turned into the menu's local space and checked against the button rectangles. One press
   per touch, with a short delay.
5. **Room changes**: when `localPlayer()` changes, every old object is gone. Forget all
   cached objects and build again.

## Don't make the game lag

1. **Look things up once.** Classes, methods and fields go in a struct that is filled on the
   first frame, never looked up again.
2. **No scene searches every frame** (`FindObjectsOfType`, `GameObject.Find`). Do it once and
   keep the result.
3. **Build UI once**, then only show/hide/move it.
4. **No new objects every frame.** Keep a hard limit (the starter menu: 2 platforms max).
5. **No new strings every frame.** Make `CreateMonoString` values once and keep them in a GC handle.
6. **No logging every frame.** Log events only.
7. **Do nothing when nothing is on.** With the menu closed and features off, your update
   should return almost instantly.
8. **Nothing over the network.** It isn't allowed in this repo at all (see AGENTS.md), and
   it is the #1 cause of lag for everyone else.

The game turns off a mod's updates if they keep taking over 4 ms a frame.

## Don't make the game crash

1. **Unity only on the main thread**: only inside `onUpdate` / `onRoomChange`. Never start
   your own threads that touch the game.
2. **Never let an exception escape** your callback (the `ym::` helpers catch them, but catch
   your own too).
3. **Check before use**: `IsValid()` on every class/method you look up, null checks on every
   object, `Object.op_Implicit` to see whether a Unity object still exists.
4. **Hold objects in a GC handle** (`Ref` in the starter menu). A raw pointer kept between
   frames can be freed by the garbage collector.
5. **Reset on room change.** Pointers from the old room are dead.
6. **Don't hook hot code** (every block's or mob's Update, physics callbacks). One update
   callback is enough for almost everything.
7. **Test leaving and joining rooms 10+ times** with your mod on. That's where most crashes are.

A mod that crashes the game is skipped on the next start until its file changes.

## Fair play

Movement features (fly, platforms, speed) are fine for building, exploring and private
worlds, but they ruin tag and minigames for everyone else. Keep them easy to switch off,
and use `ym::hasRole(...)` if a feature should only be for certain roles.

## Checklist before you share it

- [ ] `python3 tools/check_mod.py` passes
- [ ] Builds with `./build.sh` / `build.bat`
- [ ] Menu closed + everything off = no noticeable cost
- [ ] Left/joined rooms 10+ times with it on, no crash
- [ ] Played 20+ minutes with it on
- [ ] Nothing it does can be seen or felt by other players in a bad way
