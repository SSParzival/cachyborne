# File mods and XML patches

Use **Mods y parches** in the native control center. Select a mod directory,
enable the layers you want and order them: the last enabled layer wins when
several mods replace the same path. Save and prepare the game again to publish
the selection. A draft alone does not change an already prepared launch plan.

The default directory is `~/.local/share/cachyborne/mods`. Common layouts are:

```text
mods/
  My mod/
    dvdroot_ps4/
      chr/...
      param/...
      menu/...
```

The optional **Espejo del Sueño: cambiar apariencia** switch restores the
workshop mirror's appearance editor and character preview. Enable it in the
same page, save and prepare again, or use `setup GAME_DIR --change-appearance`
(`--no-change-appearance` disables it). It starts off. Setup generates a checked
retail 1.09 map layer without modifying the dump; selected player mods take
priority over that layer. A mod replacing the Dream map can therefore override
the restored mirror. The generated layer works independently of the file-mod
enable switch.

An `app0/` or `CUSA03173/` wrapper is accepted, as are a repeated archive wrapper
and direct game subdirectories (`chr`, `parts`, `map`, `menu`, `msg`, `sound`,
`font`, etc.). Extract ZIP/7z downloads before selecting them. Case-insensitive
matching lets a mod's `DVDROOT_PS4/Chr` replace the corresponding lowercase path.

Setup builds a symlink overlay for the prepared generation. Original game files
are not overwritten. Keep both the dump and the selected source mod directories
in place. A file-level layer replaces a complete DCX/BND archive; the manager
does not merge two archives internally. Logs report replaced and added files:
if a replacement mod reports zero replaced files, check its folder structure.

Use `--mods-dir DIR --mods-config FILE` for CLI preparation. GUI selections,
including enabled/disabled layers and their order, are saved with preferences
and snapshotted in the prepared generation. A neighboring
`CUSA03173-mods/dvdroot_ps4` directory is also recognized by the resolver.

Select compatible XML patches in the same page or use
`--patches-dir DIR --patches-config FILE`. Patches are filtered by executable
and version, compiled at setup and applied to the runtime image. Do not apply
another prepatched 60 FPS executable: Cachyborne supplies its own frame-time
hooks. Debug-menu patches require their separate font assets.

Text language and voice language remain independent of mod order. With
`BB_LANGUAGE=20`, recognized localized message and layout paths resolve to the
available Spanish assets; `BB_VOICE_LANGUAGE=eng` resolves speech separately.
A voice replacement should provide the bank matching the selected voice code.

`scripts/param_tables.py` also generates optional loose-file parameter mods
using BBHOST-style TOML row edits and the game's own field names. It validates
the table layout, field widths and array bounds before publishing a new mod;
the dump and existing mods stay intact. See
[named parameter inspection and generation](VERIFY_NATIVE.md#inspección-de-tablas-de-bbhost).
This is an offline asset workflow; live parameter overrides and the BBHOST
plugin ABI remain separate integrations.

See [configuration](CONFIGURATION.md) and [the Spanish manual](../README.es.md).

The optional **Renacimiento en el Altar de la Desesperación** switch enables
BBHOST-style attribute reassignment at the altar. It requires the Yharnam
Stone and offline mode. It resets attributes to the origin, refunds the actual
level costs, opens the native level-up screen and offers accept or undo. Undo
restores the original attributes, level and echoes even after spending refunded
echoes in that screen. Closing the final choice returns to the level-up screen.

Enable it in **Mods y parches**, save and prepare again, or use
`setup GAME_DIR --rebirth` (`--no-rebirth` disables it). It starts off. Setup
creates the altar script and localized event messages from the player's own
1.09 dump, before selected player mod layers. The normal build produces the
required script helper. The prepared profile pins the asset/runtime choice
together; an inherited `BB_REBIRTH` value cannot disconnect that pairing.

The standalone preparation tools remain available for development:

```sh
cmake --build out/gpu --target rebirth-script-tool
out/gpu/rebirth-script-tool check INPUT.esd
out/gpu/rebirth-script-tool rebirth INPUT.esd NEW_OUTPUT.esd
python3 scripts/rebirth_assets.py --game /path/to/CUSA03173 \
  --cache /path/outside/the/dump --tool out/gpu/rebirth-script-tool
```

The helper accepts only the known source/output script hashes and refuses an
existing output. The complete generator preserves all other archive entries
and original messages, adding five altar entries in Spanish for `spaes` and
`spaar`, and English for other folders. It publishes a complete layer after
all files validate. Verified cache generations are reused; edited/damaged
ones are retained and repaired separately. Manual asset generation alone does
not enable the runtime service. No game scripts are distributed with Cachyborne.
