# Custom Maps for Geronimo

A [GML](https://github.com/RemArrow/geronimo-mod-loader) plugin that puts custom maps in Geronimo's mission menu.
It ships no maps itself.

## For players

A map mod is a folder with its `.uplugin`, `Content\Paks\Windows\*.pak/.utoc/.ucas` and a `GeronimoMap.ini`. Put it in
either place:

- `GML\plugins\CustomMaps\Mods\<Name>\`, next to this plugin. GML 2.4 or newer copies it into
  `<game>\Geronimo\Mods\<Name>\` at launch, before the engine looks for mods. A big map is copied once, then only
  when it changes. Deleting the folder removes the map on the next launch.
- `<game>\Geronimo\Mods\<Name>\`, by hand.

The engine mounts the map itself. This plugin finds every `GeronimoMap.ini` there at startup and adds each map to the
mission list with a **Free Roam** mode (no AI). With the [PvP](https://github.com/RemArrow/geronimo-pvp) plugin
installed, a **PvP** mode appears too. Pick it and start, as with the game's own missions. To switch a map off, put a
`disabled.txt` in its folder.

```ini
Name = My Map                 ; the mission's name in the menu
Level = /MyMap/Maps/MyMap     ; the map's package path
MapX = 1050                   ; where its pin sits on the mission menu's world map
MapY = 230
Spawn = North, 520, 120       ; a spawn players can pick: name (= the PlayerStart's tag), x, y on the tactical map
Spawn = South, 520, 560
Like = DA_WinterVillage       ; a game mission to borrow the thumbnail and tactical map picture from
```

## Install

Needs [GML 2.4 or newer](https://github.com/RemArrow/geronimo-mod-loader/releases). The easiest way is
[GML Manager](https://github.com/RemArrow/geronimo-mod-loader#gml-manager): Custom Maps is in its Browse tab. By hand:
close the game, download `CustomMaps-<version>.zip` from
[Releases](https://github.com/RemArrow/geronimo-custom-maps/releases) and extract it into
`...\GERONIMO\Geronimo\Binaries\Win64`. That adds `GML\plugins\CustomMaps`.

## Making a map

You need Unreal Engine 5.7 and a project **named Geronimo**. A plugin cooked from it mounts at
`../../../Geronimo/Mods/<Plugin>/`, which is where the game looks.

1. Make a content plugin in `<project>\Mods\<Name>\` (`<Name>.uplugin` with `"CanContainContent": true`) and build
   the level in it, at `/<Name>/Maps/<Name>`.
   - Keep every mesh and material the map uses inside the plugin: the shipped game may not have the editor's basic
     shapes.
   - Add `PlayerStart`s whose tags match the `Spawn` names.
   - Players spawn inside any solid volume that encloses a start and fall through the ground. Don't give trigger
     or sound-zone volumes collision. (A map ported from Unity had a 660 m ambience trigger built as a solid box.)
   - Editor Python runs headless:
     `UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<script.py> -nosplash -stdout -unattended`.
     `print()` doesn't reach the log here, so write results to a file.
2. Make a base release of the project once (`-createreleaseversion=1`).
3. Cook the map as DLC on that release:

   ```powershell
   & "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=<path>\Geronimo.uproject" -noP4 -platform=Win64 -clientconfig=Shipping -cook -stage -pak -iostore -skipbuild -nocompile -nocompileeditor -unattended -utf8output "-dlcname=<Name>" "-basedonreleaseversion=1" "-map=/<Name>/Maps/<Name>"
   ```

   - Quote the `=` arguments in PowerShell: it split `-createreleaseversion=1.0` into `1` and `.0`.
   - "Uncooked Engine content ... referenced by DLC" (for example `EngineMaterials/DefaultNormal`) fails the
     cook only at its very end, after every package is cooked. The game ships that content. Stage what was
     cooked with the same command, with `-skipcook` instead of `-cook`.
   - UAT refuses `-iterate` for DLC, but the cook commandlet accepts it and recooks only what changed:
     `UnrealEditor-Cmd.exe <uproject> -run=cook -targetplatform=Windows -DLCName=<Name> -BasedOnReleaseVersion=1 -iterate -map=/<Name>/Maps/<Name>`.
     Then stage with `-skipcook`.
4. The map mod is `Mods\<Name>\Saved\StagedBuilds\Windows\Geronimo\Mods\<Name>\`. Add the `GeronimoMap.ini`.

## Testing

`tests\run_test.ps1 -Maps <plugin folder> -Level /<Name>/Maps/<Name>` runs `tests\MapsDev` in the game, flat in a
window. It goes from the team room into the map the way the menu does and logs:
- the player starts, meshes, materials and lights;
- the collision under the first start: line and capsule traces, and the player's capsule settings;
- where the player stands for 10 s.

It snapshots SaveGames, the GML configs, the plugins folder and the game's `Mods` folder first. Everything it
installs is put back after (hash-verified).

## Verified

- **2026-11-14, MapTest** (a 60 m floor, cover blocks, sun and sky, four tagged starts): it is listed as **Map
  Test** with Free Roam and PvP. Travelling there from the team room spawned the player at the `North` start, on
  the floor, for 10 s.
- **2026-11-15, a 15,000-mesh 660 m map (3.3 GB):** it loaded from `GML\plugins\CustomMaps\Mods\` and the player
  stood on its ground at the chosen start.
