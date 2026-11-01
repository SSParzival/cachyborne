# Cachyborne 0.8.2 — Midnight para CachyOS

**Español** · [English](README.md)

Bloodborne nativo en Linux x86-64, basado en bbport estable 0.7. El código del
juego se ejecuta en la CPU; el runtime C implementa servicios de PS4 y el renderer
C++ traduce comandos y shaders a Vulkan. Midnight es la interfaz GTK4/Libadwaita.
Necesitas tu dump descifrado de Bloodborne **1.09**, con la actualización y el DLC
combinados. No se distribuyen el juego, partidas ni binarios propietarios de AMD.

Esta versión revisa los ports comunitarios y añade protección de memoria también
en copias/clears GPU, tablas SRT nulas sin señales por draw, motion vectors finitos,
assets de **AMD FSR 4.1.2** y dos iconos propios con los colores de Sweet Purple.
Los menús del port funcionan en español e inglés; las voces son independientes.

[Notas de versión](docs/RELEASE_0.8.2.md) ·
[Validación y límites](docs/VALIDATION_0.8.2.md) ·
[Auditoría de forks](docs/COMMUNITY_AUDIT_0.8.2.md)

## Perfil de este PC

| Componente | Configuración |
| --- | --- |
| GPU | Radeon RX 9070 XT, RDNA4/GFX1201, Mesa RADV |
| CPU | i5-14600KF; P-Cores 0–11, E-Cores 12–19, verificados en este equipo |
| RAM | 16 GB; assets/cachés en disco, sin precarga completa en tmpfs |
| Imagen | FSR 4.1 Quality; escena 1706×960, salida 2560×1440 |
| Modelo | Automático por capacidades Vulkan; FP8 nativo en este equipo |
| Presentación | Mailbox, objetivo 144 FPS, generación de cuadros desactivada |
| Compilación | Clang, instrucciones nativas, ThinLTO y mold |

144 FPS es un límite objetivo, no una garantía por escena. La numeración de CPU
no es universal: revisa las máscaras al cambiar de máquina. Los hilos principales,
físicas, audio y render usan P-Cores; compilación/descompresión nombradas usan
E-Cores, respetando el cpuset permitido.

## Instalar y preparar

```sh
git submodule update --init --recursive
./cachyborne-install.sh
cachyborne-setup /ruta/al/app0 --performance-profile 144
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
cachyborne-setup /ruta/al/app0 --performance-profile 144
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
