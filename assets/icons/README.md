# Cachyborne Sweet Purple icons

Original vector artwork under GPL-2.0-or-later. The magenta/violet palette follows
Sweet-Purple-Filled and its inherited Candy application icons; no theme artwork,
fonts or game assets are redistributed. The hunter mark identifies the game;
a gear badge identifies configuration.

SVG is the source. The installer installs both PNG/SVG pairs into hicolor and
registers separate game/configuration entries. SDL uses the 128-pixel BMP.

```sh
rsvg-convert -w 512 -h 512 assets/icons/cachyborne.svg -o assets/icons/cachyborne.png
rsvg-convert -w 512 -h 512 assets/icons/cachyborne-settings.svg -o assets/icons/cachyborne-settings.png
magick assets/icons/cachyborne.png -resize 128x128 -background '#17121f' -alpha remove BMP3:assets/icons/cachyborne.bmp
```
