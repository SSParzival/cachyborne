# Integración de BBHOST — auditoría en curso

**Actualización 2026-11-05:** el perfil recomendado de 60 FPS ya publica
cadencia estable, margen de sueño adaptativo y ambos spins ociosos en cero.
Se mantienen entrada/salida 2560×1440, FSR 4.1 Native AA, preset 0 y generación
de frames apagada. La candidata pasó ventanas completas de 120 segundos en
Mensis real, Viejo Yharnam y Bosque Prohibido: 7.200 frames en cada una, p99
16,71 ms. La comparación A–B–B–A de Catedral redujo CPU observado un 12,5%.
Son barridos desde las lámparas, no certificación de mapas completos o combate.

El binario publicado anteriormente pasó **86 CTest, 308 Python**, runtime C, SPIR-V y Vulkan
(`out/native-60-defaults-check-all-fixed.log`). También pasa la implementación
THD instrumentada con ASan/UBSan en loopback, con paquetes grandes, división
por chunks completos y modo legacy. La utilidad de parámetros genera varias
tablas en un solo mod y se verificó una edición cargada por el juego.
El soak del binario publicado completó 15 ciclos en 1.235,4 s, con guardados
intactos y sin fallos de runtime/cierre. RSS creció 54,1 MiB y VRAM 162,0 MiB
entre primera y última vuelta, dentro de los límites; staging no creció.
La presentación física 1440p y la salida USB real tienen ventanas comprobadas,
incluida una nueva ventana de 600 s a 59,9913 FPS (peor 31,21 ms) y otra de
120 s con USB verificado (peor 16,73 ms). Un intento anterior detectó un
frame de 281,56 ms: sigue rechazado y bajo
investigación. No se ha creado la release final.

**Corrección 2026-11-05:** la aceptación completa de cierre de
`native-default-120-current` y `gpu-progress-integration-current` queda retirada.
El detector anterior no reconocía `Fault (signal N)` sin prefijo Host/Guest.
Ambos logs conservaban un fallo de `bb:GpuSignals` al detener la sesión. Sus
ventanas de rendimiento y etapas funcionales no prueban un cierre correcto.
Los resultados corregidos están en `out/<sesión>/acceptance-corrected.json`.
La nueva prueba de vida de interrupciones reproduce un use-after-free real;
la corrección ASan/UBSan pasa, y sus nuevas capturas de Byrgenwerth y Catedral de
120 segundos también pasan el gate corregido de cierre (véase al final).
Las secciones históricas que dicen «todos los gates pasan» describen el
resultado del verificador antiguo y quedan subordinadas a esta corrección.

**Estado: incompleto; el objetivo permanece abierto.** Esta revisión implementa
la asistencia de la barrera GX, controles/puntero, el reloj experimental y los
wrappers de replays/sincronización por tiempo de juego, además del perfil nativo
1440p verificado en una ruta y la retirada de chunks por finalización GPU. Siguen
sin certificar la aceptación completa de gameplay/VRR, las zonas exigentes,
los replays online extremo a extremo, todas las interacciones de ventanas de
memoria y la elisión de copias.
La matriz requisito por requisito está en `BBHOST_0.2.24_AUDIT.json`.

## Fuentes fijadas y diferencias de arquitectura

Fuente: [droogie/bbhost v0.2.24](https://github.com/droogie/bbhost/tree/v0.2.24),
commit `b0098e04b889e2e70b4af84be1ce809d7af3ff95`.
Se consultaron código e historial completos, además de los
[PR #199](https://github.com/droogie/bbhost/pull/199) y
[PR #201](https://github.com/droogie/bbhost/pull/201). Copia de investigación:
`out/bbhost-upstream`; metadatos de los PR: `out/bbhost-pr-{199,201}.json`.
Los 68 commits entre v0.2.22 y v0.2.24 se enumeran en
`out/bbhost-024-commits.txt`; eso no constituye aún un inventario certificado
de todo el ciclo v0.2.x.

* #201 modifica el sistema de trabajos GX del guest, no el `DrawPipe` de
  Cachyborne. El step de upstream `0x25b9980` corresponde a offset
  **`0x21b9980`**: Binary Ninja incorpora la base `0x400000`.
* El reloj upstream usa el paso de simulación medido, limitado al intervalo
  `[target, 1/30]`. No usa directamente el intervalo entre presentaciones Vulkan.
  Cachyborne ya actualiza varios efectos por delta medido; hay que comparar
  consumidores antes de añadir conversiones para evitar aplicarlas dos veces.
* `BB_GUEST_VRAM` ya existe. `=1` lleva también lecturas CPU por PCI BAR;
  disponer de 16 GB de VRAM no demuestra por sí solo que ese modo sea seguro
  ni que mantenga 60 FPS. Se implementó y midió la selección por rangos; fue
  más lenta que mantener los datos guest cacheados en CPU y los recursos del
  renderer en VRAM. El pool ajustado al crecimiento real de heaps es 5936 MiB
  a 1440p y entra en el presupuesto GTT local (véanse las mediciones al final).
* En #199 la elisión se activa por defecto en integradas. Upstream documenta
  regresión del coste de bookkeeping en discretas limitadas por CPU. No se
  asumirá que activarla en la RX 9070 XT mejora rendimiento.

## Implementación actual y diffs relevantes

| Archivos | Cambio y evidencia |
| --- | --- |
| `gpu/games/bloodborne/gx_job_barrier.{h,cpp}`, `gpu/games/bloodborne.cpp` | Step equivalente al guest; tras agotar su deque hace otro barrido y deja los trabajos robados en cola para que la barrera vuelva a llamarlo. Activo por defecto, `BB_GX_BARRIER_STEAL=0` deja la función original sin hook. Entrada de 17 bytes verificada. No se añade un mutex host alrededor de los locks propios de las deques ni de la ejecución de trabajos. |
| `gpu/shim/bbport_guest_code_hooks.{h,cpp}` | Retorno incondicional opcional para reemplazos completos de funciones void. El flag de salto existente se consume y borra; no servía como bandera permanente compartida para llamadas recursivas/concurrentes. El modo nuevo no tiene ese estado mutable. |
| `src/runtime_pad/bindings.c` | Movimiento mantiene el impulso hasta 150 ms entre teclas, emite reposo durante el hueco y recupera la rampa desde parado. En sprint gira sobre el círculo a 90 grados/70 ms; una inversión de un solo eje cambia al opuesto inmediatamente. El stick de cámara conserva su suavizado independiente. |
| `src/runtime_pad/bindings.c`, `launcher/cachyborne_options.py` | Siete acciones de menú configurables: Enter, RePág/AvPág, Inicio/Fin, F/R. Se filtran atajos de combate al leer menús; los botones físicos del mando se conservan. El Return del antiguo `key.cross` no vuelve a interactuar durante juego. Midnight genera sus controles desde el esquema ampliado. |
| `src/runtime_pad/prompts.c` | Los prompts usan asignaciones de menú cuando corresponde; su generación cambia al entrar/salir para invalidar texturas cacheadas. |
| `gpu/games/bloodborne_menu_pointer.cpp` | El clip RepositoryType del baúl responde a hover/click con L2/R2; no repite mientras espera el cambio de pestaña. Antes de activarlo verifica instrucciones del constructor que identifican vtable y campos. |
| `gpu/games/bloodborne_menu_hover.h`, puntero | La rueda bloquea el hover hasta alejarse más de 16 píxeles del escenario. Un pequeño movimiento de la mano no vuelve a seleccionar una fila recién desplazada. El click conserva prioridad. Estado compartido protegido entre hilo de eventos y guest. |
| `gpu/CMakeLists.txt`, `tests/` | Nuevos targets `gx-job-barrier-test`, `guest-hook-replacement-test`, `menu-pointer-test`; regresiones C de controles y prompts, pruebas de proyección de ajustes y verificación de offsets del retail. |
| `gpu/shim/bb_env_vars.def`, `docs/ENVIRONMENT.md`, `NOTICE` | Registro/documentación del switch y nuevos valores de controles; atribución GPL a upstream. |

Forma de los cambios centrales:

```cpp
// Barrera GX: las operaciones pop/steal son las del juego.
if (!ops.pop(self + 0x18, &job) && Sweep(self, ops))
    ops.pop(self + 0x18, &job);
// ejecutar job con el índice propio
if (assist && !Load<Address>(self + 0x38)) Sweep(self, ops);
```

```text
key.menu_confirm=Return
key.menu_prev=PageUp
key.menu_next=PageDown
key.menu_storage=Home
key.menu_inventory=End
key.menu_display=F
key.menu_status=R
```

Se conservan las fronteras del runtime C, los shims GPU, Midnight GTK4/Libadwaita
y los ajustes P `0-11` / E `12-19`. No se cambió el asignador ni se trasladaron
hilos a otros núcleos. El proyecto ya usa C++23 globalmente; los nuevos helpers
se verificaron también con Clang en C++20, sin rebajar el estándar de fuentes
existentes que necesitan C++23.

## Pruebas y límites de aceptación

* `pytest` no estaba instalado. Se creó el entorno local
  `out/bbhost-audit-venv`, con acceso a las dependencias GTK del sistema.
  `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1` evita un entry point global roto de
  `platformdirs.pytest_plugin`. Suite: **255 pruebas, 1.361 subtests**.
  Log: `out/bbhost-024-timing-python.log`.
* Diferencial contra la función original del ELF 1.09: **20.000 casos, cero
  diferencias** de llamadas, argumentos y deques con asistencia desactivada;
  activada, **8.730 barridos extra, 7.176 con trabajo robado**, sin pérdidas,
  pops ni ejecución extra durante ese barrido. La prueba conserva el código
  original como oráculo e instrumenta sólo pop, steal y TSC. Log:
  `out/bbhost-024-gx-differential.log`.
* El nuevo retorno de hook se prueba mediante código ejecutable real, antes y
  después de instalarlo, con ocho hilos y recursión. Las pruebas C de pad
  ejercitan sprint a 30/60/120 Hz, quick-turn, huecos de teclas, reinicio desde
  reposo, Enter de perfiles antiguos, separación de menús, prompts y conservación
  de botones de mando. El test de hover cubre jitter, radio y liberación permanente.
* `bash build.sh --skip-tests` y `bash scripts/check-all.sh` ejecutan la
  compilación Clang nativa/ThinLTO/mold, targets C/C++, suite Python y prueba
  Vulkan de envío/readback. El build.sh del árbol no tiene permiso ejecutable;
  se invoca mediante bash. `out` no es un build Ninja raíz: el equivalente
  válido es construir `cachyborne-tests` en `out/gpu` y ejecutar CTest.
  **38/38 tests CTest** con ELF retail disponible; el diferencial devuelve skip
  77 si no se proporciona `CACHYBORNE_TEST_ELF`.
* Sin errores de compilación ni advertencias del compilador en el build
  incremental inspeccionado. CMake sí emite avisos de políticas de dependencias
  existentes; no se afirma una salida sin ninguna advertencia de configuración.
* Smoke real: `python scripts/smoke-game.py --seconds 60 --out out/bbhost-024-smoke`,
  guardados copiados a un directorio aislado, hook instalado, tres capturas GPU
  válidas, 11 muestras de FPS, última **59,8 FPS**. Inspección de `final.png`:
  **pantalla de título**. No certifica trabajo GX durante gameplay, zonas
  exigentes, calidad del baúl, 1440p nativo ni reducción de potencia.
  Perfil de prueba existente: reconstrucción a 2560×1440; los valores de FSR
  existentes permanecen sin modificar.
* El build aplica los parches FSR-Vulkan ya presentes en `gpu/patches` al
  submódulo. Su árbol aparece modificado por esos parches existentes; no se
  creó un nuevo commit del submódulo.

Para reproducir el diferencial, extraer el ELF con `prepare.parse_self` sin
publicar bytes del juego y ejecutar:

```sh
CACHYBORNE_TEST_ELF=/ruta/eboot.elf out/gpu/gx-job-barrier-test
CACHYBORNE_TEST_ELF=/ruta/eboot.elf ctest --test-dir out/gpu --output-on-failure -L cachyborne
```

## Trabajo requerido pendiente

1. Comparar y portar el resto del ciclo v0.2.x mediante un inventario de mejoras
   funcionales; clasificar equivalentes existentes y elementos dependientes de
   la arquitectura GX/Vulkan distinta.
2. Asistencia ordenada de `DrawPipe` implementada y cubierta por
   `test_draw_pipe_steal.cpp`, CTest y el ensayo TSan previo. Pendiente: medir
   su impacto de CPU/potencia en Mensis y Catedral; ver continuación al final.
3. Completar aceptación de gameplay/VRR del reloj experimental. Auditar la
   segunda parte upstream (despertar del hilo de sonido y polling de usuario)
   dentro del inventario total. Backend/INI/entorno/Midnight y los consumidores
   YEBIS/ragdoll/LensSimu ya están implementados y probados contra código real.
4. Verificar manchas de sangre y emisión online extremo a extremo con un
   servidor y grabación reales. Los wrappers y las pruebas de cadencia variable
   contra las funciones guest ya pasan; la captura de gameplay sigue pendiente.
5. Completar ventanas de 16 MiB con lecturas GPU activas, retiro diferido de
   imports y elisión de copias, con pruebas Vulkan de sincronización y contenido.
6. Ruta explícita de 1440p/preset 0 sin FSR con VRAM BAR implementada;
   presupuestos y planes Python/shell unificados. Pendiente: aceptación
   de gameplay y adopción del perfil nativo predeterminado; ver continuación.
7. Verificar controles, rueda y baúl con interacción real; medir GX on/off en
   Mensis y Catedral, frametimes, CPU y consumo a 60 FPS. Certificar nativo 1:1
   y estabilidad usando escenas cargadas, no la pantalla de título.
8. Repetir las puertas de aceptación tras las integraciones restantes y emitir
   el reporte final completo. Este documento es un registro intermedio.


## Reloj y replays: implementación y evidencia actuales

Archivos nuevos: `gpu/shim/bbport_frame_clock.h`,
`gpu/shim/bbport_clock_consumers.h`,
`gpu/games/bloodborne/replay_time.{h,cpp}`. Integración en
`bbport_frame_rate.cpp`, ajustes en `bbport_settings.{h,cpp}`, esquema y
switch Midnight en `cachyborne_options.py`, `cachyborne_gui.py` y
`cachyborne_panels.py`. El esquema omite los controles ya colocados explícitamente
para no crear un segundo switch del reloj.

El paso medido del pacer se publica antes de las tareas de cada frame, incluso
con el reloj experimental apagado. Un atómico empaquetado mantiene juntos el
delta y el indicador de comienzo de un tick de 30 Hz. El acumulador pertenece
sólo al hilo principal. La opción experimental, apagada por defecto y aplicada
al reiniciar, gobierna los consumidores adicionales; los replays y el sync
siguen tiempo de juego por defecto, con `BB_REPLAY_STEP=0` y
`BB_CHR_SYNC_STEP=0` como diagnóstico. Se usa el tiempo de simulación del
pacer, como upstream; las presentaciones generadas no cuentan como simulación.

Se verificaron los hashes FNV completos de las funciones originales:

| Función | Offset Cachyborne | Hash del cuerpo |
| --- | --- | --- |
| PadManipulator sync | `0x1529470..0x15296c2` | `7e546384af3e3fc8` |
| ReplayManipulator update | `0x152c390..0x152c930` | `65b4ccfbbc05261a` |

Los wrappers llaman el código original mediante trampolines retornables,
publicados antes de escribir el salto de entrada. En frames sin tick canónico,
el original ve un countdown de 2 y después se restaura su valor; no se omite
el resto de su actualización. La primera muestra de un replay nuevo se toma
inmediatamente, como a 30 FPS. Los movimientos ordinarios se escalan por la
fracción del frame; los teletransportes de más de 10 unidades se conservan.

El reloj experimental escala el elapsed de YEBIS por frames de 30 Hz, entrega
el delta real al keyframe de ragdoll sólo cuando el campo conserva el default
1/30 y restaura ese campo al volver, y redirige LensSimu a `1/dt`. La integración
de ragdoll intercepta sólo el tail-call original en `0x6e464d`, dejando sus
checks de peso/cuerpos/step y stores de pose en el juego. Los pasos personalizados
y de Havok Behavior se mantienen. Las firmas de setter y tail-call y el
operando RIP-relative de LensSimu se comprueban contra el retail.

Pruebas nuevas y alcance:

* `test_chr_sync.cpp`: 100.000 estados a 30 y 100.000 convertidos, memoria y
  llamadas comparadas con el original; 36 secuencias completas (settings,
  60–1000 FPS y jitter 30–240). El default mantiene la cadencia de 6 paquetes
  por segundo de juego. La cuantización de las ventanas admite una muestra
  de borde y una diferencia temporal de hasta un frame. El sync sin conversión
  a 120 produce 24 paquetes/s, frente a 6 del original a 30.
* `test_chr_replay_manipulator.cpp`: otros 100.000 estados a 30 y 100.000
  convertidos; 18 grabaciones completas con 36 muestras cada una, incluyendo
  saltos, giro y variación de tasa. El final se mantiene alrededor de 10 s,
  dentro de un frame; el original sin conversión a 120 acaba en 2,5 s. La
  trayectoria se comprueba contra la referencia a 30 con el margen de movimiento
  de un frame original, no como identidad de cada posición a tasas distintas.
* `test_ragdoll_clock.cpp`: 20.000 drives del código original con los mismos
  argumentos/memoria y steps propios preservados, incluyendo valores especiales.
* `test_frame_clock.cpp`: acumulación de tiempo, jitter, valores inválidos,
  publicación coherente entre hilos e identidad/restore de consumidores.
* `test_frame_clock_hooks.cpp`: instala los hooks reales sobre el ELF. Entra en
  el hook real del main loop con RBX/RBP del pacer y comprueba que el delta
  medido llega al reloj, setter original/interceptado de YEBIS, drive original
  de ragdoll y operando real de LensSimu. CTest lo ejecuta en procesos separados
  con el reloj activado y desactivado: ambos pasan. No necesita una GPU.
* `test_guest_hook_replacement.cpp`: además comprueba el trampoline original y
  la propagación del valor de retorno en llamadas repetidas/recursivas/concurrentes.
* Ajustes, precedencia de `BB_FRAME_CLOCK=0|1`, persistencia y switch GTK se
  verifican en las pruebas existentes de settings, launcher y ventana real.

Logs: `out/bbhost-024-{sync,replay,ragdoll}.log`,
`out/bbhost-024-clock-hooks.log`, `out/bbhost-024-timing-python.log` y
`out/bbhost-024-timing-check-all.log`. La última verificación completa incluye
255 tests Python, 38 CTest sin skips, runtime C, compilación y readback Vulkan.

La captura real `out/bbhost-024-timing-gameplay/0` confirmó ambos hooks de
cadencia instalados pero terminó por timeout de carga, con FPS muy bajos.
Ese primer intento no aporta una captura válida de gameplay del reloj. La
repetición posterior descrita abajo sí llegó al mundo en ambos modos. El manifiesto confirma que
los guardados originales no cambiaron. Se observó presión reciente de memoria
/E/S del host y una compilación Rust ajena al proceso de pruebas con unos
4,4 GiB RSS. No se atribuye el fallo únicamente a esa presión sin una repetición
controlada, ni se presenta esa captura como aceptación. Se conserva íntegra
para investigar y repetir cuando haya presupuesto de memoria suficiente.


### Repetición controlada del reloj en gameplay

Tras terminar la compilación externa se observaron unos 9 GiB disponibles y
se repitieron ambos modos, sin compilar simultáneamente y con el puntero
apagado para que el ratón no alterase la selección de menú. Artefactos separados
`out/bbhost-024-timing-gameplay-repeat/{1,0}`; manifiesto y análisis en ese
mismo directorio. Se corrigió sólo la ruta relativa del campo `poses` del
manifiesto: el helper original presupone carpetas por FPS, mientras que esta
comparación usa carpetas por modo del reloj. Los CSV originales se conservan.

Los dos runs completaron la ruta de 8 s después de cargar el mundo, con
491 samples válidos cada uno y mapa `402718720`, sin muestras perdidas,
saltos de muestreo superiores a 100 ms ni fallos de ejecución. El modo 1
registró YEBIS/ragdoll/lens activos; el modo 0 no los instaló. Ambos registraron
replays y sync por tiempo del juego. Los últimos tres reportes de cada run
fueron 59,8 FPS; los reportes iniciales contienen carga y pausas. Se conservaron
los guardados originales, comprobados por hashes.

La comparación de trayectoria tiene posición inicial idéntica, recorrido
horizontal de referencia de 8,6967 m, diferencia horizontal máxima de
0,021124 m (0,2429 %) y vertical de 0,00000484 m. **No pasa el umbral del
0,1 % del verificador existente.** Un par de rutas no permite atribuir esa
diferencia al reloj ni certificar equivalencia de física: faltan repeticiones
de control y análisis de fase de muestreo. Se registra como aceptación básica
de ejecución de ambos modos, no como aceptación numérica de trayectoria,
VRR, Bloodstain online ni nativo 1.0x. El perfil de render sigue siendo el
preparado anterior con resolución interna 1706×960 y salida 2560×1440.

## Continuación: 1440p nativo, VRAM por BAR y pruebas fuera del escritorio

Se añadió una ruta **experimental y explícita** con
`runtime_env.BB_GUEST_VRAM=graphics`, `upscaler=off`, `preset=0`,
`render_res=2560x1440` y `output_res=2560x1440`. No se ha publicado un nuevo
perfil sobre la configuración personal ni cambiado aún el preset predeterminado.
La aceptación de 60 FPS estables en zonas exigentes sigue pendiente.

* `launcher/bbport_graphics_memory.py` comparte con `scripts/patches.py` los
  tamaños y direcciones de las dos entradas de la tabla retail 1.09. A 1440p
  quedan en **2719 + 1334 MiB**, rango físico
  **`42c00000-140100000`**. Se verifican también los bytes efectivos de
  `patches.bin`, incluyendo escrituras externas superpuestas: si cambian los
  heaps, el lanzamiento se rechaza en lugar de utilizar rangos obsoletos.
* `tools/gpu_capabilities.c --memory-budget` consulta el dispositivo Vulkan
  seleccionado, sin SDL ni ventanas. Exige GPU AMD discreta y un tipo
  DEVICE_LOCAL + HOST_VISIBLE + HOST_COHERENT con presupuesto verificable de
  `VK_EXT_memory_budget`. El preflight reserva **7125 MiB en VRAM** (heaps +
  3072 MiB para recursos adicionales) y **5867 MiB en GTT** (resto del pool,
  256 MiB extra y 512 MiB de margen). Es una comprobación conservadora de
  admisión; no reserva físicamente esos presupuestos para siempre.
* `check_memory_budget()` ya no permite saltarse la comprobación por la sola
  presencia de `BB_GUEST_VRAM=1`. El modo `graphics` rechaza backends host,
  lazy, rangos incoherentes y reservas extra que invaliden sus cálculos.
  Los planes Python y shell comprueban el presupuesto en el momento de lanzar.
  `BB_DMEM_MB` queda fijado al tamaño del perfil, de modo que un valor heredado
  de 5056 MiB no reduzca accidentalmente el pool nativo de 9152 MiB.
* `bbport_guest_memory{,_chunks}.cpp` exige que una asignación solicitada en
  VRAM use un tipo local coherente. Si la selección/asignación/exportación
  falla, detiene el proceso: no toma silenciosamente un tipo GTT que haría
  falsa la admisión del launcher. Este cambio no sustituye la auditoría
  pendiente de ventanas/imports con lecturas GPU activas.
* `scripts/isolated_game.py` y los runners de smoke/trayectoria usan
  **Gamescope `--backend headless --expose-wayland`**. Se eliminan las rutas
  DISPLAY/WAYLAND del escritorio antes de iniciar el compositor, se fuerza el
  cliente Wayland privado y audio `none`, se conserva el guardado mediante copia
  y se termina el grupo de procesos completo. Las capturas salen del renderer,
  sin capturar el escritorio del usuario. El runner vigila MemAvailable y PSI:
  exige 4 GiB al inicio y aborta si quedan menos de 2 GiB o si la presión completa
  de memoria supera 25 % durante el intervalo medido, de al menos cinco segundos.

El presupuesto Vulkan es dinámico y depende de otros procesos; la propia
[especificación de VK_EXT_memory_budget](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryBudgetPropertiesEXT.html)
lo define como guía cambiante, no como garantía de asignación. Por eso no se
promete eliminar para siempre cualquier fallo de memoria del sistema.

### Evidencia de esta continuación

* Build completo: `BB_BUILD_JOBS=2 bash build.sh --skip-tests`, log
  `out/bbhost-024-native-build.log`, correcto. Persisten avisos de políticas CMake
  de dependencias, sin nuevos errores/advertencias de compilador observados.
* Suite Python dentro del compositor privado, incluida la ventana GTK real:
  **265 pruebas y 1361 subtests**, log
  `out/bbhost-024-native-python-isolated.log`. La ejecución sin display omite
  únicamente esa ventana. `tests/test_native_vram.py` cubre concordancia con el
  ELF retail, límites de VRAM/GTT, modos incompatibles, overrides, parches
  superpuestos y aislamiento/presión de memoria.
* **39/39 CTest**, ELF retail habilitado, log
  `out/bbhost-024-native-ctest.log`. La puerta completa
  `out/bbhost-024-native-check-all.log` incluye runtime C (pad, memoria, etc.),
  compilación, tests C/C++/Python, validación SPIR-V y envío/readback Vulkan.
* Smoke privado anterior, perfil FSR existente: 20 s, tres capturas válidas,
  última muestra 59,8 FPS; `out/bbhost-024-isolated-smoke-2`.
* Smoke **1440p nativo sin FSR**: 25 s, capturas reales, escena y UI
  **2560×1440**, última muestra **59,6 FPS**, peor frame de esa ventana
  **32,6 ms**; `out/bbhost-024-native-vram-smoke/{game.log,final.png}`.
  Imagen inspeccionada: menú de título. Los logs confirman allocations por BAR
  para ambos heaps y para el crecimiento de A. Última muestra de memoria:
  **3193 MiB VRAM / 586 MiB GTT / 918 MiB RSS**. No se presenta la tasa media
  del título como prueba de estabilidad a 60 FPS en gameplay.
* Las primeras cargas de mundo, `out/bbhost-024-native-vram-gameplay` y
  `out/bbhost-024-native-vram-gameplay-repeat`, no alcanzaron la aceptación.
  La primera se detuvo manualmente por presión externa; la segunda activó
  la nueva protección PSI. Se observó un compilador externo en torno a 8 GiB
  RSS durante el segundo intento. Sus manifiestos confirman hashes de
  guardados originales intactos. No se atribuye todo retraso de carga a ese
  proceso sin un control completado. Los reportes de stall posteriores al cierre
  del compositor se conservan y no se clasifican por sí solos como GPU reset.

La asistencia DrawPipe, que estaba implementada en el árbol pero aún figuraba
pendiente en este informe, está cubierta por `test_draw_pipe_steal.cpp` y la
puerta CTest de 39 tests: consumidor ordenado excluyente, espera por objetivo,
wrap/backpressure, resolvers y wake/teardown. `Rasterizer::OnStageA()` distingue
el rol temporal de grabación; se restauran registros y userdata al terminar la
asistencia. Los logs previos `out/bbhost-024-steal-{unit,tsan,gameplay}.log`
permanecen disponibles. Siguen pendientes mediciones comparables de CPU/potencia
con Mensis y Catedral y los requisitos restantes de la matriz JSON.

El tercer intento, `out/bbhost-024-native-vram-gameplay-controlled`, tampoco
completó una ruta. El compilador externo volvió durante la prueba y el monitor
abortó al bajar MemAvailable de 2 GiB. Se observó el mapa 402718720 con estado
loading=1, pero no hay captura del mundo ni recorrido terminado que permitan
aceptar rendimiento. La última muestra de memoria del renderer antes del cierre
fue 5219 MiB VRAM / 555 MiB GTT / 897 MiB RSS; no es una medición de un estado
estable. El cierre del compositor produjo ErrorSurfaceLostKHR después de iniciar
la parada; se conserva el log y no se oculta ese diagnóstico. El manifiesto
confirma otra vez los guardados originales intactos. No se realizan más intentos
de gameplay en esta continuación bajo esta presión repetida.

Durante la última regresión parcial hubo un resultado transitorio de exit code
0 en `test_run_directly_executes_prepared_probe_and_propagates_exit` (esperado
7); log `out/bbhost-024-native-final-regression.log`. El launcher se estaba
editando durante esa ejecución, por lo que no se toma esa corrida como puerta
final de un árbol estable ni se afirma una causa confirmada. La prueba aislada
pasó después (0,11 s), y el mismo subconjunto completo volvió a pasar:
**55 pruebas, 3 subtests**, en 1,09 s, log
`out/bbhost-024-native-final-regression-repeat.log`. Se repite la suite Python
privada tras terminar todas las ediciones de código; no se oculta el intento
fallido intermedio.

Resultado de esa pasada final, con el código quieto: **265 pruebas Python y
1361 subtests, sin fallos ni skips, en 3,68 s**, incluyendo GTK en el compositor
privado. `git diff --check` correcto. Al cerrar no quedan procesos bb-probe ni
Gamescope de las pruebas. El alcance global del port permanece incompleto;
los pendientes de la matriz JSON no se convierten en aceptados por estos tests.

## FSR 4.1 a 1,0×: primera ruta completa aceptada

**El objetivo global sigue activo.** La puerta de una ruta nativa pasó; no se
ha dado por terminado el port de todo bbhost ni publicado la release final.
Los presupuestos de este apartado reemplazan los conservadores del apartado
anterior para el código actual.

### Corrección de memoria y selección probada

El pool ya no salta de 5056 a 9152 MiB al superar 1080p. Comparte con el
patcher las dos ampliaciones reales de la tabla retail: **5056 + 880 =
5936 MiB a 1440p**, conservando el espacio libre original; los heaps máximos
necesitan **8395 MiB**. Los logs 1.09 confirman 14 asignaciones de juego, cuyo
extremo pasa de **4589,125 MiB** (retail) a **5469,125 MiB** (1440p): la
ampliación calculada conserva los **466,875 MiB** restantes del pool retail.
Los 256 MiB del renderer siguen fuera del pool que ve el juego.

Con el margen del launcher, el presupuesto nativo completo es **6704 MiB de
GTT**, inferior al límite local; se elimina así el bloqueo artificial de la
resolución nativa. Se siguen rechazando presupuestos ausentes o insuficientes
y parches de heaps incompatibles. No se promete capacidad ilimitada si otros
procesos agotan RAM/VRAM.

La comparación de ubicación dio resultados distintos:

| Memoria guest | Evidencia observada con FSR 4.1 Native AA |
| --- | --- |
| Ambos heaps gráficos por BAR | 10–11 FPS; hilo de render 70–75 ms. `out/fsr411-native-gameplay-1` |
| Sólo GraphicsPrivate por BAR | Aproximadamente 44–55 FPS antes de optimizar índices. `out/fsr411-native-private-1` |
| GraphicsPrivate por BAR e índices optimizados | Primer tramo 55–58 FPS; últimos reportes 59,8. `out/fsr411-native-private-optimized` |
| Guest cacheado en CPU; recursos y mirrors GPU en VRAM | Ruta de 30 s aceptada, **59,824 FPS**. `out/fsr411-native-cached-verified` |

Estas corridas sirven para escoger la ruta funcional de este PC; no son un
experimento de potencia ni una equivalencia numérica A/B de todas las escenas.
El perfil elegido conserva unos **4262 MiB de VRAM**, **4354 MiB GTT** y **1127 MiB RSS** al final. VRAM sigue alojando los
recursos y las copias del renderer; poner además los datos que lee la CPU en
BAR perjudicó claramente esta prueba. La opción experimental
`BB_GUEST_VRAM=graphics-private` queda disponible con rangos/presupuestos
verificados; no es el valor predeterminado.

### Optimización y pruebas

`gpu/upscale/motion_history.h` y `gpu/render/rasterizer.cpp`: el perfil
`BB_SECTIONS` identificó rescans de índices de hasta **87 ms**. Las mallas
cargadas juntas vencían juntas cada 32 cuadros. Ahora la primera comprobación
se distribuye por hash y las siguientes mantienen el máximo de 32 cuadros;
se toma una sola copia secuencial al host y se calcula rango/hash de esa misma
instantánea, evitando dos recorridos por BAR. La prueba de 512 mallas comprueba
el límite de antigüedad y la distribución; índices de 16/32 bits, restart y
bases negativas conservan sus resultados. Prueba Clang C++20 con
`-Wall -Wextra -Werror`: correcta (`out/fsr411-native-motion-unit.log`).

`frame_stats.cpp` añade `monotonic_ns` al CSV, compatible con las anclas de pad
y poses. `scripts/native_acceptance.py` evalúa **todos** los cuadros de la
ventana de replay: configuración 1440p/1,0×, backend FSR 4.1 real sin fallback,
generación apagada, ventana completa, muestras de mundo válidas, promedio,
p99, peor cuadro, ausencia de faults y replay terminado. Sus ocho pruebas
rechazan también título/loading, muestras perdidas, un tramo final rápido que
sustituya la ruta completa y CSVs antiguos sin ancla absoluta. Un último registro
stdio truncado sólo se admite cuando un timestamp completo posterior ya prueba
que acabó la ventana medida; el artefacto original se conserva.

Resultado real en `out/fsr411-native-cached-verified/acceptance.json`:

* **1795 cuadros y 1794 poses** en la ventana de 30 s, mapa `402718720`.
* **59,824 FPS**, **p99 16,73 ms**, **máximo 16,80 ms**.
* Backend **FP8 t2160_m0**, entrada/salida **2560×1440**, preset **0**,
  generación de cuadros **off**, `BB_DMEM_MB=5936`.
* Todos los checks de esa ruta pasan; guardados originales intactos.
* Benchmark Vulkan separado: 300 cuadros de FSR 4.1 FP8 nativo,
  **0,652 ms/cuadro GPU** (`out/fsr411-native-fp8-bench-300.log`), con entrada
  sintética. No sustituye la medición de gameplay.

El valor predeterminado nuevo y el preset recomendado de 60 usan 1440p,
preset 0 y memoria guest cacheada; conservan los recursos GPU en VRAM y las
afinidades P/E. La configuración personal ya preparada **no se reemplazó**.
Candidato reproducible: `out/fsr411-native-cached-profile.json` (audio/ventana
configurados para pruebas, no es la publicación final para jugar).

Compilación nativa/ThinLTO/mold y puerta completa:
`out/fsr411-native-{motion-build,timestamp-build,check-all}.log`.
**39/39 CTest**, runtime C y readback Vulkan correctos. Tras cambiar los defaults,
la suite Python se repitió dentro del compositor privado: **275 pruebas,
1361 subtests, sin skips**, `out/fsr411-native-python-isolated.log`; el código
real de salida del proceso pytest quedó en `out/fsr411-final-pytest-exit.txt`
(`0`), pues Gamescope no propaga necesariamente el código del hijo. El antiguo
log `out/bbhost-024-native-python-isolated.log` fue reutilizado al migrar el
default y contiene un assert desactualizado del pool 5056: queda como intento
intermedio; el registro final válido es el de prefijo `fsr411-native`.

### Fuentes adicionales y siguientes puertas

`docs/NATIVE_FORKS_AUDIT.json` fija las cabezas inspeccionadas de bbhost,
bbport, ayozetr, SrShadowy, Supermedo, bloodborne-runtime y Mrsuss60. No se
afirma haber revisado todos sus diffs ni se usa popularidad como prueba de
calidad. bbhost sigue en el commit de v0.2.24 en esta inspección. Se revisó
además el diff concreto
[bloodborne-runtime e2073260](https://github.com/bloodborne-native/bloodborne-runtime/commit/e207326050a31ccd9e09d5d56bd1cc275816cbd8)
(formatos GCN reservados que deben tomar el camino de descriptor inválido):
la aserción antigua todavía existe aquí; port y tests exhaustivos quedan
pendientes. También se identificó el cambio reciente de prioridad de teclas en
[Mrsuss60 8ea9aa37](https://github.com/Mrsuss60/bloodborne_pc_windows_port/commit/8ea9aa3744c6e59ada2ed041c65e524c0a2837bb)
para compararlo con los controles actuales.

Siguen abiertas: Mensis/Catedral y pruebas prolongadas, audio con backend SDL
activo aislado, menús/baúl reales, VRR, replays/online extremo a extremo,
stress de publicación de tablas con lecturas activas,
ventanas de 16 MiB y elisión, resto del ciclo bbhost, commits temáticos y release.
No se declara que el juego corra perfecto por haber pasado una sola ruta.

Cierre de esta etapa: comprobación del candidato con `launch --check-memory`
correcta; **39/39 CTest** repetidos sobre el ELF 1.09, sin skips
(`out/fsr411-native-final-ctest.log`). Captura correspondiente a la ruta
aceptada: `out/fsr411-native-cached-verified/60/final.png` (2560×1440). No quedan
procesos de juego ni compositores de las pruebas. Los commits y la release se
reservan para cuando pase la auditoría completa requerida, no para presentar
esta aceptación de una ruta como fin del objetivo.


### Retirada de chunks protegida por la finalización GPU

Se confirmó en el código local que `DestroyRetiredChunks()` destruía buffers y
memoria tras 3 segundos de reloj de CPU. Ese plazo no garantiza la finalización
de comandos Vulkan: [vkFreeMemory exige completar los envíos que referencian la
memoria](https://docs.vulkan.org/refpages/latest/refpages/source/vkFreeMemory.html).

`BufferCache::Maintain` ahora procesa los unmaps y sus actualizaciones de tabla
antes de elegir el tick de retirada. Mueve las vistas antiguas a un callback del
scheduler; `DestroyRetiredChunks` captura el lote concreto y difiere su liberación
hasta la finalización de ese tick. El callback libera primero las vistas, luego
los buffers y las asignaciones. Capturar el lote al programarlo evita que un
callback antiguo libere accidentalmente chunks retirados más tarde.

La prueba nueva `tests/test_guest_memory_retirement.cpp` usa el asignador real
DMA-BUF y dos bloques de 16 MiB. Un semáforo privado, señalado desde CPU, mantiene
pendiente una lectura GPU durante más de los 3 segundos: no hay espera ocupada
GPU, ventana ni audio. Se verifican los datos completos de ambas lecturas, la
retención mientras el envío está pendiente, la independencia de los lotes y el
orden de destrucción, exactamente una vez. Pasa en RX 9070 XT/RADV 26.2.5:
`out/guest-retirement-test.log`. Compilación de la prueba con `-Wall -Wextra
-Werror` y compilación completa correctas (`out/guest-retirement-test-build.log`,
`out/guest-retirement-full-build.log`).

Esta prueba cubre la vida de las asignaciones; todavía no demuestra todas las
interacciones de publicación de tablas paginadas de ventanas de memoria. La
parte de elisión de copias de #199 y el soak prolongado siguen pendientes.

La puerta completa posterior al cambio de retirada pasa: **40/40 CTest**, 275
pruebas Python, runtime C y lectura Vulkan (`out/guest-retirement-check-all.log`,
código del hijo en `out/guest-retirement-check-exit.txt`: 0). `pytest tests/`
se ejecutó además de forma explícita en el compositor privado: **275 pruebas y
1361 subtests**, sin skips (`out/guest-retirement-pytest.log`, código 0). Estos
registros anteceden a la corrección adicional de claves de vistas descrita a
continuación; sus comprobaciones finales se registrarán aparte.

El script de viajes `scripts/stress_warp_soak.py` ahora usa el mismo aislamiento
headless/sin audio y la vigilancia de presión RAM de las capturas. Mide RSS/DRM
del PID real de bb-probe, no del compositor, y admite el pool calculado del perfil
(5936 MiB en nativo 1440p), en lugar de exigir 5056 MiB. La puerta de aceptación
sigue exigiendo 15 ciclos y límites de crecimiento; un diagnóstico corto no la
aprueba.


### Claves de vistas sin alias tras 4096 asignaciones

Hallazgo local adicional durante la auditoría de memoria: `Chunk::index` cuenta
creaciones durante toda la sesión, pero las tres rutas de enlace lo reducían
módulo 4096. Aunque hubiera sólo dos chunks vivos, los índices 0 y 4096 podían
reutilizar el mismo buffer; retirar uno también eliminaba la vista del otro.

`gpu/buffers/guest_chunk_views.h` conserva las vistas por índice completo.
`buffer_residency.cpp` usa esa caché en las tres rutas y `buffer_maintain.cpp`
extrae únicamente el índice retirado, transfiriendo su propiedad al callback.
La prueba GPU ahora asigna dos chunks reales con índices 0 y 4096, verifica que
sus vistas son distintas y estables, que extraer 0 conserva 4096 y que las dos
lecturas completas y retiradas siguen siendo correctas. No se simulan los datos
ni la finalización GPU.

Compilación final completa: `out/guest-retirement-final-build.log`.
Puerta completa final: `out/guest-retirement-final-check-all.log`, código **0**
en `out/guest-retirement-final-check-exit.txt`: **40/40 CTest**, **275 pruebas
Python**, runtime C y readback Vulkan. El stdout de cada prueba está preservado
en `out/guest-retirement-final-ctest-details.log`, incluida la comprobación
explícita de índices 0/4096. La prueba nueva compila con `-Wall -Wextra -Werror`.

Diagnóstico offscreen anterior a esta corrección de claves:
`out/guest-retirement-warp-diagnostic/result.json`: 2 ciclos, 5 cambios de mapa,
194,228 s; render/salida 2560×1440, preset 0, pool 5936 MiB. Entre los dos
regresos al Sueño: RSS **+5,129 MiB**, VRAM **+142,262 MiB**, staging **+0 MiB**
(144 MiB). Los límites de crecimiento pasan y los guardados originales están
intactos. `accepted=false` es correcto: faltan los 15 ciclos de la puerta
prolongada. Este diagnóstico no es evidencia de gameplay de la corrección
posterior de claves; esa versión se mide aparte.

### Formatos GCN reservados y nueva validación nativa

La captura posterior a las claves por índice completo pasa el analizador:
`out/guest-retirement-native-verified/acceptance.json`, 1.794 frames y poses,
**59,813 FPS**, p99 **16,74 ms**, peor frame **19,72 ms**. Backend real
FSR 4.1 FP8 `t2160_m0`, entrada/salida 2560×1440, preset 0, sin generación de
cuadros y con guardados originales intactos. Sigue siendo una ruta de 30 s.

Se portó la corrección de formatos reservados de
[bloodborne-runtime e2073260](https://github.com/bloodborne-native/bloodborne-runtime/commit/e207326050a31ccd9e09d5d56bd1cc275816cbd8):
15, 23–31, 42 y 43 pasan a `FormatInvalid`. Los targets de color inválidos se
desactivan; ambas rutas de inputs de vértices omiten formatos inválidos. Esto
evita emitir atributos Vulkan `eUndefined` en la preparación o reinterpretar
un descriptor inválido como float4 en la ruta regular. `PipelineKeyVersion=8`
descarta las descripciones de inputs serializadas con la conducta anterior.

`tests/test_reserved_formats.cpp` recorre los 64 encodings de datos y los 16
encodings numéricos, conserva los remaps de formatos reales y prueba el lowering
de cargas/escrituras formateadas typed/untyped del formato 15 para los ocho
tipos numéricos de buffers. La biblioteca anterior falla con código **23** en
`LowerBufferFormatToRaw` (`out/reserved-formats-before.log`). La candidata
compilada por separado pasa (`out/reserved-formats-after.log`): las cargas se
convierten en cuatro ceros y las escrituras se eliminan sin operaciones de
memoria. La batería completa y la captura del juego de esta corrección se
registrarán cuando terminen; no se heredan los resultados del binario previo.

El intento `out/guest-retirement-warp-15/result.json` **no aprueba** el soak:
se detuvo tras cuatro ciclos por el umbral de 2 GiB disponibles durante una
compilación simultánea. Además se había lanzado sin perfil explícito y usó el
predeterminado escalado (1706×960/preset 1), no el nativo requerido. Los guardados
originales siguen intactos. La repetición debe usar
`--profile out/fsr411-native-cached-profile.json`, sin compilación simultánea.

La auditoría de sonido también confirmó una pieza faltante: el wake de BBHOST
en `engine/sound_wake.cpp` llama una vez por frame al hilo MagicOrchestra y
conserva como timeout su periodo original de 33.333 us. En el ELF local coinciden
el setup en offset `0x2801373`, el call en `0x280137d` (destino `0x208bac0`) y
los cinco dígitos del periodo en `0x4733c51`; el polling de usuario conserva
33.000 us en `0x21629f1`. Evidencia:
`out/bbhost-sound-wake-retail-audit.json`. El sink SDL de Cachyborne no equivale a
despertar este productor guest. Esta integración y su prueba de audio siguen
pendientes; no se certifican mediante capturas con audio desactivado.

Puerta completa de la corrección de formatos: **41/41 CTest**, sin skips,
**275 pruebas Python**, runtime C y readback Vulkan, código **0** en
`out/reserved-formats-check-exit.txt`; log
`out/reserved-formats-check-all.log`. Identidad del binario y biblioteca:
`out/reserved-formats-build-sha256.txt`. La compilación amplia de objetos dejó
una advertencia preexistente de `memcpy(InfoPersistent)` en `common/serdes.h`;
no se afirma que todo el código existente compile sin advertencias.

La primera captura nueva (`out/reserved-formats-native-verified`) se detuvo por
presión de memoria del host superior al 25 %, con guardados originales intactos.
El reintento `out/reserved-formats-native-retry` sigue preparando la copia de
archivos; su proceso se verificó vivo, en espera de IO/escrituras del kernel.
Se observaron otros procesos `rustc`/`ld.lld` activos, fuera de esta tarea, y
presión de IO elevada. No se alteró el límite de protección ni se reinició el
driver mientras seguía activo. La validación de gameplay y el soak nativo de
esta versión permanecen pendientes; el resultado anterior de 59,813 FPS no se
transfiere automáticamente al binario nuevo.

Actualización: el reintento terminó con código **1**, también por la protección
de presión de memoria, tras la espera de IO; los guardados originales siguen
intactos y no quedan procesos de juego/compositor. Después del marcador de
parada del harness apareció además un `Host fault` en
`CopyFilterThread::_M_run`, biblioteca +`0xb07ab0`, dirección `0x8`.
`addr2line` lo ubica en `raster_dispatch.cpp`: el singleton inicia un hilo
detached sin mecanismo de stop/join antes de destruir sus colas e historial.
La vida de ese worker requiere corrección y prueba de cierre antes de la
siguiente captura. El fallo queda conservado en
`out/reserved-formats-native-retry/60/game.log`; no se oculta por ocurrir tras
la parada ni se considera aceptado el gameplay de esta versión.

### Cierre de los workers y tablas de escrituras GPU

El worker de copias ahora pertenece al `Rasterizer`, usa `std::jthread` y se
detiene/junta antes de destruir sus jobs, historial y los buffers guest. La
parada cambia el valor esperado por `atomic::wait`, sin publicar jobs ficticios.
La prueba real de hashing e historial cubre 1.000 carreras de arranque/cierre,
espera inactiva y parada durante un check con otro job pendiente. ASan/UBSan y
TSan pasan (`out/copy-filter-{asan,tsan}.log`).

La captura `out/copy-filter-native-verified` se detuvo por menos de 2 GiB de
RAM host disponibles, conservando los guardados originales. Tras la orden de
parada apareció otro fallo: `NewestGpuWrite`, biblioteca +`0x9ac1fe`, durante
una lectura de vértices en el hilo de dibujo. Las tablas globales de ticks se
construían al primer uso y sus destructores podían liberar sus directorios
antes de que el presenter global juntara sus hilos. Se mantienen ahora durante
toda la vida del proceso, como las hojas dispersas que ya usaban: una única
asignación de los directorios y los granules, sin crecimiento por frame o mapa.

`tests/test_write_ticks_lifetime.cpp` registra un consumidor antes de construir
las tablas y las consulta desde un hilo durante `atexit`. Con el header de
HEAD, ASan reproduce **heap-use-after-free**; con la corrección pasa ASan/UBSan.
Logs: `out/write-ticks-{before,after}-asan.log`. Esta prueba ejercita las tablas
reales, sin sustituir la consulta por un mock.

El analizador nativo rechaza ahora fallos de host/guest, assertions y pérdida
de dispositivo también después del marcador de parada; conserva la excepción
de pérdida de superficie esperable al cerrar el compositor. Prueba de regresión
incluida en `test_native_acceptance.py`.

Puerta completa de esta versión: **43/43 CTest**, sin skips, **276 pruebas
Python**, runtime C y readback Vulkan, código **0** en
`out/write-ticks-check-exit.txt`; log `out/write-ticks-check-all.log` e identidad
en `out/write-ticks-build-sha256.txt`. La captura mencionada precede la corrección
de tablas. **La aceptación de gameplay de este binario sigue pendiente**, al
igual que las zonas exigentes, soak, VRR, audio/online y release final.

### Productor de sonido por frame y nueva ruta nativa

Se integró `src/engine/sound_wake` de BBHOST mediante
`gpu/shim/bbport_sound_wake.{h,cpp}`. Con `BB_FRAME_CLOCK=1`, el pacer notifica
al productor MagicOrchestra antes de esperar por el limitador. Su sleep espera
una generación nueva o el timeout original; los frames acumulados durante una
pasada se consumen juntos. Se conservan los dígitos `33333` del timeout y los
`33000` us del polling de usuario. `BB_SOUND_WAKE=0` conserva el sleep original;
`BB_SOUND_WAKE_LOG=1` muestra contadores cada 300 frames de simulación. Los
frames generados o presentados no notifican al productor.

El observador del pacer comienza en `image+0x2034776`, después de los seis
bytes que ya usa la captura offline. El instalador de hooks ahora comprueba
rangos exactos: permite hooks contiguos y rechaza también solapamientos dentro
de spans mayores de 16 bytes. Las pruebas ejecutan ambos observadores sobre
el prologue retail y el CALL real del sleep, con código original de continuación
reemplazado sólo para regresar al test. Verifican rechazo de bytes/configuración
incorrectos sin instalación parcial, pending wake, despertar de un wait real,
timeout y desactivación. ASan/UBSan y TSan pasan la prueba del productor/consumidor.

Puerta completa: **45/45 CTest**, sin skips, **277 pruebas Python**, runtime C y
readback Vulkan, código 0 en `out/sound-wake-check-exit.txt`. Log y hashes:
`out/sound-wake-check-all.log`, `out/sound-wake-build-sha256.txt`.

La captura inicial `out/sound-wake-native-verified` se detuvo deliberadamente:
gamescope había sustituido el backend dummy solicitado por PipeWire. La ruta
de prueba ahora fija ambos hints SDL dentro del comando hijo, después de la
reescritura de gamescope, y exige confirmación `audio backend dummy` en el log.
No se acepta aquella captura. Los guardados originales permanecen intactos.

La repetición `out/sound-wake-native-dummy-verified/acceptance.json` **aprueba**
la ruta completa de 30 s: **1.795 frames**, **59,823861 FPS**, p99 **16,73 ms**,
peor frame **16,77 ms**, FSR 4.1 FP8 `t2160_m0`, entrada/salida 2560×1440,
preset 0, sin frame generation, reloj experimental activo y cierre sin fallos.
`60/final.png` muestra gameplay en Yharnam Central; no es la pantalla de título.
En los seis reportes dentro del replay, transcurren **1.500 frames y 1.500
pasadas de sonido**, con **cero timeouts nuevos**. El PCM del run contiene
28.549.120 muestras float, 27.125.158 no nulas, cero no finitas, pico 0,18527.
Evidencia: `audio-analysis.json`. Esto verifica actividad del productor y audio
digital, no cada sonido de ataque esperado ni la calidad del dispositivo físico.

La puerta anterior precede los últimos cambios sólo Python del aislamiento y
de la comprobación de fallos tras el cierre del soak. Sus pruebas dirigidas
pasan; no se atribuye a esa puerta una ejecución posterior que no ocurrió.
El soak de **15 ciclos** se lanzó con perfil nativo explícito y reloj activo:
`out/sound-wake-native-warp-15`, perfil
`out/sound-wake-native-soak-profile.json`. Mientras no termine y apruebe
`result.json`, permanece pendiente. Antes de relanzarlo hay que inspeccionar
el proceso existente, no inferir que terminó por este registro.

Actualización del soak: terminó tras **194,1 s**, **cuatro cambios de mapa y un
ciclo completo**, detenido por menos de 2 GiB disponibles en host. Resultado
`accepted=false`; runtime y cierre sin fallos, guardados originales intactos.
No aprueba la puerta de 15 ciclos. Se observaron compilaciones Rust externas
consumiendo varios GiB; no se detuvieron ni modificaron esos procesos.

### Prioridad opcional de la última tecla de movimiento

Se comparó el commit `8ea9aa3744c6e59ada2ed041c65e524c0a2837bb` de
`Mrsuss60/bloodborne_pc_windows_port`: añade prioridad a la última tecla
opuesta y reinicia la magnitud en una inversión de 180 grados. Ese reinicio
conflicta con el impulso/giro rápido BBHOST ya probado aquí. Cachyborne integra
la prioridad como `key_opposites=last`, conservando `neutral` por defecto y
el impulso de sprint. Al soltar la tecla más reciente se recupera la anterior
aún pulsada; pulsaciones simultáneas dentro del mismo poll se anulan. Cada eje
lleva su propio estado; el stick de cámara conserva la política neutral.

La opción aparece en controles de Midnight y en el esquema compartido. El
runtime lee el INI al arrancar. Las pruebas C cubren 30/60/120 Hz, magnitud de
sprint, inversión, recuperación, empate y cámara; se conservaron las pruebas
de mandos existentes. La validación Python cubre ambos valores, rechazo de
valores desconocidos y el contrato del launcher preparado, que no reescribe
el INI al lanzar. La prueba GTK real selecciona y recoge las dos opciones.

Puerta: **45/45 CTest**, **279 pruebas Python**, runtime C y readback Vulkan,
código 0 en `out/key-opposites-check-exit.txt`. Tras el último ajuste de textos
del selector, pasan por separado **10 pruebas Midnight**, incluida la ventana
GTK real (`out/key-opposites-gui-all.log`, código 0). Hashes del binario actual:
`out/key-opposites-build-sha256.txt`. Aún no se transfiere la captura del binario
anterior a éste.

La siguiente prueba nativa de 15 ciclos tiene un monitor de admisión real,
`out/wait-key-opposites-native-soak.py`, que exige 8 GiB disponibles estables
durante 20 s antes de iniciar `out/key-opposites-native-warp-15`. La espera
máxima es 1.800 s; permanecen las protecciones originales durante el juego.
Su estado debe verificarse en el proceso/session y en
`out/key-opposites-native-soak-admission.log`; este texto no prueba que siga vivo
ni que haya arrancado o aprobado el soak.

### Viento: ritmo original y fuerza independientes del framerate

La comparación con `engine/wind_pace.h` y `engine/frame_rate.cpp` encontró
otra pieza faltante. En el modo GX predeterminado (`+0x100 == 1`), el juego
avanza el viento 1/60 por cada frame de 30 FPS: medio segundo de viento por
segundo de simulación. Cachyborne conservaba el paso fijo, que a 60 FPS duplica
ese ritmo. La carga tardía de fuerza tomaba además el slot de tarea que aquí
ya sigue dt, reduciendo la curvatura a tasas mayores.

Se portó la corrección emparejada: `image+0x2313d8d` toma la mitad del delta
medido; `image+0x23149ea` toma el 1/60 original en `image+0x4929e70`. Ambas
instrucciones y la constante deben coincidir antes de cambiar ninguna.
`BB_WIND_PACE=0` conserva la conducta anterior. Se activa con los hooks de
framerate desbloqueado, independientemente del reloj experimental. Su alcance
es el modo GX predeterminado, como upstream; no se certifican otros modos.

La regresión nueva ejecuta las instrucciones instaladas en el ELF retail tras
pasar el delta por el hook real del pacer. El binario previo falla en el paso
de viento (`out/wind-before.log`); el candidato pasa con reloj nativo activado
y desactivado, con el switch de viento desactivado y con una instrucción de
fuerza deliberadamente distinta, que debe dejar también intacto el paso de
tiempo. Sólo las continuaciones de las cargas se reemplazan por un retorno al
test; no se confunde esto con ejecutar toda la función de viento en gameplay.

Puerta completa: **47/47 CTest**, sin skips, **280 pruebas Python**, runtime C
y readback Vulkan, código 0 en `out/wind-check-exit.txt`. Evidencia:
`out/wind-check-all.log`, `out/wind-targeted.log`,
`out/wind-build-sha256.txt`. Gameplay del nuevo binario y ritmo visual del
viento siguen pendientes. La ruta aceptada de la sección de sonido pertenece
al binario anterior y no se transfiere automáticamente.

El monitor anterior se detuvo antes de compilar. Su reemplazo confirmado vivo,
`out/wait-wind-native-validation.py`, espera 8 GiB estables durante 20 s,
ejecuta la captura nativa con audio dummy y su analizador, y sólo si aprueba
espera nuevamente el margen de memoria para lanzar los 15 ciclos. Logs y
resultados: `out/wind-native-validation.log`,
`out/wind-native-dummy-verified`, `out/wind-native-warp-15`. Hay que consultar
el proceso/session actual para determinar si espera, corre o terminó.

El inventario ampliado está en `BBHOST_FEATURE_INVENTORY.json`. Distingue la
elisión de copias de imágenes de BBHOST del filtro local de copias de buffers,
y registra explícitamente espejo de apariencia, renacimiento, edición de
parámetros, plugins y servicios online aún sin portar/certificar. La revisión
del ciclo completo permanece abierta; las pruebas actuales no certifican esas
funciones por extensión.

Actualización de la captura: `out/wind-native-dummy-verified/acceptance.json`
**aprueba** el run nuevo de 30 s: **1.794 frames y poses**, **59,825366 FPS**,
p99 **16,73 ms**, peor frame **16,75 ms**. Backend FSR 4.1 FP8 `t2160_m0`,
entrada/salida 2560×1440, preset 0, sin frame generation, sonido por frame y
backend dummy confirmados; guardados originales intactos y cierre sin fallos.
La identidad actual de binario/biblioteca coincide con
`out/wind-build-sha256.txt`. Esto aporta gameplay del binario nuevo; no mide
todavía el ritmo visual del viento comparado entre tasas ni otros modos GX.
El monitor sigue después con la admisión del soak, sin heredar su aprobación
de este run corto.

### Espejo: generador del mapa, antes de la integración del editor

`scripts/world_assets.py` porta el generador de
`engine/dream_mirror_layout.cpp` de BBHOST. El input local descomprimido tiene
el hash retail esperado; se conserva el personaje original del espejo (entidad
2100202, talk t210696), se convierte de DummyEnemy a Enemy y se mueve del slot
807 al 742, renumerando las referencias e índices. El resultado de **388.552
bytes** coincide **byte a byte** con ejecutar el generador C++ original:
`out/mirror-layout-differential.json`, SHA-256
`99d5c7b7f52307344c6e8296b499d5e57969f21c443fcc607cb42cb2c38c8afa`.

El generador comprueba input y output, valida de forma acotada el stream DCX,
recomprime usando el encabezado retail y publica el archivo generado mediante
rename atómico en una carpeta separada. Su caché se valida por contenido y se
repara si está dañada. Las pruebas usan el overlay existente para comprobar
que la capa generada precede a los mods del usuario y que el dump permanece
intacto. No se incluyen bytes del juego en el repositorio.

**No es todavía el espejo completo.** Faltan el observador de pasos de menú,
su propiedad de referencias y el controlador de la ventana ChrMake_BG con la
previsualización del cazador; después se integrarán la preparación, la opción
de usuario y la aceptación dentro del Sueño. El generador no tiene aún una
opción de launcher que anuncie esa función como disponible.

Pasan **4 pruebas dirigidas** y la suite completa de **284 pruebas Python**
(`out/mirror-layout-python.log`, `out/mirror-python-all.log`, código 0 en
`out/mirror-python-exit.txt`). Este trabajo no cambia el binario ni la biblioteca
GPU. El monitor obtuvo memoria suficiente y el soak de 15 ciclos del binario
de viento **ya arrancó**, con proceso observado vivo y archivos de muestras
en `out/wind-native-warp-15`. No se compiló el runtime durante ese soak ni se
cuenta como aprobado mientras no termine y pase `result.json`.


### Seguimiento del 04-11: soak completo y controlador del espejo

Se inspeccionó el resultado definitivo del soak de viento:
`out/wind-native-warp-15/result.json` **acepta los 15 ciclos**, con 31 cambios
observados, 2.425 muestras y 1.224,369 s de ejecución. RSS máximo 1.379.995.648
bytes, VRAM máxima 5.563.219.968 bytes; pasan los controles de RSS, VRAM y
staging, no hay fallos de runtime/cierre y los guardados originales coinciden.
El audio estaba desactivado. Es evidencia del binario de viento, no del nuevo
controlador de apariencia ni de todas las zonas del juego.

Se registró el controlador experimental `appearance.cpp` en el perfil del
juego mediante `BB_CHANGE_APPEARANCE=1`. Por defecto sigue apagado; requiere
el layout generado. Las pruebas nuevas ejecutan el constructor retail completo
y el observador del pacer. Los spies de heap/ventana comprueban el descriptor,
la referencia adquirida antes de asignar, propietario del hilo principal,
sustitución de menú, teardown del mundo y limpieza tras errores. Los tres tests
`appearance-test`, `menu-steps-test` y `frame-tasks-test` pasan contra el ELF
retail local. No se afirma todavía que la previsualización real esté validada.

La primera puerta completa detectó documentación de variables sin regenerar:
`test_document_is_current` falló; se regeneró con `scripts/env_vars.py --doc`.
El log `out/appearance-check-all.log` conserva ese fallo; el reintento tendrá
un nombre distinto. Los README español e inglés ahora describen 1440p nativo,
preset 0 y el pool de 5936 MiB ya implementado, en lugar del antiguo Quality.

La consulta actual de GitHub encontró BBHOST en
[7dbe6f60](https://github.com/droogie/bbhost/commit/7dbe6f60f33fff45ea1bcbf6c6e61a6b35466e13),
posterior a v0.2.24. Su espera/reintento de descarte GX y lectura segura de
stacks host están identificados para comparación con el renderer local;
no se cuentan como portados. Los PR abiertos se revisan por función y
aplicabilidad a Linux/RADV, sin asumir que todos mejoran este equipo.


### Perfil publicado y datos de comandos diferidos

Se preparó una generación nueva para este PC con `--performance-profile 60`:
render y salida 2560×1440, FSR 4.1, preset 0 y pool 5936 MiB. La validación
recorrió los 26.913 DCX; el log es `out/native-default-setup.log`. También se
alinearon las preferencias y valores por defecto locales con esa generación,
conservando antes sus copias privadas en `out/native-default-preferences-backup`.
No se publican esos JSON, porque pueden contener configuración privada.

El build de apariencia pasó **50/50 CTest**, **284 pruebas Python**, runtime C
y readback Vulkan, exit 0: `out/appearance-check-all-retry.log` y
`out/appearance-check-retry-exit.txt`. Con el perfil publicado, pero habilitando
el reloj experimental para la captura, `out/appearance-native-baseline` acepta
1.795 frames y poses: **59,825297 FPS**, p99 **16,73 ms**, peor **16,80 ms**,
FSR 4.1 FP8 `t2160_m0`, preset 0, audio dummy y guardados originales intactos.
Se inspeccionó la imagen GPU real: gameplay de Yharnam Central a 2560×1440.
No es una medición del reloj predeterminado, que sigue desactivado.

Se revisó el cambio de estabilidad
[3e600386](https://github.com/Mrsuss60/bloodborne_pc_windows_port/commit/3e600386439130a981259b84076a21a7bb31c3f6).
Se implementó la propiedad del overflow de datos/comandos por `RecordChunk`,
con alineación correcta incluso por encima de 64 bytes, destrucción de capturas
en cancelación y prefetch acotado. `RecordData` ya no retira un bloque entre
copias de un mismo comando; las reservas explícitas delimitan los lotes.
La regresión falla con el header anterior y pasa con el nuevo, también con
ASan/UBSan (`out/record-chunk-before.log`, `out/record-chunk-asan.log`).

No se copió su mutex global de creación de pipelines: la caché local se crea
sin `VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT`; la
[especificación Vulkan](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateGraphicsPipelines.html)
no exige ese mutex en esta configuración. Tampoco se ocultaron fallos de shaders
ni paquetes descartando draws como si una escena incompleta certificara calidad.
La serialización adicional necesitaría demostrar un fallo de driver y su coste
local. La concurrencia del productor del scheduler sigue teniendo su propio
contrato; esta corrección de lifetime no certifica productores simultáneos.

La puerta del primer build de overflow pasa **51/51 CTest**, **284 pruebas
Python**, runtime C y readback Vulkan, exit 0 en
`out/record-chunk-check-exit.txt`. Su primera ruta
`out/record-chunk-native-verified/acceptance.json` **no aprueba**: 59,675379 FPS,
p99 19,51 ms, peor 21,31 ms. Hay 24 frames lentos consecutivos entre los
segundos 0,937 y 1,393; todos sus compiladores de shader están a cero. No se
atribuyen automáticamente a un shader frío ni se descarta esa ventana.
El reloj predeterminado se mide por separado; la causa de aquella ráfaga sigue
pendiente de aislar. Una prueba adicional de reciclado de datos con readback
GPU se prepara antes de cerrar esta integración.

La comparación de cabezas nuevas encontró cambios Metal/macOS en
`bloodborne-native/bloodborne-runtime`, sin una mejora Linux/RADV identificada
en ese rango. `ayozetr/bloodborne_linux` añade fallback MOVBE para CPUs antiguas,
compatibilidad de imports de GPUs con un solo pool y salida al escritorio
esperando el guardado. MOVBE ya existe en este i5; la utilidad de guardado sí
queda identificada para integración posterior. No se han contado estas funciones
como implementadas por tener un ajuste o un nombre parecido.


La ruta del **reloj predeterminado**, sin overrides de tiempo, sí aprueba:
`out/native-active-default-verified/acceptance.json`, 1.794 frames y poses,
**59,825027 FPS**, p99 **16,73 ms**, peor **16,75 ms**. Mantiene FSR 4.1 FP8
`t2160_m0`, 1440p/1× y el guardado original. Ventana y audio están aislados;
no mide scanout físico ni calidad del dispositivo de sonido. No se infiere que
el reloj experimental sea la causa de la otra ráfaga sin un A/B adicional.

`record-data-test` añade un caso determinista sobre el scheduler real: retiene
un comando en el worker, fuerza otra reserva/reciclado mientras sus dos spans
siguen vivos, altera los vectores fuente y lee de vuelta **128 KiB reales**
escritos por Vulkan. La primera compilación falló por inicialización incompleta
de structs C con `-Werror`; se corrigió. La primera ejecución detectó el loader
Vulkan-Hpp privado del test sin inicializar; se usa ahora dispatch explícito como
en las demás pruebas GPU. El reintento pasa, sin omitir la comparación de bytes:
`out/record-data-vulkan-dispatch.log`.

Puerta consolidada posterior: **52/52 CTest**, **284 pruebas Python**, runtime C,
SPIR-V y readback Vulkan; exit 0 en `out/record-data-check-exit.txt`.
La compilación está en `out/record-data-build-sha256.txt`. La incorporación de
la prueba cambia la huella del compilador de shaders a través de CMake; se
verificará el nuevo build en gameplay por separado, sin trasladar hashes viejos.


### Espejo preparado desde Midnight y persistencia real

La opción `change_appearance` ahora existe en Midnight, en la preparación CLI
(`--change-appearance` / `--no-change-appearance`) y en el plan de lanzamiento.
Empieza apagada. La preparación genera el layout antes de los mods del usuario,
que conservan prioridad; el dump permanece intacto. Un mapa no compatible
cancela la generación y conserva el perfil publicado, verificado por una prueba.
La opción es independiente del switch general de file mods. El test GTK real
selecciona el switch, guarda y recupera su valor.

Se eliminó un segundo intento de instalación al encontrar que el punto original
`bbgpu_patch_image` ya existía: no hay dos rutas de activación. El controlador
no consulta el mundo si no tiene un menú pendiente/abierto. Una suscripción al
pacer que falla ya no consume un watch del constructor, permitiendo reintentar;
la regresión usa el pacer retail modificado y restaurado para comprobarlo.

La primera sesión real, `out/appearance-mirror-session`, duró **475,775 s**:
se navegó desde la lámpara hasta el espejo, se aceptó la nota original de una
sola vez, se abrió `ChrMake_BG`, se cambió Hombre por Mujer en la copia del
save, se cerró y se volvió a abrir. Las imágenes GPU muestran el modelo real,
no sólo un menú vacío. Resultado: guardados originales sin cambios.

Se preparó además un perfil independiente completo con el switch activo:
`out/appearance-prepared-validation.log`. Su ejecución posterior,
`out/appearance-prepared-session`, usa ese perfil sin forzar la variable del
espejo ni el reloj experimental. La imagen `frames/f002_final_2560x1440_bgra.png`
muestra **Mujer seleccionada tras reiniciar**: el cambio persistió en la copia.
`f003` y `f004` muestran rotación y zoom. Se pidió el viaje validado a Yharnam
Central con el editor abierto; `f005` y `f006` muestran gameplay en el mapa nuevo,
sin la ventana del editor. No aparecen fallos de runtime/cierre ni diagnósticos
`CONCURRENT recording` / `REENTERED recording` con `BB_PRODUCER_CHECK=1`.
Duración **295,465 s**, copia de origen intacta. No equivale a certificar
cualquier preset, toda región o largas sesiones.

Puerta de esa integración: **52/52 CTest**, **287 pruebas Python**, runtime C,
SPIR-V y Vulkan real. Código 0 en `out/appearance-feature-check-exit.txt`, log
`out/appearance-feature-check-all.log`; hashes en
`out/appearance-feature-build-sha256.txt`.

La primera captura de FPS posterior (`out/native-active-default-final`) fue
detenida por el límite de **2 GiB disponibles**; no se contó como aceptada.
El reintento esperó 8 GiB estables durante 20 s y conservó hashes antes/después:
`out/native-active-default-final-retry`. Tampoco aprueba: **59,671898 FPS**, p99
**16,73 ms**, peor **93,22 ms**, y un hueco de poses mayor de 100 ms. En ese
frame el tiempo del lado CPU fue **93,14 ms**, el contador de tiempo en kernel
**105,08 ms** (suma de hilos), sin compiles, nuevas asignaciones Vulkan ni
esperas de tick/presentación registradas. No se omite ese frame ni se afirma su
causa. Se añadieron muestras opcionales de memoria, faults, I/O y presión host;
el volcado PCM puede desactivarse para medir sin su tráfico diagnóstico de disco.

### FMOD: contador sin inicializar confirmado en el constructor retail

Se revisó BBHOST PR #104, cabeza
`d230787b2ab84d23981f7f7edfaf063e753e4a17`. El constructor retail
`0x11f3ce0` inicializa el puntero de +0x168 y hace memset desde +0x178, dejando
el contador de lenguajes +0x170 sin escribir. La prueba lo reproduce con bytes
0xA5. `BB_FMOD_PROJECT_INIT=1` (exacto) pone sólo esos cuatro bytes a cero antes
de ejecutar el constructor completo; sigue apagado por defecto, como el PR.
Una solicitud explícita con hook no disponible detiene el arranque.

La prueba ejecuta el **constructor retail completo**; sólo enlaza su import de
memset al memset host, que comprueba además el contador durante la llamada.
Compara todos los bytes y guards contra la ejecución original, comprueba
reutilización, retorno e idempotencia y permite la asignación posterior de un
LANG cargado con éxito. No ejecuta ni certifica todo el camino de limpieza de
un banco corrupto. Pasa `fmod-project-init-test` contra el ELF local.

### Repetición extendida y preparación de la recuperación GX

La captura `out/native-stutter-diagnostic/acceptance.json` aprueba el intervalo
completo de **120 segundos**: **7.178 frames**, **7.177 poses**, **59,824359 FPS**,
p99 **16,74 ms**, peor **17,38 ms**. Entradas y salida 2560×1440, preset 0,
FP8 `t2160_m0`, sin generación de frames, reloj publicado 0. Se usaron timers
de sección, comprobación del productor, traza host de 1 Hz y el hook FMOD
opcional; éste inicializó cuatro proyectos reales. Audio dummy sin escribir
PCM. Los binarios y los guardados originales conservaron sus hashes. La
correlación de pausas mayores de 25 ms está vacía. Esto acredita esa ruta y esa
compilación; no establece la causa de la pausa previa ni certifica todas las
áreas del juego.

La repetición `out/native-default-120-verified.log`, sin esos diagnósticos,
**no llegó a lanzar el juego**: expiró la admisión de 8 GiB disponibles estables
durante 20 segundos. No se registra como éxito ni como fallo de FPS. La puerta
de compilación anterior `out/record-batch-check-all.log` pasó **53/53 CTest**,
**287 pruebas Python**, runtime C, SPIR-V y readback Vulkan real. Los cambios
posteriores deben pasar su propia puerta antes de reutilizar esa conclusión.

La revisión de la recuperación GX de BBHost está fijada en
[`7dbe6f6`, rename_wait.cpp](https://github.com/droogie/bbhost/blob/7dbe6f60f33fff45ea1bcbf6c6e61a6b35466e13/src/decomp/gx/rename_wait.cpp).
Su contador de visitas al recolector no basta para distinguir todas las causas
del retorno 9. El desensamblado retail muestra que la segunda asignación de
copia puede recuperarse tras el recolector (`0x2ad0d97`), y fallar después la
asignación de tabla (`0x2ad0e87` o `0x2ad0ec0`). En ese caso repetir la función
consume otra copia. `gx-rename-retail-test` ejecuta el cuerpo original con
asignadores controlados para comprobar agotamiento, recuperación seguida de
fallo de tabla, consumo de una segunda copia al repetir y éxito completo.
Esta prueba no se presenta como integración del hook de recuperación.

Se añade `Vulkan::Semaphore::WaitFor`, con timeout explícito: un timeout no
publica un tick como completado y un éxito sí lo publica sin depender del
polling amortiguado. Es la base para esperar una submission sin drenar toda la
GPU. `timeline-wait-test` usa Vulkan real para comprobar timeout, señal
demorada, publicación y conservación de la API de espera anterior. Falta
conectar la recuperación GX con las etiquetas CPU y el hilo productor: esperar
sólo el semáforo no demuestra que sus callbacks de etiquetas hayan terminado.

Puerta posterior de estas bases: `out/gx-wait-check-exit.txt` **0**,
`out/gx-wait-check-all.log`: **55/55 CTest**, **288 pruebas Python**, runtime C,
SPIR-V y readback Vulkan real. Pasan tanto `timeline-wait-test` sobre RADV como
`gx-rename-retail-test` con el ELF 1.09; no se omitió ninguno de ellos.

### Recuperación GX conectada al productor y a las etiquetas

`gpu/games/bloodborne/gx_reclaim.cpp` integra ahora esa utilidad con una
adaptación importante: el hook está **dentro de la ruta lenta**, inmediatamente
después de la segunda asignación de copia. Si ésta devuelve cero, espera avance
y reintenta sólo ese asignador; luego continúa el cuerpo retail. Las asignaciones
que funcionan a la primera no pasan por un hook nuevo. Un fallo posterior de
tabla conserva el retorno original y no vuelve a consumir otra copia. Se
comprueban también los bytes del frame/llamadas usados para recuperar argumentos,
la llamada de tabla vacía y la actualización del contador del pool.

El recolector original se conserva mediante trampoline. Para la llamada de
tabla vacía se usa el retorno de la pila real, incluida una pila creada por el
host. `BbStats::NoteRangeCollector` sirve tanto al nuevo hook como al fallback
antiguo; el trap de estadísticas no se instala sobre el nuevo recolector.
`BB_GX_RENAME_WAIT=0` conserva las asignaciones/fallos originales y cuenta las
rutas lentas, sin las esperas nuevas.

La espera avanza de una submission en una (hasta 64 pasos, primera etapa de
200 ms), luego solicita flush grabado y finalmente decode+flush; las esperas
comparten un presupuesto de 400 ms. Las ejecuciones del asignador/recolector
retail conservan sus propios locks. `Presenter::AdvanceGpuFor` solicita el flush
al procesador de comandos; el llamador nunca graba Vulkan. La petición vive en
memoria compartida con el callback, puede cancelarse antes de arrancar y permite
que un flush ya activo termine después del timeout sin usar una pila vencida.

La finalización exige tanto el tick Vulkan como la salida del callback CPU de
la cola de etiquetas. Se utiliza una condición separada para no robar el wakeup
del hilo que escribe esas etiquetas. La espera de decode también tiene condición
separada y un snapshot **por cola**: trabajos nuevos terminados en otra cola no
pueden ocultar uno anterior aún bloqueado.

`gx-reclaim-test` ejecuta los cuerpos retail completos de rename y recolector
con asignadores/locks controlados. Comprueba las tres etapas, fallo posterior
de tabla sin consumo duplicado, segunda asignación recuperada por el propio
juego sin espera host, ruta normal sin observer, límite de 64 pasos, ningún
avance, firmas incompatibles, modo apagado, pila de hilo host y estadísticas
compartidas. Para el pool, el lock de prueba modela el cambio del contador tras
el avance; no simula todos los bloques de una partida. `cp-progress-test` usa
colas reales: mantiene gráficos bloqueado mientras termina cuatro trabajos
compute posteriores, exige timeout y luego libera el registro esperado desde
el propio procesador. `progress-request-test` verifica cancelación y vida del
callback que supera el plazo. Pasan las cuatro comprobaciones dirigidas en
`out/gx-reclaim-targeted-tests.log`.

La puerta `out/gx-reclaim-check-exit.txt` **0**, log
`out/gx-reclaim-check-all.log`, pasa **59/59 CTest**, **288 pruebas Python**,
runtime C, SPIR-V y readback Vulkan real. Falta la prueba integrada del puente
en una partida y la nueva medición predeterminada: esos checks separados no se
presentan como equivalentes.

Se preparó `tests/gpu_progress_preload.cpp`, target explícito
`gpu-progress-preload`, para esa prueba integrada. Se carga sólo en el proceso
de juego aislado, espera 1.800 ticks válidos de mundo y llama una vez a cada
etapa desde el pacer. Con `BB_GPU_LABELS=0` se fuerza la ruta de etiquetas CPU.
`out/run-gpu-progress-integration.py` usa copias de guardados, dummy audio sin
PCM, hashes antes/después y exige los tres resultados. La etapa de un tick
puede no tener trabajo pendiente; ambos flush deben completar. El plazo
registrado permite inspeccionar la espera real; el margen de un segundo del
harness detecta bloqueos grandes y no redefine el presupuesto de producción.
Este harness no se carga al jugar normalmente ni acredita por sí solo FPS del
perfil final. Su admisión conserva 8 GiB disponibles estables durante 20 s.

La puerta posterior al harness también pasa: **59/59 CTest**, **288 Python**,
runtime C, SPIR-V y readback Vulkan, código **0** en
`out/gx-reclaim-harness-check-exit.txt`, log
`out/gx-reclaim-harness-check-all.log`. El proceso de la prueba integrada debe
comprobarse por su sesión/PID y su manifest; esperar memoria no acredita que el
juego haya arrancado ni que el puente haya pasado.

### Siguiente utilidad de los forks: salir después de guardar

La comparación de `ayozetr/bloodborne_linux` hasta
[`1d0bbec`](https://github.com/ayozetr/bloodborne_linux/commit/1d0bbec9785843eca8661f481f3a7f47de126829)
incluye salir al escritorio pasando primero por Quit Game y su guardado.
Aquí `gpu/shim/bbport_game_menu.cpp::QuitFactory` aún envía SDL_QUIT
inmediatamente. Su port está pendiente. Antes de trasladarlo conviene resolver
la cancelación: la variante upstream arma la salida antes de preguntar y
mantiene esa intención hasta 120 segundos. Eso no demuestra desarme inmediato
al elegir No.

La inspección retail ofrece un candidato para armar **tras confirmar**, sin
mantener una intención global pendiente al cancelar: ExitGame BN `0x200c9f0`
construye un functor con vtable `0x5744a10`; sus clones copian el byte +8.
`out/save-quit-vtable.json` documenta las relocaciones, y
`out/exit-game-callback.bin` el cuerpo: el método de +0x10 es BN `0x200d050`,
que escribe el estado +0x250 en `0x200d0c4`. Ese bloque no lee el byte del
functor. La construcción en `0x200ca4d` y la escritura final tienen instrucciones
sin referencias RIP que admiten observers existentes. Es una **hipótesis de
integración**, a validar con clones/cancelación retail y el diálogo real: marcar
sólo el functor de nuestra pregunta, y armar el cierre cuando se ejecuta su
confirmación, conservando el cuerpo original. Todavía no se ha modificado el
comportamiento de salida ni se presenta esa hipótesis como función terminada.

### Salida con guardado: implementación y verificación real

La hipótesis anterior se implementó y comprobó. `save_quit.cpp` marca el
functor de nuestra pregunta durante su construcción; las dos funciones de copia
retail conservan el byte +8, ignorado por su invocación. El callback afirmativo
original arma `runtime_quit_confirmed` y sigue escribiendo los estados del juego
como antes. Una pregunta normal recibe cero. No hay intención global armada al
abrir el diálogo ni al responder No. Se verifican firmas, clones y vtable antes
de publicar los hooks.

La opción del mundo se agrega como acción, igual que Quit Game, y se conserva
la opción original. El estado de construcción del menú es local al hilo y se
restaura al volver. Si los hooks no están disponibles no se ofrece una acción
inmediata de cierre dentro del mundo. En el título se conserva la salida
inmediata. Los textos y la pregunta tienen traducción española.

`src/runtime_quit.c` exige confirmación, llegada al título y **todos** los slots
de guardado desmontados. El timeout usa reloj monotónico. El callback de unmount
se ejecuta después de liberar el mutex del módulo de guardado. Un fallo al
publicar SDL_QUIT mantiene la petición dentro de su plazo; los avisos simultáneos
no duplican el cierre. No se usa la apertura heurística de un archivo logo como
prueba de llegada al título: se utiliza la construcción de sus textos.

Pruebas: `save-quit-test` ejecuta la fábrica y el callback retail completos,
ambos clones, cancelación seguida de Quit Game normal y confirmación desde otro
hilo, comparando los bytes de estado escritos por el juego. `quit-test` prueba
orden de eventos, montajes, expiración, cancelación, error de cola y concurrencia;
ASan/UBSan también pasa en `out/quit-asan-tests.log`.
`savedata-quit-test` usa los módulos reales de guardado/cierre con dos directorios
temporales: ambos archivos deben estar legibles al emitir el cierre, y el
callback vuelve a consultar montajes para demostrar que el lock ya fue liberado.

Puerta completa: **60/60 CTest**, **288 Python**, ambas pruebas C nuevas, runtime
C restante, SPIR-V y readback Vulkan. Código **0** en
`out/save-quit-check-exit.txt`; log `out/save-quit-check-all.log`.

Se comprobaron tres sesiones reales headless, con copias independientes del
guardado y el perfil publicado 1440p Native AA/FSR 4.1 FP8:

- `out/save-quit-cancel-session`, **230,055 s**: pregunta española, No,
  movimiento en el mundo, Quit Game original y regreso al título sin cerrar.
  No hay mensaje de intención de salida. La captura `f004` muestra la pregunta
  propia y `f008` el título. Se detuvo después de comprobar la cancelación.
- `out/save-quit-confirm-session`, **55,507 s**: restaura la posición guardada de
  la primera copia, confirma Sí en nuestra pregunta, registra confirmación y
  luego «guardados desmontados y título alcanzado». Cierre natural **0**.
- `out/save-quit-title-session`, **145,742 s**: reabre la copia después del
  cierre anterior; restaura de nuevo `x=-194,916626, z=66,9968414`. Quit Game
  normal vuelve al título y la salida desde allí cierra inmediatamente, código
  **0**, sin armar la salida con guardado del mundo.

Los tres manifests registran **guardados originales intactos**, hashes de
binarios estables y ausencia de fallos de runtime y cierre. Son verificaciones
funcionales, con el `MemoryWatch` existente (admisión 4 GiB, corte runtime 2 GiB
y PSI full 25%); **no se usan como aceptación de FPS**. No se rebajó la admisión
de 8 GiB estable de las mediciones de rendimiento/prueba GX pendiente. Su
primer intento `out/gpu-progress-integration.log` expiró sin lanzar el juego.

### Puente GX: prueba dentro del juego con etiquetas CPU

`out/gpu-progress-integration-current/manifest.json` pasa la prueba integrada
después de admitir **8 GiB estables durante 20 s**. Se carga el harness sólo en
el juego aislado y se fuerza `BB_GPU_LABELS=0`; los contadores EOP registran
etiquetas escritas, cero etiquetas/frame por GPU y vuelven a cero pendientes.
Los tres callbacks del pacer reportan:

| Etapa | Resultado | Tiempo observado |
|---|---|---|
| Un tick submitted adicional | Sin trabajo pendiente; retorno inmediato válido | 6.550 ns |
| Flush del trabajo grabado, GPU y etiquetas | Completado | 400.834 ns |
| Snapshot de decode por cola, flush, GPU y etiquetas | Completado | 431.798 ns |

Hashes antes/después estables, guardados originales intactos y ausencia de
fallos de runtime/cierre. La ruta diagnóstica completa de 30 s también pasa
`native_acceptance.py`: **1.794 frames**, **59,825087 FPS**, p99 **16,73 ms**,
peor **16,77 ms**. La línea stdio truncada queda después del intervalo completo
según su timestamp, no se elimina un frame dentro de la medición.

Esto verifica los dos flush reales con etiquetas CPU. **No ejercita una
submission pendiente en la primera etapa** ni reproduce agotamiento real de
copias en un área pesada; ambos siguen siendo comprobaciones separadas. Las
pruebas retail sí cubren recuperación y fallos con progreso controlado. Tampoco
se sustituye la medición del perfil predeterminado: esta ruta lleva un harness y
el modo de etiquetas distinto. El reintento sin ellos está identificado por
`out/native-default-120-current/manifest.json`.

### Registro histórico: aprobación de 120 segundos retirada por fallo de cierre

Los párrafos siguientes conservan la conclusión original, ahora retirada:
el detector anterior omitía `Fault (signal 11)` durante el cierre. La revisión
`acceptance-corrected.json` rechaza esta captura; los resultados nuevos y
válidos figuran en el seguimiento del 05-11.

`out/native-default-120-current/acceptance.json` aprueba el intervalo completo
de **120 segundos**, después de 30 s de warmup: **7.179 frames** y **7.179 poses**,
**59,824357 FPS**, p99 **16,73 ms**, peor **17,49 ms**. Entradas y salida reales
2560×1440, FSR 4.1 FP8 `t2160_m0`, preset 0, sin generación de frames, pool
5936 MiB y memoria guest CPU cached. Usa los ajustes publicados, sin preload,
sin forzar etiquetas CPU, FMOD opcional, reloj experimental ni timers de sección.
La captura conserva el muestreo de frames/poses y audio dummy sin volcado PCM.

La admisión esperó 8 GiB estables durante 20 s; los guardados originales y los
binarios conservaron sus hashes. Todos los gates, incluida ausencia de fallos
durante runtime y cierre, pasan. El binario es el mismo verificado en las tres
sesiones de salida con guardado: `bb-probe` SHA-256
`6cdf9536980cdce1913427e3780ac7cb19f5b30623ef3df0ce498924d4ea6c56` y
`libbbgpu.so` `f6413aeea86cef49ea5685e960608127c0cbb0d8e0ec1156045130cd48de80b3`.
Es una ruta headless en este PC; no certifica áreas pesadas, toda transición,
VRR/monitor físico, mando/audio físicos, online ni la integración completa de
las utilidades pendientes de BBHost. No se toma como finalización del objetivo
ni se publica todavía una release final.

### Seguimiento del 05-11: interrupciones, parámetros y nuevas capturas

Se fijó BBHOST `bdc5cdf8be5f37188da9c2368e92be444fdc04cb` (45 commits después
de la base de esta auditoría) y se actualizaron las cabeceras de seis forks.
`BBHOST_FOLLOWUP_2026-11-05.json` conserva commits y PR abiertos; es un
descubrimiento actualizado, no una certificación de todos sus cambios.

`interrupt-lifetime-test` reproduce el uso de callbacks de interrupciones
después de su destrucción estática. El código anterior falla; ASan identifica
`heap-use-after-free` en `BbIrq::Raise`. El directorio fijo de 65 líneas ahora
vive hasta terminar el proceso; `Forget` y `Once` siguen liberando handlers
durante la ejecución. No se añade una asignación por frame. La regresión y
ASan/UBSan pasan. Logs: `out/interrupt-lifetime-{before,after}-asan.log`.
El verificador ahora rechaza también `Fault (signal N)` genérico, incluso
después del marcador de cierre, conservando la excepción para pérdida de
superficie del compositor sin fallo del runtime.

El perfil publicado 1440p Native AA/FSR 4.1 FP8 pasa dos barridos de cámara
de 120 segundos, después de 30 s de warmup y admisión de 8 GiB estables:

| Captura | Frames / poses | FPS | p99 | Peor frame | Cierre |
|---|---:|---:|---:|---:|---|
| `out/native-mensis-camera-lifetime` | 7.179 / 7.179 | 59,825040 | 16,73 ms | 16,88 ms | Sin fallos |
| `out/native-cathedral-camera-lifetime` | 7.179 / 7.179 | 59,824816 | 16,73 ms | 16,74 ms | Sin fallos |

Entrada y salida reales 2560×1440, preset 0, FP8 `t2160_m0`, sin generación
de frames, pool 5936 MiB, audio dummy sin PCM ni muestreo host. Se verificaron
el mapa solicitado durante toda la ventana, guardados originales y hashes
estables. Binarios: `bb-probe` SHA-256
`256c583edabe2c7670571b216c56a4d8050e1b039c288ce1f7edc5337aabebf0` y
`libbbgpu.so` `61afeca755b6339c4183baf9839b4824d89286f1acd3146bc46c602f9328c2b3`.
Son pruebas alrededor de las lámparas de entrada; no certifican cada zona,
combate, mapa completo, VRR/monitor físico, online ni todo BBHOST.

El intento previo `out/native-mensis-120-goal` se conserva como fallo: la
marcha de 120 s provocó carga/muerte fuera de una ventana válida y además
mostró el fallo de cierre anterior. No se usó su media de 59,826 FPS para
aprobar gameplay. El primer intento de admisión expiró sin lanzar el juego.

La corrección de tela ahora termina el muestreo de sus tres contadores
atómicos después del único reporte. Se comprueba el hook retail antes y
después del umbral a 30–480 Hz. Microbenchmark del hook: 11,738–11,799 ns por
llamada antes y 2,808–3,034 ns después, sin cambiar resultados físicos. Esto
no equivale a una ganancia de FPS del juego completo. La puerta completa
posterior a interrupciones pasa **62 CTest, 300 Python**, runtime C, SPIR-V
y readback Vulkan (`out/interrupt-lifetime-check-all.log`); con la telemetría
opcional posterior pasan **301 Python** (`out/goal-board-telemetry-python.log`).

`scripts/param_tables.py` adapta el lector de nombres de campo de BBHOST y
genera mods opcionales desde TOML `[row.all]`/`[row.ID]`. Coincide con upstream
en las 65 tablas / 79.759 filas. La prueba genera, descomprime y compara un mod,
verificando límites, bits vecinos, prioridad por ID e integridad del dump.
No se modifican valores de juego predeterminados. La lectura del repositorio
vivo en `games/bloodborne/params.{h,cpp}` pasa ASan/UBSan y se compiló después
de las capturas anteriores. `out/param-mod-live/manifest.json` verifica un mod
generado en el juego real, sin cambiar el archivo original ni los guardados.
El comando de aceptación `param` verifica palabras de tablas cargadas.
Los overrides vivos y la ABI de plugins continúan pendientes.

`out/native-cp-spin-abba` compara `BB_GPU_SPIN_US=200,0,0,200` en el mismo
barrido de cámara de Byrgenwerth con muestreo CPU/memoria y potencia de placa a 1 Hz.
La potencia es del dispositivo entero e incluye otras aplicaciones; no se
atribuye exclusivamente al proceso. **No se han cambiado defaults por esta
comparación.** También terminó `out/native-pipe-spin-abba`: los cuatro
casos pasan y eliminar ese spin redujo el tiempo CPU observado aproximadamente
un 2,12%; la potencia de placa no mostró una reducción atribuible al cambio.
Con ambos spins en cero pasan dos capturas de 120 segundos y el soak
`out/native-idle-combined-soak`: 15 ciclos, 1.219,7 segundos, sin fallos de
runtime/cierre, sin crecimiento persistente fuera de límites y con los
guardados intactos. Estos resultados corresponden a ese binario y configuración;
no certifican los nuevos ajustes experimentales del limitador.


### Limitador adaptativo y cadencia: primera comparación

El margen de sueño de BBHOST CPU-04 se adaptó conservando el limitador retail:
se verifican las instrucciones antes de parchear y se compara su estado completo
con la función original. `BB_LIMITER_MARGIN_US=auto` aprende la segunda mayor
latencia de los últimos 128 despertares y limita el margen a 300–2.000 us.
Sigue siendo opcional. `BB_PACE_STABLE=1` conserva la fase ante pequeños retrasos
del host y reinicia el plazo ante un atasco, evitando acumular el retraso de cada
sleep. `BB_NATIVE_FLIP_PACING=1` es otra alternativa experimental, con un techo
de seguridad de 480 FPS para frames ya limitados por el juego.

`out/native-phase-screen/summary.json` compara cuatro condiciones en Byrgenwerth,
con ambos spins en cero, ventanas completas de 60 s y el mismo binario:

| Condición | FPS | p99 | Peor frame | CPU, % de un núcleo |
|---|---:|---:|---:|---:|
| Referencia | 59,8251 | 16,73 ms | 16,75 ms | 155,36 |
| Cadencia estable | 59,9988 | 16,71 ms | 17,64 ms | 219,71 |
| Cadencia estable + margen adaptativo | 59,9933 | 16,71 ms | 18,16 ms | 138,29 |
| Handoff nativo + margen adaptativo | 60,0024 | 17,32 ms | 21,55 ms | 135,51 |

Todos los gates pasan; las tres alternativas entregaron 3.600 frames durante la
ventana. La combinación de cadencia estable y margen adaptativo reduce el CPU
observado un 10,99% respecto de la referencia de esta sesión, con mejor p99 que
el handoff nativo. No se atribuye ahorro energético a la potencia de placa.
Faltan repetición, otras áreas y soak con esta combinación antes de adoptarla.
La suite previa a esta comparación pasa 78 CTest y 302 Python, runtime C,
SPIR-V y Vulkan (`out/phase-pacing-check-all.log`).


### Corrección de identidad de área

La lámpara `3202950`, mapa `0x20000000` (`m32_00_00_00`), es Byrgenwerth,
no Mensis. Los directorios históricos `native-mensis-*` de este seguimiento
conservan su nombre para no romper la procedencia de la evidencia, pero sus
capturas sólo prueban Byrgenwerth. Esto afecta también a los ABBA de spins y
al primer screen de cadencia. La afirmación de cobertura de Mensis queda
retirada. `tools/area_check.py` de BBHOST y `ReturnPointParam` identifican
Mensis como lámpara `2602950`, mapa `0x1a000000` (`m26_00_00_00`). La comparación
de Catedral en curso usa correctamente `2402950` / `0x18000000`.

### Preparación del altar y validación adicional

La nueva suite pasa **87 CTest y 315 Python**, runtime C, SPIR-V y Vulkan
(`out/esd-rebirth-check-all-final.log`). Los lectores y el generador ESD
son herramientas offline; no se cargan en la biblioteca del juego. Los 271
guiones locales se reconstruyen idénticos; la modificación del altar produce
el SHA-256 esperado por BBHOST. ASan/UBSan pasa con 10.000 mutaciones
sintéticas y ocho por guion local. Se corrigieron límites de offsets/counts
y la conservación de condiciones referenciadas sólo por listas huérfanas.
El CLI rechaza fuentes desconocidas y destinos existentes, incluidos enlaces.

El plan puro de renacimiento valida los nueve orígenes y el grafo de costes
locales, límites de ecos y datos inválidos sin escribir al juego. Una prueba
contra el menú retail encuentra seis niveles cuyo coste difiere de la
expresión original de BBHOST compilada para este PC. Separar operaciones
escalares y reproducir el segundo redondeo retail elimina las diferencias
en los 544 niveles, comprobando coste−1, coste y coste+1. Estas pruebas de
plan/precio pasan por separado; todavía falta conectarlas a la suite y al
servicio de altar. No hay renacimiento jugable habilitado.

La oclusión fresca opt-in pasó otra ventana completa de Mensis (7.200 frames,
peor 16,75 ms) y revisión de una captura con geometría/HUD visibles. Sigue
apagada por defecto hasta ampliar las pruebas, incluido retraso de GPU.
El escaneo de mandos corregido está compilado. La captura física anterior
de 600 s pasa al analizar toda la ventana con el lector corregido: 36.000
frames, 59,993 FPS, p99 16,71 ms, peor 16,89 ms. Conserva el fallo original
del analizador anterior. Esto no retira el intento con el frame de 281,56 ms.

### Binarios conservados y monitor físico

Con el escaneo de mandos corregido, el perfil actual pasa una ventana física
de **600 s: 35.999 frames, 59,9913 FPS, p99 16,71 ms, peor 31,21 ms**. Ese
frame ocurre 36 ms después de iniciar el barrido, junto a 33,6 MB de
asignaciones; no se elimina de la ventana ni se atribuye causa sin prueba.
El monitor físico se mantiene estable y los guardados quedan intactos.
La aceptación global del observador permanece `false`: al omitir el muestreo
de recursos no se exportó el PID y no hubo observaciones de la ruta USB.
La clasificación está en `out/native-physical-current-600/observer-classification.json`.

El capturador ahora publica el PID en toda sesión de escritorio, con o sin
muestreo. La repetición de **120 s pasa completa**, con 7.200 frames,
59,9953 FPS, p99 16,71 ms y peor **16,73 ms**. Confirma salida USB física
en todas las observaciones medidas y cero faltas de datos de audio. Ambas
sesiones restauran la pantalla y conservan sus hashes durante la captura.
La candidata se conserva en `out/verified-native-1440p-2026-11-05`, separada
de las siguientes compilaciones, con los tres binarios y su manifiesto.
No es una release final; el tirón histórico de 281,56 ms sigue sin explicación.

### Capa completa de archivos del altar

La suite pasa **89 CTest y 323 Python**, runtime C, SPIR-V y Vulkan
(`out/altar-assets-check-all.log`). Las pruebas de orígenes y umbrales retail
de los 544 niveles ya están registradas en CTest. El lector BND4 compartido
por parámetros y diálogos reconstruye idénticos 42 paquetes locales. Los
528 grupos FMG conservan sus mapas de mensajes al leer y escribir, incluidos
nulos, vacíos, caracteres coreanos y pares UTF-16.

El generador `scripts/rebirth_assets.py` produjo una capa de 23 archivos: sólo
modifica `t242307.esd` dentro del paquete del altar y añade cinco mensajes al
grupo de eventos 30 de cada idioma disponible. Los originales permanecen
intactos; `spaes` y `spaar` reciben los nuevos textos en español. Las pruebas
verifican contenido restante idéntico, colisiones rechazadas, origen de
script y salida del helper comprobados por hash, caché reutilizable, reparación
sin borrar cachés editadas y ausencia de publicación parcial ante un último
archivo inválido. No está instalada en el perfil publicado: todavía falta
el servicio de altar y su prueba real de aceptar/deshacer sobre guardados
copiados. Los binarios aceptados físicamente se mantienen congelados en
`out/verified-native-1440p-2026-11-05`; esta nueva compilación tiene hashes
propios y no hereda automáticamente su aceptación.

### Servicio de renacimiento opt-in y separación del readback

El servicio nativo `BB_REBIRTH=1` ya está implementado, apagado por defecto y
limitado al modo offline. Reutiliza los observadores de pacer/menú y el lector
de parámetros. Los eventos se comparten con la restauración del kit; el nuevo
servicio de lectura de atributos no mantiene punteros a través de cargas.
Antes de aplicar atributos mediante la función retail comprueba los datos y
los destinos de salud/ecos. Cura tras aplicar y conserva el estado para
deshacer; al cargar o cambiar de mapa descarta la transacción. Posee y libera
las referencias del menú y espera dos segundos, independientemente del FPS.
Fuera de `m24_02` no consulta los flags del altar.

Pasan **91 CTest y 323 Python**, runtime C, SPIR-V y Vulkan. El controlador
puro y el adaptador instrumentado pasan ASan/UBSan. El primer fallo del test
fue una escritura de la propia prueba en la página RX del pacer después de
instalar el hook. Se localizó con GDB y se corrigió el `mprotect` del fixture.
El juego no presentaba ese fallo.

Con guardados copiados, una prueba de origen temporal verifica nivel 13→9,
3.526 ecos, curación tras bajar vitalidad y deshacer exacto. La prueba con
orígenes originales verifica nivel 13→10, 2.715 ecos y deshacer exacto, pero
su ventana de rendimiento queda **rechazada** por un frame de 62,23 ms al
inicio, antes de las órdenes del servicio. Ningún resultado se reescribe.

Se detectó un factor de la medición: el capturador solicitaba una imagen GPU
al mismo tiempo que arrancaba el replay. Ese readback puede asignar memoria
y escribir el archivo en un callback del scheduler. El nuevo `SnapshotGate`
espera el archivo completo y dos segundos sin cambios antes del replay; sus
regresiones y la suite Python ampliada pasan **326 pruebas**. Una nueva
ventana completa con el mismo binario y parámetros originales pasa servicio
y rendimiento: **7.200 frames, 59,9983 FPS, p99 16,72 ms, peor 16,73 ms**.
La comparación está en `out/rebirth-live-standard-primed/snapshot-phase-comparison.json`.
Esto no explica el antiguo tirón de 281,56 ms a mitad de ventana.

La exploración visual identifica `2422952` como arena del Emisario Celestial
y `2422951` como sala de Ebrietas. Ambas están en `m24_02`; las pruebas de
handshake no certifican interacción física con el altar. Su diálogo, requisito
de piedra, menú de nivel y aceptar/deshacer mediante la UI siguen pendientes,
junto con su selección en el launcher. El perfil publicado no activa el
servicio ni instala esta capa por defecto.


### UI del altar, preparación fija y monitor físico: candidato actual

La UI nativa del altar se verificó en la sala de Ebrietas (`2422951`) sobre
una copia del guardado. Un fixture exclusivo de prueba marca la derrota del
jefe antes de cargar la sala y entrega una Piedra de Yharnam mediante las
funciones originales de inventario. El renacimiento se opera con el mando y
el diálogo del juego, sin ejecutar las órdenes de depuración reset/undo.
Se comprobaron los textos españoles, rechazo sin piedra, compra de nivel,
retorno desde la elección final, deshacer exacto, aceptar y conservación al
viajar. Una segunda ejecución, sin preload, confirma la persistencia del
nivel 11, atributos y 1.886 ecos aceptados. Evidencias:
`out/rebirth-ui-owned-dialogue/ui-evidence.json` y
`out/rebirth-prepared-persistence/manifest.json`.

El launcher y CLI ya preparan y activan juntos la capa y el servicio opcional;
la GUI ofrece su interruptor y el build normal compila el helper ESD.
Permanece apagado por defecto. Los mods del usuario conservan prioridad.
La suite integrada pasa **91 CTest y 328 Python**, runtime C, SPIR-V y Vulkan
(`out/rebirth-integrated-check-all.log`).

Se corrigió `output_auto` en el preset recomendado: una preparación desde la
sesión remota podía elegir 1520×720. Ahora fija explícitamente render y salida
2560×1440. La preparación real publicó la generación
`7df11393e8944db5b122a0f41701d926`, conservando los cuatro mods seleccionados,
con FSR 4.1, preset 0, generación de cuadros apagada y renacimiento apagado.
Su parche de escena también es 2560×1440; el canvas lógico UI sigue siendo
1920×1080. La prueba de regresión cubre partir de `output_auto=True`.

La compilación actual pasó **600 segundos completos** de barrido de cámara en
la entrada de Mensis sobre el MSI MAG 275QF físico, a 2560×1440, con audio USB
ALSA activo: **36.000 frames, 59,9934 FPS, p99 16,72 ms, peor 16,95 ms**.
No se detectaron fallos de ejecución/cierre ni starvation de audio; las
partidas originales y los binarios quedaron intactos. La configuración de
pantallas se restauró al finalizar. El HDR solicitado no certifica HDR real.
Evidencia: `out/native-physical-fixed-600/manifest.json`. Los tres binarios y
sus hashes se conservan en `out/verified-native-1440p-fixed-2026-11-05`.

Es un candidato verificado para esa ruta, no la release final ni una garantía
de todas las escenas. El tirón histórico de 281,56 ms conserva su resultado
rechazado y carece de causa demostrada. La comparación del resto de BBHOST y
su host de plugins continúa; la matriz enumera los 54 servicios API 16 en
`docs/BBHOST_PLUGIN_SERVICES_AUDIT.json`.


### Decisión por llamada para hooks de plugins

`Core::InstallConditionalGuestCodeHook` permite devolver desde una función o
continuar el original según esa invocación, preservando registros, YMM, MXCSR
y flags. La decisión no utiliza el flag compartido de compatibilidad: se
bifurca con el resultado del callback antes de restaurar el estado del juego.
El trampoline original queda disponible antes de publicar el hook. La
instalación exige que ningún hilo ejecute el sitio mientras se parchea;
serializar el registro no equivale a detener esos hilos.

La regresión ejecuta 16.000 llamadas en ocho hilos sincronizados, decisiones
opuestas recursivas, original invocable, argumentos/resultados modificados y
carry conservado en ambas ramas. Pasan los **92 CTest y 328 Python** completos
(`out/plugin-conditional-check-all.log`). El test y el generador de hooks
instrumentados pasan ASan/UBSan en `out/plugin-conditional-asan.log`.
Se excluye sólo `-fsanitize=function`: esa comprobación intenta leer los
metadatos de tipo de compilador ocho bytes antes de una función JIT, que no
los posee; el primer intento falló antes de instalar el hook y se conserva en
`out/plugin-conditional-asan-initial-jit-metadata.log`. Las comprobaciones de
memoria y el resto de undefined behavior permanecen activas; leak checking
se desactiva por las áreas de trampoline con vida de proceso.

Este cambio cubre una primitiva necesaria, no expone todavía la API 16 ni un
loader de plugins. La propiedad y retirada de callbacks/módulos, el puente de
registros de BBHOST y los demás servicios siguen pendientes.

La nueva compilación con el hook condicional pasa otra captura física completa
de 120 segundos: 7,200 frames,
59.99179 FPS, p99 16.71 ms,
peor 16.73 ms; USB activo, sin starvation y pantalla
restaurada. Evidencia: `out/native-physical-plugin-hooks-120/manifest.json`.
Se conserva por separado en `out/verified-native-plugin-hooks-2026-11-05`.
La captura de 600 segundos corresponde al candidato anterior congelado.

Se revisaron también los ocho commits de la PR abierta de BBHOST
[#213](https://github.com/droogie/bbhost/pull/213), head
`04aea6ddced70c9b645f130e4c9382d9f79dff83`, base v0.2.26.
La selección de su nuevo pool sigue exigiendo más de 60 FPS. La política local
de dormir workers ociosos permanece; el pool de homes/segments, sus caches
y su flag de publicación pertenecen a otra arquitectura y no se copian sin
equivalencia. La clasificación por commit y las limitaciones figuran en
`docs/BBHOST_PR213_AUDIT.json`. Sus cifras de rendimiento/TSan son mediciones
de upstream, no de este PC.


### Adaptador experimental de ondas de agua

Se adaptó el control de interacción de agua de BBHOST v0.2.26 a los hooks
nativos, como `BB_WATER_GATE=1`, **apagado por defecto**. Los dos prologs retail
se validan antes de parchear; se emplean trampolines originales y se conservan
argumentos/retornos. Si el segundo hook no se instala, el primero continúa
pasando al original porque ningún task adaptado pudo activarse. La instalación
es idempotente y rechaza imágenes diferentes.

El modelo conserva la rotación de los tres mapas de altura y el último estado
visible en los frames entre pasos. Compensa cruces de cámara mediante un paso
adelantado, maneja mapas nuevos por resize y conserva sólo los impulsos de la
última trama, sin intercambiar buffers de heaps diferentes. Las pruebas cubren
30/60/90/120/144 FPS, tiempos irregulares, tiempo parado, cargas, cámara,
resize, superficies desactivadas y separación update/task. Detectan las **18
variantes defectuosas**; el modelo pasa ASan/UBSan.

Una prueba adicional ejecuta los prologs originales del ELF 1.09 y los hooks
reales con cuerpos de update/task sustituidos por fixtures: comprueba ABI,
retorno, paso/retención y rechazo de cada firma alterada sin parche parcial.
También pasa instrumentada con ASan/UBSan, con la misma exclusión de metadatos
de tipo JIT documentada para el hook condicional. No equivale a ejecutar el
cuerpo completo del renderer retail ni a validar visualmente las ondas.

La suite pasa **97 CTest y 328 Python**, runtime C, SPIR-V y Vulkan
(`out/water-native-check-all-fixed.log`). El primer full check detectó que el
documento generado de variables no incluía la opción nueva; se regeneró y se
repitió la suite. El registro fallido se conserva.

El adaptador usa el banco de tiempo del mundo GX; el helper de planificación
30/60 Hz también está portado y probado, pero todavía no sustituye la cadencia
local de efectos. Las reglas smooth, emisión y ribbons permanecen pendientes.
Se mantienen cuatro registros de manager como en upstream; los adicionales
pasan al original. La equivalencia visual en el lago, los impulsos producidos
por movimiento y los viajes prolongados siguen pendientes. La captura física
de entrada de Byrgenwerth iniciada con esta opción no se denomina prueba de
Moonside Lake sólo por compartir mapa.

La captura `out/native-physical-water-byrgenwerth-120/manifest.json` pasa
rendimiento nativo y presentación física/audio USB: 7.200 frames, 59,992996 FPS,
p99 16,71 ms y peor 16,73 ms. Sin embargo, su resultado global permanece
**false**: el cierre SDL del harness no produjo el informe `atexit` de actividad
de agua. No se observaron fallos de ejecución/cierre; la instalación está
registrada, pero eso no prueba trabajo efectivo. La clasificación se conserva
en `observer-classification.json`; hace falta leer contadores durante la
partida antes de certificar actividad, además de comparar ondas dentro del lago.

La observación durante la partida resuelve la falta de contadores en un
**ensayo separado**, sin modificar el resultado físico original. El preload
exclusivo `tests/water_ui_preload.cpp` comprueba el guardado copiado/offline,
se suscribe al pacer y lee los contadores mientras el juego vive. La función
retail de traslado sitúa al personaje dentro de Moonside Lake; no se modifican
flags de jefe ni atributos. Se verifican la posición, un desplazamiento lateral
breve, tres capturas 1440p y salud final 594/594. Los originales quedan intactos.

El último muestreo contiene **2.440 tareas, 1.221 pasos, 3 compensaciones de
cámara y 1.222 restauraciones de cuadrícula**. Se inspeccionaron dos capturas,
con agua/suelo/personaje y Rom visibles, sin fallo obvio en esos fotogramas.
El personaje termina en (-509,139; -271,243; 380,221), aún en el lago.
Evidencia revisada: `out/water-ui-owned-lake-1/reviewed-evidence.json`.
No demuestra equivalencia temporal con un control a 30 FPS ni una comparación
A/B. Los contadores de impulsos son **cero**: su conservación está probada por
el modelo, pero no por este movimiento en la partida. La opción sigue apagada.
La compilación se conserva en `out/verified-native-water-adapter-2026-11-05`;
es otro candidato con alcance explícito, no una release final.


### Bloodletter: radio de autoimpacto y coste del lector de parámetros

El servicio `gpu/games/bloodborne/bloodletter.cpp` adapta la corrección de
BBHOST para el autoimpacto del L2 transformado. Se instala cuando la conversión
nativa de FPS está activa; `BB_BLOODLETTER_SELF_HIT=0` permite desactivarlo.
Resuelve el repositorio y fila nuevamente en cada operación y sólo escribe
una palabra alineada si el tipo es `ATK_PARAM_ST`, el stride retail es 164 y
el radio conserva los bits de 0,4 m originales. Valores personalizados y
0,6 m ya aplicados permanecen intactos. Comprueba el repositorio cada cinco
segundos sobre el hilo propietario del pacer, fuera de carga; no crea hilos.
No cambia archivos del juego. Un arranque a 30 sin conversión no instala el
servicio; la comprobación real de ese control aún está pendiente.

Pasan los guards de escritura, sólo cuatro bytes, idempotencia, radio de mod,
memoria de sólo lectura y repositorio recargado. Se comparte el fixture del
repositorio entre las pruebas para no duplicar su construcción. La referencia
offline de los tests usa el editor PARAMDEF existente y confirma las otras 64
tablas intactas y prioridad de mods. **No está conectada al setup**: la
corrección normal se aplica después de cargar los mods, mediante el servicio
nativo. ASan/UBSan pasa; la suite completa pasa **98 CTest y 331 Python**.

La captura `out/native-physical-bloodletter-120/manifest.json` pasa en monitor
1440p y audio USB: 7.200 frames, 59,998865 FPS, p99 16,71 ms y peor 16,73 ms.
Una lectura dentro del juego confirma `0x3f19999a` (0,6 m) y la palabra vecina
cero, como en el archivo original. El log registra la corrección y se conservan
los binarios anteriores a la siguiente optimización en
`out/verified-native-bloodletter-before-batching-2026-11-05`.
Esto verifica el dato cargado, no la ejecución del aura de frenesí con el arma;
su ensayo de L2 transformado en una copia del guardado queda pendiente.

El lector compartido ahora consulta metadatos en lotes de hasta 128 registros,
usando como máximo 3 KiB de pila para ese bloque. Conserva búsqueda secuencial
para IDs repetidos/desordenados, rechaza offsets inválidos y no conserva
punteros entre llamadas ni añade caché de heap. La regresión nueva cubre 2.049
IDs descendentes, un duplicado anterior y un registro roto antes del resultado;
la consulta larga queda en 18 lecturas en lugar de miles. Pasan ASan/UBSan y
la suite completa de **98 CTest y 331 Python** sobre esta nueva compilación
(`out/params-batch-check-all.log`).

El microbenchmark `out/params-query-bench-manifest.json` compara los dos
binarios reales con la tabla AtkParam_Pc original y `process_vm_readv`, orden
A–B–B–A: 3.302 → 15 lecturas por consulta y CPU de 917,97 → 5,98 µs.
Sólo mide esa consulta sobre un repositorio de prueba, no todo el juego ni
consumo eléctrico. La reserva TLS extra del proceso de benchmark permite
cargar las dos bibliotecas a la vez; no modifica los defaults del juego.
La captura física de la compilación con lectura por lotes sigue en curso.

La captura física de lectura por lotes ya terminó y pasa todos sus gates,
con 7,200 frames, 59.992876 FPS,
p99 16.71 ms y peor 16.73 ms.
La corrección Bloodletter cargada y su vecino se volvieron a comprobar.
El audio USB, cierre, guardados originales, hashes y restauración de pantalla
pasan. Evidencia: `out/native-physical-params-batch-120/manifest.json`.
Candidato congelado: `out/verified-native-param-batches-2026-11-05`.

Los controles nativos adicionales también pasan con los mismos tres binarios:
`out/native-bloodletter-controls/manifest.json`. Se prepararon realmente dos
perfiles aislados con el pipeline normal, conservando los cuatro mods del
usuario. El preset 30 no instala conversión: se leyó 0,4 m en el juego y el
muestreo disponible contiene 591 frames a 30,0015 FPS. El segundo perfil añade
un mod privado de radio 0,8 m sobre los otros mods: se conserva 0,8 m con
conversión activa, 1.179 frames muestreados a 59,9997 FPS y ningún log de
sobrescritura. No se fuerza `BB_BLOODLETTER_SELF_HIT=0` en estos controles.

Ambas ejecuciones mantienen los guardados originales y los hashes, sin fallos
de ejecución/cierre. Son controles funcionales de dato y modo; no certifican
una ventana completa de rendimiento ni la ejecución del L2 transformado.
Se conserva su vínculo a los binarios en
`out/verified-native-param-batches-2026-11-05/qualification-controls.json`.
El siguiente ensayo del arma debe observar su efecto 5286/5296 disparado por
la acción original. La referencia local de parámetros identifica Bloodletter
como arma 29000000, `Carnicera` en `spaar`, requisitos base 14 de fuerza y 16
del campo `properMagic`; el lector de jugador mapea viveza/bloodtinge como
atributo 4. No se han concedido armas ni alterado atributos en estos controles.


### Política revisada tras combate real de Carnicera (2026-11-06)

La corrección de radio queda **desactivada por defecto**: sólo se instala con
conversión activa y `BB_BLOODLETTER_SELF_HIT=1`. El radio original de 0,4 m
activa naturalmente el efecto 5286 en los tres ataques de cada control:
30 FPS, 60 FPS con reloj normal y 60 FPS con reloj experimental realmente
instalado (YEBIS, ragdoll y lens). No se ha reproducido una necesidad de
ampliarlo automáticamente en estos estados. Estos nueve ataques no demuestran
que todas las situaciones o tasas estén cubiertas.

Los resultados y observaciones están en `out/bloodletter-ui-owned-30-baseline`,
`out/bloodletter-ui-owned-60-unfixed` y
`out/bloodletter-ui-owned-60-clock-unfixed`. Los tres procesos cerraron sin
fallos y conservaron guardados originales y hashes. Usan la misma copia con
arma equipada y atributos suficientes; el observador sólo lee los efectos,
no los aplica. Cada ronda vuelve al Sueño para restaurar salud y frenesí.
Son controles funcionales, no ventanas certificadas de rendimiento.

Se observó antes una activación con radio corregido en
`out/bloodletter-ui-owned-60-created/ui-evidence.json`. Su ejecución completa
falló posteriormente por MemoryWatch; el `result.json` fallido se conserva.
No se atribuye la presión de memoria a una causa no demostrada ni se usa esa
sesión como certificado de rendimiento. Las capturas anteriores de radio
corregido permanecen vinculadas a sus propios binarios y política antigua.

### Puente de llamada escalar nativa para futuras utilidades de plugins

`BbGuestCall::Invoke` adapta el frame de BBHOST: seis registros enteros, ocho
valores XMM bajos, cuatro palabras de pila y resultados RAX/XMM0. Mantiene
alineación SysV, indica ocho registros vectoriales a callees variádicos y
preserva registros no volátiles y metadatos CFI. No cambia FS: el runtime
nativo conserva FS de host y usa GS para TLS del juego. No crea hilos ni añade
trabajo por frame; sólo actúa cuando se llama explícitamente.

La prueba escalar cubre argumentos ponderados para detectar intercambios,
float, variádicos, alineación de entrada, recursión y ocho hilos concurrentes.
La prueba de desenrollado C++ usa un callee de host y no afirma soporte de
excepciones del juego. ASan/UBSan pasa con la instrumentación de tipos de
función desactivada para el símbolo ensamblador sin metadata de compilador.
La comparación de precio retail añade la llamada por registros junto a la
llamada tipada para cada umbral de los 544 niveles. La suite con ELF original
pasa: **99 CTest sin omisiones y 331 Python**, además de lectura Vulkan
de 4.096 bytes (`out/guest-call-optin-retail-check-all.log`). El adaptador público, validación de contexto/hilo y ciclo de
vida de módulos siguen pendientes; esto no implementa el host API 16 completo.


La compilación nueva pasa también todos los gates físicos en Mensis:
`out/native-physical-guest-call-optin-120/manifest.json`, 7.200 frames a
59,998655 FPS, p99 16,71 ms y peor frame 16,74 ms. FSR 4.1 FP8 t2160_m0,
render y salida 2560×1440, audio USB sin starvation y monitor físico estable.
El radio cargado conserva 0,4 m y vecino cero sin instalar el observador de
corrección. Cierre, guardados originales, hashes y restauración de pantalla
pasan. El candidato se conserva en
`out/verified-native-guest-call-optin-2026-11-06`; no es la release final ni
una certificación de todas las áreas o del host completo de plugins.


El perfil local publicado `c80433a0546443698b74d5ab7584d1b9` añade preferencia
por `Microstep MSI MAG 275QF` en arranque, ajustes y defaults personales.
El setup normal validó de nuevo los 26.913 DCX y conserva los cuatro mods.
Sus cuatro artefactos compilados son byte a byte idénticos a los de la
medición física anterior; no se extrapola una medición nueva de rendimiento.
La publicación y respaldo quedan en
`out/physical-monitor-profile-publication.json`. El monitor se selecciona
cuando está activo; la configuración de escritorio virtual fue restaurada.


### Retirada de callbacks por propietario (2026-11-06)

`BbPluginCallbacks::Registry` prepara la integración segura de `on_frame`.
El despachador existente sigue destinado a tareas que viven hasta terminar el
proceso. El nuevo registro acepta un pin compartido que debe poseer la
referencia de `dlopen`; propietarios, entradas y snapshots mantienen ese pin.
Cierra la admisión antes de retirar entradas y espera callbacks activos desde
fuera del despacho. Dentro de cualquier callback, incluso de otro registro,
retira sin esperar y devuelve false: el host debe completar el drenaje fuera
del callback. Así evita esperarse a sí mismo o formar esperas cruzadas.

La publicación usa copy-on-write; el despacho no reserva memoria ni copia
vectores. La capacidad es de 256 entradas, con deduplicación por propietario,
función y userdata. Una excepción desactiva al propietario y devuelve un
contador de fallos; el host debe retirarlo después para liberar sus entradas.
El adaptador `InstallFrames` reutiliza `BbFrameTasks` y sólo se instala cuando
se pide explícitamente. El juego normal todavía no instala tarea de plugins.

La prueba carga una biblioteca C real, detiene su callback y verifica que la
retirada espera y no descarga código ejecutándose. Otra ejecución suelta el
propietario desde el propio callback: el snapshot conserva el módulo durante
el retorno a código del plugin y sólo después permite descargarlo. Se comprueba
la descarga con `RTLD_NOLOAD`. También se cubren concurrencia, rechazo de otros
registros, cupo/deduplicación, reutilización, autorretirada y excepciones.
ASan/UBSan con detección de fugas pasa. La prueba de entrada retail ejecuta
el prólogo/trampolín real y sustituye el cuerpo posterior del pacer; no se
presenta como validación de un plugin completo dentro de una sesión de juego.

Faltan el adaptador API público y conectar la retirada a todos los caminos de
fallo y descarga del host. Los hooks, HLE y threads de módulos requieren su
propia residencia; el registro de callbacks no certifica por sí solo que un
módulo completo sea descargable.


Contrato de userdata: el host debe conservar válidos todos los punteros de
usuario hasta completar el drenaje fuera del despacho, o incluir su propiedad
en el recurso compartido que fija la residencia. Mantener cargada una DSO no
conserva por sí solo un objeto que el plugin haya liberado prematuramente.
No se permite destruir el registro mientras otro hilo ejecuta sus métodos.

La revisión de metadata de GitHub de hoy conserva master
`bdc5cdf8be5f37188da9c2368e92be444fdc04cb` y la PR213
`04aea6ddced70c9b645f130e4c9382d9f79dff83`. Los 16 PR abiertos se registran en
`out/bbhost-open-pr-refresh-2026-11-06.json`; esta revisión de metadata no
sustituye la comparación de código de cada propuesta.


La suite final pasa **101 CTest sin omisiones y 331 Python**, más envío/lectura
Vulkan de 4.096 bytes (`out/plugin-callbacks-final-check-all.log`). La nueva
compilación pasa todos los gates de la regresión física normal de 120 segundos:
`out/native-physical-plugin-callbacks-120/manifest.json`, 7.200 frames,
59,992101 FPS, p99 16,71 ms y peor 20,43 ms. Este pico aislado se conserva,
sin atribuirlo al nuevo código ni afirmar ejecución perfecta en todo el juego.
Audio USB sin starvation, monitor físico estable, guardados originales,
hashes, cierre y restauración de pantalla pasan. El registro de plugins no
se instala por defecto; la regresión no se presenta como prueba de un host
completo en vivo. Candidato: `out/verified-native-plugin-callbacks-2026-11-06`.


### Licencia de las adaptaciones BBHOST corregida (2026-11-06)

El README de BBHOST declara **GPL versión 3 o posterior**, tanto en b0098e0
como en bdc5cdf8; su LICENSE contiene GPLv3. Las atribuciones anteriores que
indicaban GPLv2 para esas adaptaciones eran incorrectas. Se corrigen NOTICE y
los identificadores de 60 archivos revisados, con inventario en
`docs/BBHOST_LICENSE_AUDIT.json`. Los archivos originales de bbport conservan
su permiso GPLv2 o posterior; en archivos mixtos se expresan ambas condiciones.
El texto original LICENSE no se sustituye: el texto GPLv3 se incluye además en
`LICENSES/GPL-3.0-or-later.txt`. No se cambia la atribución de forks ajenos ni
las licencias de terceros. Las revisiones/grants y el contenido del texto de
licencia se verificaron directamente contra el repositorio de BBHOST.
La futura release deberá incluir los avisos, textos y fuentes correspondientes;
esta corrección no constituye una release ya publicada.

### Cargador de módulos en dos fases y contexto propietario

`BbPluginModule::Module` carga bibliotecas con los símbolos originales
`bb_plugin_init` / `bb_plugin_image`. Conserva el header C público de BBHOST
sin cambios salvo comentarios de procedencia/licencia, valida versión y tamaño
del prefijo API y copia el nombre antes de cualquier descarga. El integrador
debe suministrar una tabla de servicios veraz que dure toda la actividad del
plugin: este cargador no anuncia ni construye un host API16 completo.
No hay descubrimiento/carga automática desde el arranque normal todavía.

Los callbacks registrados durante las fases se asocian a su propietario.
El despacho establece el mismo contexto, de modo que un callback que registre
otro callback no pierde la propiedad. Los contextos son locales al hilo y se
anidan; uno inválido bloquea el registro en vez de atribuirlo al plugin exterior.
Fallar una fase, por retorno o excepción, retira todos sus callbacks. Un pin
local conserva la biblioteca hasta destruir también la excepción: su destructor
y metadata pueden pertenecer al código del plugin, incluso después de Close.

La política predeterminada conserva la DSO hasta terminar el proceso, incluso
si la fase falla; sólo sus callbacks quedan desactivados/retirados. Así no se
supone que hooks, HLE o threads desconocidos sean descargables. La opción
CallbacksOnly requiere que el integrador garantice que sólo existen callbacks
como recursos asíncronos. Las pruebas usan ese contrato explícito para
verificar descargas reales, y prueban al final la residencia predeterminada.
Se cubren fallo de init/image, excepciones con destructor dentro de la DSO,
versión incompatible, tabla truncada, símbolos ausentes, doble fase/cierre,
limpieza por destructor, publicación desde callbacks y falta de contexto.

La primera suite (`out/plugin-module-check-all.log`) conservó un fallo de
fixture: se había compilado la biblioteca antes de añadir el destructor de
excepción, y el ejecutable de test después. La pasada estable siguiente
(`out/plugin-module-final-check-all.log`) pasó 102 CTest. Tras corregir el
contexto inválido se ejecuta la suite definitiva de esta versión, sin modificar
sus fuentes mientras se compila. ASan/UBSan con detección de fugas pasa para
el módulo; la regresión final del registro y del juego se comprueban aparte.


La suite definitiva pasa **102 CTest sin omisiones y 331 Python**, lectura
Vulkan de 4.096 bytes y ASan/UBSan de registro/módulo con detección de fugas.
Evidencia: `out/plugin-module-owner-check-all.log` y
`out/plugin-module-validation.json`. La regresión física normal de 120 segundos
pasa todos los gates en Mensis: 7,200 frames, 59.992326 FPS,
p99 16.71 ms y peor 16.73 ms. Render/salida 2560×1440,
FSR4.1 FP8 t2160_m0, audio USB, monitor, hashes, guardados originales, cierre y
restauración pasan. No se instala todavía un host completo en el arranque
normal. Candidato conservado: `out/verified-native-plugin-module-2026-11-06`; incluye avisos y
textos de licencia y sigue sin constituir la release final.


### Reemplazos HLE con retirada de imports ya enlazados (2026-11-06)

`src/runtime_plugin_hle.c` se conecta al resolvedor nativo de funciones y al
cargador de módulos. `Module::RegisterHle` sólo acepta registros dentro de init,
con contexto del módulo y política de residencia hasta terminar el proceso.
El probe instala ese backend antes del punto reservado para cargar plugins y
sella los registros antes de enlazar imports. No carga automáticamente módulos
ni anuncia por sí solo una tabla completa de servicios.

Cada símbolo conserva hasta ocho capas propietarias, de las que gana el último
registro. Retirar un propietario restaura la capa anterior o el proveedor nativo.
Un import enlazado recibe un trampolín estable que salta indirectamente por un
destino atómico; retirar después de enlazar cambia ese destino y no requiere
buscar/modificar los slots del ejecutable. No altera registros enteros, XMM,
flags, pila ni AL de variádicos. Las llamadas ya ejecutándose necesitan que el
módulo permanezca residente; esta implementación no pretende descargarlas ni
interrumpirlas. El backend no intercepta datos ni supera el gate de capabilities.
Rechaza nombres fuera del catálogo nativo; no inventa implementaciones para
funciones sin proveedor nativo.

La primera inscripción reserva el estado acotado. Los trampolines se asignan
sólo cuando se enlaza un reemplazo: una página RX con código y otra RW sin
permiso de ejecución para destinos. El límite es de 256 símbolos y nombres de
255 bytes. Sin inscripciones no se reserva ese estado ni se hace búsqueda extra
de nombres; no se añaden hilos ni trabajo de frame por defecto.

La prueba del runtime real valida staging/sello, límites, prioridad y retirada,
restauración mediante el mismo puntero cacheado, alias de librería, datos y
capabilities, retorno mixto RAX/XMM0, diez argumentos enteros, ocho flotantes,
pila y AL variádico. Otra llamada permanece dentro del reemplazo mientras se
retira y una nueva llamada entra al proveedor nativo. El código generado se
comprueba RX sin escritura en /proc/self/maps.

Tres casos adicionales ejecutan la cadena completa con una DSO de plugin real,
`Module`, backend y resolvedor nativos: fallo de init antes de enlazar, fallo de
image después de cachear el import y éxito/cierre. Los tres pasan; el fixture
expone sólo los servicios que usa, sin anunciar un host de producción completo.
La suite completa pasa **105 CTest sin omisiones y 331 Python**, más lectura
Vulkan de 4.096 bytes (`out/plugin-hle-integration-check-all.log`). ASan/UBSan
pasa para el runtime nativo y para el módulo. En el runtime se desactiva sólo
la comprobación de metadata de tipos de función en entradas generadas sin esa
metadata; leak checking se desactiva para ese fixture con estado de proceso.
La prueba de módulo mantiene detección de fugas. Dos intentos de link del
fixture ASan necesitaron mold y -flto=thin para el archivo Atrac9 ya compilado
con ThinLTO; el binario instrumentado final ejecuta y pasa.

La regresión física de esta compilación queda registrada por separado; los
resultados de módulos/HLE no se presentan como un host completo jugando una
sesión real. La tabla pública, descubrimiento/configuración y servicios restantes
siguen pendientes.


La regresión física pasa todos los gates sobre la compilación nueva:
`out/native-physical-plugin-hle-120/manifest.json`, 7,200 frames,
59.992551 FPS, p99 16.71 ms y peor 16.73 ms.
Render/salida 2560×1440, FSR4.1 FP8 t2160_m0, monitor físico y audio USB pasan;
no hay starvation, fallos de ejecución/cierre ni cambios en guardados originales
o hashes. La pantalla fue restaurada. El candidato se conserva en
`out/verified-native-plugin-hle-2026-11-06`. Sigue sin ser una release final ni una prueba
de todas las áreas o de la tabla pública completa de plugins.


### Identidad/acceso de imagen y escritura compartida de parches (2026-11-06)

Se corrige un fallo real del publicador de hooks: ignoraba mprotect antes de
escribir. Ahora usa `BbGuestPatch::Write`, el mismo escritor de los parches de
imagen. Lee con comprobación de errores los buffers y el destino, verifica
expected/idempotencia, admite todos los permisos antes de modificar bytes y
restaura las protecciones de cada mapping. Rechaza escrituras de más de 16 MiB
y rangos que desbordan. No se afirma atomicidad de parches en código ejecutándose.
Si falla la escritura se restaura el contenido; una imposibilidad de rollback
o restauración de permisos detiene el proceso en lugar de continuar corrupto.

Una prueba cruza dos páginas con permisos distintos. Otra permite abrir la
primera página, pero la segunda procede de un FD sólo lectura con MAP_SHARED:
se rechaza sin cambiar bytes y se restaura RX en la primera. El test de hooks
usa realmente ese tipo de archivo ejecutable: recibe false, original=null,
conserva una función ejecutable que devuelve su resultado original y permite
repetir el intento sin registrar un hook inexistente. El trampolín no publicado
se rebobina. Los hooks normales del juego ya utilizan esta ruta compartida.

`BbPluginImage::Image` analiza un ELF original acotado y reconstruye PT_LOAD/
SCE_RELRO con BSS cero para comparar la identidad cargable con el mismo hash
071df19c de game_check.py. Conserva por separado el SHA real del archivo. El
ELF reconstruido local tiene cec1b276, mientras el archivo de referencia de
BBHOST tiene 941f887a: no se falsifica ese dato. Plugins que comparen un hash
de archivo directamente requieren revisión de compatibilidad. El test confirma
la identidad del ELF real local y que añadir relleno fuera de segmentos altera
sólo el hash de archivo. Se verifican BN/base alta, punteros runtime de imagen,
fallos de cabecera/rangos y heaps únicamente aprobados por el backend del host.
Parches esperados sobre una copia reubicada de un prólogo retail pasan e
idempotencia/restauración también; esto no equivale a un plugin público jugando
una sesión real. Datos-write y los adaptadores ABI/propiedad de hooks quedan
pendientes.

ASan/UBSan con detección de fugas pasa para ambos módulos. La suite definitiva
pasa **107 CTest sin omisiones y 331 Python**, más lectura Vulkan de 4.096 bytes
(`out/plugin-image-patch-final-check-all.log`). La primera pasada quedó en
`out/plugin-image-patch-check-all.log`: faltaba enlazar SHA256 en la biblioteca
nativa porque antes se excluía como helper sólo offline. Se reutiliza ahora la
misma implementación tanto en los targets offline como en la biblioteca GPU.

La regresión física pasa todos los gates: 7,200 frames,
59.994266 FPS, p99 16.71 ms y peor 16.73 ms.
Render/salida 2560×1440, FSR4.1 FP8 t2160_m0, audio USB sin starvation y monitor
físico pasan. Guardados originales, hashes, ejecución/cierre y restauración de
pantalla pasan. Candidato: `out/verified-native-plugin-image-patch-2026-11-06`. Sigue pendiente la
tabla pública completa del host y la validación amplia antes de la release.


### Adaptadores hook/hook2 con propiedad y drenaje compartidos (2026-11-06)

`BbPluginHooks::Hooks` adapta los tipos C originales de BBHOST al snapshot y
la decisión local de `Core::InstallConditionalGuestCodeHook`. Hook1 copia sus
seis argumentos y devuelve RAX cero cuando omite. Hook2 copia seis GPR y ocho
valores XMM bajos; permite cambiar argumentos y resultado RAX al omitir, sin
alterar los bits superiores YMM. Los cambios se publican sólo si el callback
retorna normalmente. Una excepción deja intactos los argumentos originales,
desactiva los callbacks de su propietario y deja pasar la función original.

Frames y hooks comparten `Registry::Invocation`: admisión, recuento en vuelo y
contexto de retirada no bloqueante. Module::Close retira juntos los callbacks
activos. El binding y trampolín permanecen para siempre porque el código
publicado los referencia; los retirados no vuelven a tocar userdata ni ejecutan
el callback. Se exige política de módulo residente y fase image antes de iniciar
threads del juego. No hay suspensión de threads ni instalación de hooks en
runtime. Límites: 256 bindings, prólogos de14..64 bytes completos sin referencias
relativas; hook2 admite prólogo NULL leyendo los bytes actuales con comprobación.

El fixture carga una DSO real, pasa por Module y ejecuta trampolines reales sobre
funciones de prueba dentro de una imagen reubicada verificada contra el ELF1.09.
No son los cuerpos retail del juego ni se presenta como host completo jugando.
Prueba entrada/salida ponderadas de los seis GPR, argumentos y retorno flotante,
skip/continuación, recursión y16.000 llamadas de ocho threads. Un callback se
detiene mientras otro thread cierra el módulo: nuevas llamadas van al original,
el cierre espera al callback activo y después se libera realmente userdata.
También verifica propiedad de un frame registrado desde un hook, argumentos
intactos tras excepción y hooks inertes tras fallo de image.

Pasan ASan/UBSan con detección de fugas para DSO, adaptadores, registro, módulo,
imagen/escritor y generador nativo. Se desactiva la comprobación de metadata de
tipos de función en código generado sin esa metadata. El decoder y dependencias
preexistentes no están completamente instrumentados. La suite pasa **108 CTest
sin omisiones y331 Python**, lectura Vulkan4.096 bytes. Evidencias:
`out/plugin-hooks-check-all.log`, `out/plugin-hooks-asan.log` y
`out/plugin-hooks-validation.json`. La compilación manual ASan necesitó añadir
el include de Xbyak del árbol de dependencias de CMake; el ejecutable final pasa.

La cuarentena actual desactiva callbacks. Los HLE se restauran al ejecutar
Module::Close; automatizar la limpieza de todos los recursos al poner un
propietario en cuarentena sigue pendiente junto con la tabla pública y el
arranque del host. La regresión física de esta compilación se registra aparte.


La regresión física de esta compilación pasa todos los gates:
`out/native-physical-plugin-hooks-abi-120/manifest.json`, 7,200 frames,
59.991916 FPS, p99 16.72 ms y peor 17.31 ms.
Se conservan render/salida2560×1440, FSR4.1 FP8 t2160_m0, monitor físico y audio
USB sin starvation. Guardados originales, hashes, ejecución/cierre y restauración
de pantalla pasan. El pico17,31 ms queda registrado, sin atribuirle una causa
ni convertir una ruta en certificación de todas las áreas. Candidato:
`out/verified-native-plugin-hooks-abi-2026-11-06`. La prueba física usa el arranque normal sin cargar
un host público de plugins; los adaptadores se verificaron por separado con
la DSO real y funciones de guest de prueba. La release final sigue pendiente.
