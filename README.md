# Custom Maps for Geronimo

A [GML](https://github.com/RemArrow/geronimo-mod-loader) plugin that puts custom maps in Geronimo's mission menu.
The plugin itself has no maps in it. Maps come as separate downloads.

## Maps

- **Myrne** by **SpectrumB**: the BONELAB map, rebuilt for Geronimo and shared with SpectrumB's permission. It is
  a snowy 660 m village (1.5 GB) with its own sky, ambience and surfaces. It is in GML Manager's Browse tab, or
  download `Myrne-<version>.zip` from the [release page](https://github.com/RemArrow/geronimo-custom-maps/releases)
  and extract it into `...\GERONIMO\Geronimo\Binaries\Win64`, which adds `GML\plugins\CustomMaps\Mods\Myrne`. Its
  ambience needs Custom Maps 0.3.0 or newer.

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
Ambience = /Game/Audio/Ambient/Villages/Winter_Map, 1.0   ; optional: a sound looped in the map, and its volume
```

- **Ambience:** the game's own maps place their ambience in the level. `Ambience` names a sound, either the
  game's (Winter Village's wind loop above) or one cooked into the map (Myrne: `/Myrne/Audio/Ambience1`), and the
  plugin loops it from the moment the map loads.
- **Reverb belongs in the map.** The game gives every sound its room with Resonance Audio presets on audio volumes
  (Meat's presets, in each map's audio sublevel). Resonance only changes preset from the volume the listener is in:
  a map with no audio volume keeps the previous map's reverb (the team room's indoor range), so gunshots and
  footsteps sound indoors. Give the map an `AudioVolume` over the whole playable area, plus smaller ones with a
  higher priority for rooms, each with a `ResonanceAudioReverbPluginPreset` of its own (enable the ResonanceAudio
  plugin in the project). Myrne copies Meat's Winter Village presets: outdoors with room effects off, gain 0.25;
  bunkers brick and concrete, gain 1.25.
  The `Reverb` and `ReverbZone` lines added in 0.3.0 have no effect, so don't use them: the engine doesn't apply a
  global preset while another one is active.
- **Footsteps and bullet hits** go by the surface type of what is hit. Give the map's colliders physical materials
  with the game's surface numbers (Concrete 10, Glass 14, Metal 15, Softwood 26, Dirt 30): copy the game's
  `PhysicalSurfaces` list into the project's `DefaultEngine.ini`.

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
installs is put back after (hash-verified). With `-Install`, the maps in `-Maps` are left out of that snapshot: they
stay installed after the run, and a multi-GB map is never copied to TEMP.

Cooked materials must carry **SM6** shaders: the game renders with D3D12 SM6, and a material cooked for SM5 only
draws as the engine's grey default. The project's `Config\DefaultEngine.ini` needs
`[/Script/WindowsTargetPlatform.WindowsTargetSettings]` with `+D3D12TargetedShaderFormats=PCD3D_SM6` (and SM5).
Check a cooked material's `.uexp` for the text `PCD3D_SM6`.

## Verified

- **2026-11-14, MapTest** (a 60 m floor, cover blocks, sun and sky, four tagged starts): it is listed as **Map
  Test** with Free Roam and PvP. Travelling there from the team room spawned the player at the `North` start, on
  the floor, for 10 s.
- **2026-11-15, a 15,000-mesh 660 m map (3.3 GB):** it loaded from `GML\plugins\CustomMaps\Mods\` and the player
  stood on its ground at the chosen start.
- **2026-11-19, Myrne cooked with SM6 and the game's forward renderer settings:** it loads in about 69 s. Its
  15,102 meshes draw their real materials, lit by the sun, and the wind ambience plays.
- MapTest (in `UEMaps`, not released) still draws black after the same re-cook. The cause is unknown.
- **2026-11-23, Myrne 1.1 with Custom Maps 0.3.0** (flat, no headset): it loads with its BONELAB sky, lit like
  Winter Village, and its own ambience starts. The player spawns on the ground.
- **2026-11-23, Myrne 1.2** (in a headset): gunshots, footsteps and ambience sound right with its own reverb
  volumes. A crash still happened after a few minutes; the crash dumps point into the game's renderer and VR code,
  not a mod, and it is being looked into.

The project must also use the game's renderer settings (forward shading, no Lumen). Copy the game's
`[/Script/Engine.RendererSettings]` block into the project's `DefaultEngine.ini`. Without it, a map draws but
nothing in it is lit. Then turn **mesh distance fields off** (`r.GenerateMeshDistanceFields=False`): Myrne's carried
4.4 GB of them, which a movable sky light loads into video memory, and it crashed a 4 GB GPU. No shader reads that
setting, so it doesn't break the game's renderer settings.

Light: with only movable lights (no baked bounce light), copying Winter Village's grey 1.5-lux sun came out about
4× too dark. Myrne uses a white 1.5-lux sun and a sky light of 1.0.
