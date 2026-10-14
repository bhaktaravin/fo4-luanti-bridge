# Fallout 4 × Luanti Bridge

Blocks you place or dig in Luanti appear and disappear in Fallout 4, and your Fallout 4 character shows up in the Luanti world as a blue-and-yellow Vault-suit marker.

```
 Luanti  ──HTTP──▶  bridge.py  ◀──HTTP──  Fallout 4 (F4SE plugin, under Proton)
 (Lua mod)          127.0.0.1:8765         (DLL built by GitHub Actions)
```

Set up for **Fedora Kinoite + Flatpak Steam + Fallout 4 1.11.240**. Everything runs on your own PC. Keep it that way: don't publish builds that include Bethesda assets.

In the paths below, `STEAM` means `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`.

---

## 1. Bridge (2 minutes)

Python is already on Kinoite. From this folder:

```
python3 bridge/bridge.py
```

Leave that terminal open. (`python3 bridge/test_bridge.py` runs a self-test.)

## 2. Luanti mod (5 minutes)

1. In Luanti's **Content** tab, install a game if you haven't yet. **Minetest Game** is the simplest.
2. On Luanti's **About** tab, click the button that opens the user data folder. Copy `luanti_mod/fo4bridge` into the `mods` folder there.
3. Allow the mod to use HTTP: **Settings → All settings**, search for **HTTP mods**, and set it to `fo4bridge`. (Or add `secure.http_mods = fo4bridge` to `luanti.conf` in that same data folder.) Restart Luanti.
4. Create a world, click **Select Mods**, enable **fo4bridge**, and play.
5. You should see `[fo4bridge] Connected to bridge.py` in chat.
   - `/fo_status` shows whether Fallout is connected.
   - `/fo_anchor` makes the spot you're standing on line up with where your Fallout character was standing when the bridge started.

At this point Luanti ↔ bridge works on its own.

## 3. Fallout 4 side

### a. F4SE and Address Library

Download both from Nexus and extract them into `STEAM/steamapps/common/Fallout 4/`:
- **F4SE 0.7.9** (supports 1.11.240): https://www.nexusmods.com/fallout4/mods/42147
- **Address Library for F4SE Plugins**, the **1.11.240** file: https://www.nexusmods.com/fallout4/mods/47327?tab=files

Make Steam launch F4SE through Proton. In the game folder:
1. Rename `Fallout4Launcher.exe` to `Fallout4Launcher.exe.bak`
2. Copy `f4se_loader.exe` and name the copy `Fallout4Launcher.exe`

Pressing Play in Steam now starts Fallout through F4SE. Undo by restoring the `.bak`.

### b. Build the plugin (GitHub does it)

Every push to this repo builds the plugin on a free Windows server (`.github/workflows/build-plugin.yml`). To get the result:
1. Open your repo on GitHub → **Actions** → the latest **Build Fallout 4 plugin** run.
2. Once it has a green check, download **FO4LuantiBridge** under **Artifacts**.
3. Extract it into `STEAM/steamapps/common/Fallout 4/Data/`. That puts `FO4LuantiBridge.dll` and `FO4LuantiBridge.ini` in `Data/F4SE/Plugins/`.

If a build fails, copy the red error lines from the log and paste them to Claude.

### c. Pick the "block" object

The plugin spawns one object for each Luanti block. Choose it in-game:
1. Load a save, open the console with `~`, and type `help concrete 4` (or `help crate 4`, `help box 4`).
2. Pick a **STAT** entry that sounds like a small cube or crate and note its 8-digit ID.
3. Put that ID in `Data/F4SE/Plugins/FO4LuantiBridge.ini` as `BlockBaseFormID`.
4. Adjust `BlockScale` until one object is about 1 meter wide.

With `BlockBaseFormID=0`, the plugin still syncs your position to Luanti but spawns nothing.

## Playing

1. Start `bridge.py`.
2. Launch Fallout 4 from Steam, load a save, and stand somewhere open, like outside Sanctuary. This spot becomes the anchor. You'll see "Luanti bridge connected."
3. In Luanti, stand somewhere flat and type `/fo_anchor`.
4. Build in Luanti and watch the structure appear in the Commonwealth. Walk around in Fallout and watch your marker move in Luanti.

To re-anchor Fallout at a new spot, run `curl -X POST http://127.0.0.1:8765/fallout/reanchor`, or restart the bridge.

## Troubleshooting

Fallout's logs live inside the Proton prefix:
`STEAM/steamapps/compatdata/377160/pfx/drive_c/users/steamuser/Documents/My Games/Fallout4/F4SE/`
`f4se.log` shows whether the plugin loaded, and `FO4LuantiBridge.log` shows what the plugin is doing.

| Problem | Fix |
|---|---|
| Luanti says HTTP access denied | The HTTP mods setting is missing, or Luanti wasn't restarted |
| Luanti says "Lost connection" | `bridge.py` isn't running |
| No F4SE logs at all | F4SE isn't launching. Redo step 3a's launcher swap |
| `f4se.log` doesn't mention FO4LuantiBridge | The DLL isn't in `Data/F4SE/Plugins/`, or Address Library is missing |
| Fallout never shows "connected" | Check `FO4LuantiBridge.log`, and confirm `bridge.py` is running |
| Blocks don't appear | `BlockBaseFormID` is 0 or isn't a placeable object. The log names the problem |
| Blocks are huge or tiny | Adjust `BlockScale` |
| Blocks are far away | Re-anchor both sides while standing still |

## Ideas for next steps

- Map Luanti block types to different Fallout objects (stone → concrete, wood → crate, glass → window)
- Mirror the Luanti player into Fallout as an NPC
- Two-way building: let Fallout settlement objects appear as Luanti blocks
- Sync day/night between the games
