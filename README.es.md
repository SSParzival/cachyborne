# Cachyborne 0.7.4 — Bisexual Midnight para CachyOS

[English](README.md) · **Español**

Cachyborne ejecuta Bloodborne de forma nativa en Linux x86-64. Se basa en
[bbport](https://github.com/deadinside28/bloodborne_pc): el código x86-64 original
se ejecuta directamente en la CPU; un runtime C sustituye los servicios del
sistema PS4 y el renderizador C++ traduce sus comandos y shaders a Vulkan.
El centro de control utiliza Python, GTK4 y Libadwaita.

Necesitas tu propio dump descifrado, con la actualización **1.09** combinada con
el juego base. El perfil objetivo es CUSA03173, Bloodborne + The Old Hunters.
El proyecto no distribuye el juego, partidas ni assets propietarios de FSR.

## Equipo objetivo y alcance

| Componente | Perfil precalibrado |
| --- | --- |
| Sistema | CachyOS, kernel 6.18 LTS, KDE Plasma 6 sobre Wayland |
| CPU | Intel Core i5-14600KF, 6 P-cores con HT y 8 E-cores |
| GPU | Radeon RX 9070 XT, RDNA 4 / GFX1201, 16 GB VRAM |
| Controlador | Mesa RADV; validación local con Mesa 26.2.5 |
| RAM | 16 GB DDR4; juego y cachés persistentes en disco |
| Imagen | Salida 2560×1440, escena 1706×960, FSR Quality |
| Objetivo | Límite de 144 FPS; el rendimiento depende de la escena |

El objetivo de 144 FPS no garantiza esa tasa durante toda la partida. Consulta
[las mediciones de esta versión](docs/VALIDATION_0.7.4.md). La compilación utiliza
instrucciones nativas de la CPU anfitriona; recompila al cambiar de equipo.

## Instalación y primera ejecución

```bash
git submodule update --init --recursive
./cachyborne-install.sh
cachyborne-setup "$PWD/CUSA03173" --performance-profile 144
cachyborne-launch --dry-run
cachyborne-launch
```

El instalador instala dependencias mediante pacman, compila con Clang, mold,
optimización nativa y ThinLTO, y registra comandos en `~/.local/bin`. Conserva el
checkout y `out/`. Con dependencias ya instaladas puedes usar
`./cachyborne-install.sh --user`; `--skip-build` registra una compilación existente.
Sin comandos instalados, usa `./cachyborne-setup.sh` y `./cachyborne-launch.sh`.

La preparación verifica identidad del ejecutable y archivos DCX, enlaza módulos,
compila parches y publica una generación de configuración solamente al terminar.
Si falla, conserva el perfil anterior. Repite setup después de modificar ajustes,
dump, mods o parches. El lanzamiento habitual ejecuta el plan Bash preparado sin
recompilar ni volver a escanear todos los assets.

## Textos en español y voces en inglés

Son selecciones independientes en **Imagen y sonido → Textos y voces**:

- **Idioma de interfaz y textos**: Español Latinoamericano (`BB_LANGUAGE=20`,
  archivos `dvdroot_ps4/msg/spaar`). Si falta, setup intenta Español España
  (`3`, `spaes`), después inglés (`1`). El selector muestra archivos completos.
- **Idioma de voces**: inglés por defecto (`BB_VOICE_LANGUAGE=eng`). También
  admite `esa`, `ese`, `jaj`, `ded`, `frf`, `iti` y `ptb`.
- La interfaz Midnight y los menús propios del port (incluido System y el panel de gráficos) usan español por defecto
  (`BB_MENU_LANGUAGE=es`).

El runtime redirige los bancos `dvdroot_ps4/sound/*_<idioma>.fsb` a su equivalente
del idioma de voces si existe y no está vacío. Conserva la ruta original si no
hay alternativa. Los mensajes, layouts y fuentes se resuelven aparte según `BB_LANGUAGE`, incluso
si el ejecutable europeo pide una ruta inglesa. No se modifica el dump. En las
cinemáticas elige el stream de audio correspondiente, tanto en inicio automático
como cuando el juego selecciona streams explícitamente; conserva la selección
original si la pista solicitada no está disponible. La apertura de CUSA03173
carece de etiquetas de idioma: se reconoce su estructura y tamaño para asignar
las pistas; la segunda pista de audio es la inglesa. La variante regional del
español depende también de los metadatos presentes en el MP4.

```bash
cachyborne-setup "$PWD/CUSA03173" --voice-language eng
BB_AUDIO_TRACE=1 cachyborne-launch
```

`BB_AUDIO_TRACE=1` registra cada redirección. Cambiar el idioma en la GUI exige
preparar de nuevo para actualizar el plan persistente.

## Perfil de rendimiento y escalado

El botón **Perfil recomendado para este PC (1440p · 144 FPS)** selecciona FSR 4
Quality, escena 1706×960, salida 2560×1440, límite 144, presentación Mailbox,
voces inglesas y textos españoles. Apaga motion blur, aberración cromática y
profundidad de campo. Conserva controles, rutas, HDR y selección de contenido.

```bash
bash tools/fetch_fsr4_assets.sh
```

Los assets propietarios FSR 4 son opcionales. La GUI comprueba los archivos
básicos del dump, los idiomas y los shaders seleccionados antes de preparar;
setup realiza después la validación completa. Si faltan o están dañados los
assets FSR 4, informa del problema y prepara **FSR 3.1** automáticamente.
FSR 4.1.1 y DLSS mantienen sus requisitos propios; consulta
[la guía de escaladores](docs/upscaler.md).

Para salida 4K elige 3840×2160 y mantén la escena dentro de 1920×1080. El preset
Performance corresponde a esa proporción 2×; Balanced no equivale a 1080p en 4K.
La resolución interna también puede editarse manualmente. Comprueba el tamaño
resultante y el presupuesto antes de preparar. No se ha certificado aquí un
recorrido completo a 4K/144 FPS.

El plan solicita RADV, `RADV_PERFTEST=sam`, anisotropía 16×, caché Mesa de 4G en
`~/.cache/cachyborne/mesa`, desactivación de capas implícitas, envío asíncrono y
3 hilos de grabación Vulkan. La memoria compartida utiliza `BB_GUEST_IN_PLACE=1`
y `BB_LAYER_MEMORY=1`, sujeto a las capacidades detectadas. ReBAR depende de
firmware y controlador; una variable de entorno no lo activa en el hardware.
RADV gestiona su compilador ACO. No hace falta instalar AMDVLK para este perfil.

La afinidad se solicita con `BB_CPU_PINNING=1`, `BB_P_CPUS=0-11` y
`BB_E_CPUS=12-19`. Los hilos de juego, audio y envío permanecen en P-cores;
ShaderCompile, PipeCacheIO, AssetWorker y Decompress usan E-cores. Se respeta la
máscara de CPU permitida por el sistema. Comprueba `lscpu -e` si cambias de equipo.
Los workers internos del controlador no reciben esta política por nombre.

GameMode es opcional (`--gamemode`) y necesita `gamemoderun`. Gamescope y los
planificadores sched_ext también son opcionales. No envuelvas todo el proceso
con `taskset 0-11` si quieres que sus workers puedan utilizar los E-cores.

## Memoria: límite para equipos de 16 GB

El dump ocupa unos 30 GB; no cabe en el tmpfs de aproximadamente 7,7 GiB. El
staging completo en RAM está desactivado por defecto. Si se solicita manualmente,
se comprueban capacidad y memoria disponible antes de copiar.

| Recurso | Perfil seguro |
| --- | --- |
| Pool directo (`BB_DMEM_MB`) | 5056 MiB, escena hasta 1920×1080 |
| Pool extra (`BB_DMEM_EXTRA_MB`) | 256 MiB |
| Caché de recursos (`BB_RENDER_RESOURCE_CACHE_MB`) | 256 MiB |
| Prefetch (`BB_MEMORY_WILLNEED`) | 0 |
| Compiladores de pipelines | 2 workers |

El nombre consumido por este runtime es **`BB_DMEM_MB`**. Una escena 1440p nativa
requiere un pool de 9152 MiB y se rechaza en el perfil de 16 GB incluso cuando
sysfs no permite consultar GTT. Los 16 GB de VRAM no sustituyen la RAM/GTT
necesaria para este modelo de memoria. Utiliza escalado para salida 1440p o 4K.

## Centro de control, controles y mods

Abre `cachyborne-gui`. La paleta Bisexual Midnight respeta el modo de alto
contraste. Incluye los 41 ajustes originales de bbport, 44 mapeos de teclado y
mando, diagnóstico, contenido y herramientas de cuentas. Los borradores se
guardan automáticamente; **Guardar como mis predeterminados** crea un perfil
personal. La validación CSS funciona también sin display en tests.

Con mando SDL se conservan las acciones del juego. El mouse-look nativo gira
la cámara directamente. Por defecto: WASD para moverse, clic izquierdo para
ataque, clic derecho para disparo, espacio para esquivar, Q para vial y Escape
para menú. Ajusta sensibilidad, inversión y cada mapeo en **Controles**.

En **Mods y parches**, activa capas y ordena su prioridad: la última capa activa
prevalece. Se prepara una vista mediante enlaces, sin sobrescribir el dump.
Conserva las fuentes de mods mientras uses esa generación. Los parches XML se
seleccionan y compilan durante setup. Consulta [MODS](docs/MODS.md).

```bash
python3 launcher/cachyborne.py desktop --desktop
```

Crea accesos separados para jugar y configurar con un icono original de sigilo
sobre luna de sangre, en SVG y PNG cuadrado. La ventana usa `cachyborne-play`
como identificador para que KDE/Wayland encuentre el mismo icono; X11 recibe
además el icono SDL. La GUI conserva las proporciones de la imagen.
`--download-icon` permite optar por el arte oficial: se encaja en un lienzo
cuadrado sin deformarlo y, si falla la red, conserva el icono anterior.

## Archivos y comprobaciones

Preferencias y perfil: `~/.config/cachyborne/`. Generaciones, partidas y logs:
`~/.local/share/cachyborne/`. Se respetan las rutas XDG. Las credenciales son
privadas; no deben incorporarse a distribuciones ni commits.

```bash
bash build.sh --test
bash scripts/check-all.sh
python3 scripts/smoke-game.py --seconds 15
```

La suite verifica C/C++, CTest, Python, SPIR-V y operaciones Vulkan reales. La
comprobación sintáctica Python usa AST en memoria y no escribe `__pycache__`.
La compilación y las pruebas que generan artefactos siguen necesitando un
`out/` escribible. La prueba de humo copia las partidas a una carpeta aislada,
exige una imagen GPU visible y comprueba que el proceso siga vivo.

[Configuración](docs/CONFIGURATION.md) · [CachyOS](docs/CACHYOS.md) ·
[Validación 0.7.4](docs/VALIDATION_0.7.4.md) · [Arquitectura](RESEARCH_AND_ARCHITECTURE.md)
