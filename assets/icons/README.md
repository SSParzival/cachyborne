# Cachyborne application icon

Original vector artwork for this project: a silver hunter sigil and blood moon
on a charcoal square. Distributed under the repository's GPL-2.0-or-later
license. It contains no downloaded game artwork or fonts.

The SVG is the source. PNG is installed in the hicolor theme; BMP is loaded by
SDL for supported native window icons. KDE/Wayland resolves the PNG/SVG through
the matching desktop entry and `cachyborne-play` application ID.

Regenerate with librsvg and ImageMagick:

```bash
rsvg-convert -w 512 -h 512 assets/icons/cachyborne.svg -o assets/icons/cachyborne.png
magick assets/icons/cachyborne.png -resize 128x128 -background '#11121a' -alpha remove BMP3:assets/icons/cachyborne.bmp
```
