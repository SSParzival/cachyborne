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

See [configuration](CONFIGURATION.md) and [the Spanish manual](../README.es.md).
