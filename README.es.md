# Cachyborne 0.8.3 — Midnight para CachyOS

**Español** · [English](README.md)

Bloodborne nativo en Linux x86-64, basado en bbport estable 0.7. El código del
juego se ejecuta en la CPU; el runtime C implementa servicios de PS4 y el renderer
C++ traduce comandos y shaders a Vulkan. Midnight es la interfaz GTK4/Libadwaita.
Necesitas tu dump descifrado de Bloodborne **1.09**, con la actualización y el DLC
combinados. No se distribuyen el juego, partidas ni binarios propietarios de AMD.

Esta versión revisa los ports comunitarios y añade protección de memoria también
en copias/clears GPU, tablas SRT nulas sin señales por draw, motion vectors finitos,
assets de **AMD FSR 4.1.2** y dos iconos propios con los colores de Sweet Purple.
El writeback conserva todos los mips residentes y su relleno; los hooks corrigen
el tiempo de partículas a altos FPS y los eventos online sin datos.
Los menús del port funcionan en español e inglés; las voces son independientes.

La [auditoría de convergencia del 01-11-2026](docs/CONVERGENCE_AUDIT_2026-11-01.md)
añade validación DCX paralela con memoria acotada, corrección periódica de afinidad
de hilos de bibliotecas, caducidad del staging del host y publicación inmediata
de las finalizaciones Vulkan. Completa el catálogo español actual y los errores
de preparación de Midnight. Se validaron 26 pruebas C++, 239 pruebas Python y
FSR FP8 nativo en la RX 9070 XT; el informe detalla las pruebas de físicas y
viajes entre lámparas que siguen pendientes.

La [auditoría consolidada de seguimiento](docs/CONVERGENCE_AUDIT_FINAL.md)
documenta el hook nativo libdeflate comparado con el guest original, sombras de
contacto y PCSS opcionales, HUD de latencia de software (`BB_LATENCY_HUD=1`) y
herramientas de trayectorias y viajes repetidos sobre copias del guardado. Las
opciones «Sombras de contacto» y «Sombras direccionales suaves (PCSS)» están en
Midnight; `BB_CONTACT_SHADOWS=1` y `BB_PCSS=1` permiten activarlas por entorno.
Ambas empiezan desactivadas. El HUD no mide la presentación física en
pantalla. La equivalencia Havok completa, POM y las salas Lantern siguen sin
certificación; el informe detalla el trabajo pendiente.

[Notas de versión](docs/RELEASE_0.8.3.md) ·
[Validación y límites](docs/VALIDATION_0.8.3.md) ·
[Auditoría de forks](docs/COMMUNITY_AUDIT_0.8.3.md)

## Perfil de este PC

| Componente | Configuración |
| --- | --- |
| GPU | Radeon RX 9070 XT, RDNA4/GFX1201, Mesa RADV |
| CPU | i5-14600KF; P-Cores 0–11, E-Cores 12–19, verificados en este equipo |
| RAM | 16 GB; assets/cachés en disco, sin precarga completa en tmpfs |
| Imagen | FSR 4.1 Quality; escena 1706×960, salida 2560×1440 |
| Modelo | Automático por capacidades Vulkan; FP8 nativo en este equipo |
| Calidad | Texturas full, GT-VBAO (SSAO activado), sombras dinámicas, bloom y RCAS 0.35 |
| Presentación | Mailbox, objetivo 60 FPS, generación de cuadros desactivada |
| Compilación | Clang, instrucciones nativas, ThinLTO y mold |

El límite predeterminado es 60 FPS (16,667 ms); la estabilidad se mide en juego.
SMAA queda seleccionado y se ejecuta sin escalador; FSR utiliza antialiasing temporal.
Aberración cromática, motion blur y DoF quedan desactivados. `preset=1` significa
**Quality (1,5×)**; el modo nativo es `preset=0`. El perfil de 16 GB rechaza
1440p internos porque exige un pool de 9152 MiB; el predeterminado conserva 5056 MiB.
El audio usa reloj monotónico, cola SDL precargada y P-Cores. Activa
`BB_AUDIO_STATS=1` para medir vaciados de cola en hardware.

60 FPS es un límite objetivo, no una garantía por escena. La numeración de CPU
no es universal: revisa las máscaras al cambiar de máquina. Los hilos principales,
físicas, audio y render usan P-Cores; compilación/descompresión nombradas usan
E-Cores, respetando el cpuset permitido.

## Instalar y preparar

```sh
git submodule update --init --recursive
./cachyborne-install.sh
cachyborne-setup /ruta/al/app0 --performance-profile 60
cachyborne-launch --dry-run
cachyborne-launch
```

El instalador usa pacman con sudo para dependencias, compila y registra comandos,
menú y accesos directos. Con dependencias ya instaladas usa `--user`; con una
compilación lista, `--user --skip-build`. Conserva este checkout y `out/`: los
comandos de `~/.local/bin` apuntan aquí. Sin instalación puedes usar los scripts
`cachyborne-setup.sh`, `cachyborne-launch.sh` y `cachyborne-gui`.

La preparación valida identidad del ejecutable y archivos DCX, enlaza módulos y
parches, aplica mods en una vista separada y publica una generación solamente al
terminar. Un fallo conserva la generación anterior. Preparar no modifica el dump.
El lanzamiento normal consume el perfil preparado, sin reinstalar ni recompilar.
Repite la preparación después de cambiar ajustes, mods, parches o contenido.

## FSR 4.1.2 en Linux

```sh
# Extracción opcional: requiere herramientas MinGW/SPIR-V y un Proton utilizable.
bash tools/fetch_fsr412_assets.sh
cachyborne-setup /ruta/al/app0 --performance-profile 60
```

El helper descarga la DLL oficial **4.1.2.4555 del SDK 2.4.0**, verifica SHA-256 y
genera shaders/modelos locales en `~/.local/share/cachyborne/fsr4_412` (o XDG).
La DLL se usa durante la extracción; el juego ejecuta el modelo directamente en
Vulkan. La clave interna `fsr411` se conserva por compatibilidad. Si faltan assets
válidos, se informa y se prepara FSR3 como alternativa.

La comparación sintética a 1440p coincide byte a byte con la DLL en la prueba de
tres frames. Se detectaron diferencias en dos combinaciones 4K/ultrawide: esos
modos siguen siendo experimentales. No se afirma equivalencia universal ni una
ganancia de FPS por actualizar el SDK. [Detalles y otros escaladores](docs/upscaler.md).

## Ajustes, idiomas y contenido

Abre `cachyborne-gui`, guarda y prepara antes de jugar. Midnight reúne gráficos,
rendimiento, controles y bindings, mods/parches, multijugador, trofeos y diagnóstico.
Los iconos distinguen juego y configuración y se instalan sin descargar artwork.

El perfil recomendado usa textos españoles y voces inglesas. La selección de
voces redirige bancos existentes y audio de cinemáticas cuando hay una pista
compatible; conserva el original si no existe alternativa. El menú propio del
port tiene su selección español/inglés. No cambia los archivos del dump.

La selección de mods, parches, guardados y servidores se conserva al aplicar el
perfil recomendado. Los cambios de mods pueden generar una copia de guardados.
No confundas el co-op BBSC existente con el protocolo room de Lantern: no son
intercambiables. Los controles experimentales solo aparecen cuando tienen backend.

[Configuración](docs/CONFIGURATION.md) · [CachyOS y memoria](docs/CACHYOS.md) ·
[Mods](docs/MODS.md) · [Multijugador](docs/BBSC.md) ·
[Variables avanzadas](docs/ENVIRONMENT.md)

## Actualizar y comprobar

```sh
git pull --ff-only
git submodule update --init --recursive
bash scripts/check-all.sh
./cachyborne-install.sh --user --skip-build
cachyborne-setup /ruta/al/app0
```

`check-all.sh` compila el runtime y los tests GPU, ejecuta CTest/Python y comprueba
SPIR-V y Vulkan real. `build.sh` aplica los parches versionados del submódulo FSR;
no dependas de cambios locales sin registrar en terceros. Los assets AMD y los
cachés generados no se suben a Git.

Para investigar un fallo puedes activar la espera de diagnóstico en Midnight y
usar `bash tools/debug_attach.sh PID`. El helper guarda un informe privado y se
desconecta sin terminar el proceso por defecto. La suite y los smoke tests no
sustituyen recorridos prolongados, pruebas de jefes o validación de todos los mods.

[Arquitectura](RESEARCH_AND_ARCHITECTURE.md) · [Trabajo pendiente](docs/ROADMAP.md)
