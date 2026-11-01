# Auditoría del perfil de 60 FPS — 2026-11-01

Se cambió el perfil por defecto y el botón recomendado a 60 FPS, con un límite de
60 y un presupuesto nominal de 16,667 ms. La estabilidad real del frame-time y
`stat_starved=0` requieren mediciones en partida en la máquina objetivo.

## Restricciones comprobadas en esta versión

- El perfil de 16 GB admite un pool directo de 5056 MiB. El renderizado interno
  2560×1440 solicita 9152 MiB y la validación lo rechaza. Se conserva la salida
  2560×1440 con escena 1706×960 y FSR Quality, sin quitar esta protección.
- `preset=1` corresponde a Quality, escala 1,5; `preset=0` corresponde a nativo.
- `anti_aliasing=smaa` queda seleccionado; SMAA se ejecuta con el escalador
  desactivado. FSR aporta su propio antialiasing temporal.
- No existe `gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp` ni el path
  opcional `BB_EARLY_SUBMIT`. El renderizador local está en `gpu/render`.
  No se modifica el envío de fences necesario para la sincronización Vulkan.
- El fix Intel ya detecta `GenuineIntel`, independientemente del fabricante de
  la GPU. Los hooks dinámicos ya incluyen delta-tiempo, pasos fijos, sprint y
  rigidez de telas; con hooks activos no se selecciona `60 FPS++` para el eboot.
- `src/runtime_file.c` solo redirige bancos FSB de voz hacia alternativas existentes
  y no vacías. AvPlayer (`gpu/platform/avplayer`) decodifica AAC mediante FFmpeg
  y programa vídeo según PTS y reloj estable, independientemente del FPS del juego.
  AJM (`src/runtime_ajm.c`) admite ATRAC9; sus instancias MP3/AAC siguen sin soporte.
- Las preferencias guardadas y generaciones preparadas son persistentes. Cambiar
  los defaults del repositorio no sustituye silenciosamente un perfil personal.

## Cambios por archivo

| Archivo | Cambio y justificación |
| --- | --- |
| `launcher/cachyborne.py` | Defaults y perfil recomendado 60/60. Proyecta hooks activos; limpia overrides antiguos de hooks/VBlank al aplicar el preset. Restablece las opciones de calidad del preset conservando controles y rutas. Mantiene VBlank virtual a 1000 Hz y RADV anisotrópico 16×. |
| `launcher/cachyborne_options.py` | Defaults legacy 60/60; texturas full, GT-VBAO, SMAA, preset 1, RCAS 0,35; aberración cromática, DoF y motion blur apagados. |
| `launcher/cachyborne_gui.py` | Botón recomendado, textos y métricas a 60 FPS y 16,67 ms. El combo toma los defaults compartidos. |
| `launcher/bbport_launcher_common.py` | Valores compartidos de la GUI original a 60/60 y nueva calidad gráfica; `bbport_launcher.py` los importa y no necesita duplicarlos. |
| `launcher/bbport_launcher_win.py` | Sincroniza los defaults duplicados del launcher Windows, incluyendo efectos y nitidez. |
| `launcher/settings_schema.json` | Defaults de FPS y gráficos; conserva tipos booleanos, numéricos y cadenas del esquema. |
| `gpu/shim/bbport_frame_rate.cpp` | Sin variable explícita, los hooks limitan a 60. Conserva hooks de telas y delta-tiempo; no añade parches invasivos. |
| `gpu/shim/bbport_frame_rate.h` | Comentario del default actualizado. |
| `gpu/shim/bbport_settings.h` | Defaults de respaldo C++ coherentes para calidad, nitidez, oclusión, efectos y cuadrícula de luces. |
| `gpu/shim/bbport_affinity.cpp` | El monitor de reserva de cores cede siempre a la política P/E activa, incluso si existe `BB_CORE_RESERVE`. Evita que sobrescriba la afinidad del audio. |
| `src/cachy_affinity.h` | Los nombres audio/FMOD tienen prioridad sobre la clasificación de trabajadores de fondo. |
| `src/runtime_audio.c` | Precarga SDL antes del arranque: 1024 frames más dos buffers, 32 ms con buffers de 256 frames. Fija el hilo llamante en P-Cores antes de crear hilos SDL. Mutex por puerto inicializado una sola vez y bloqueo bajo protección de la tabla; protege estado y timestamps. Contadores globales atómicos. Corrige contabilidad del relleno, comprueba errores SDL, reinicia pacing tras retrasos de más de un periodo y evita alterar deadlines de puertos secundarios. Reintenta el sueño solo ante EINTR; mide la cola después del pacing. |
| `scripts/patches.py` | CLI con FPS 60 por defecto. Conserva la selección vacía de parches FPS con hooks activos y la detección del tonemap Intel. |
| `gpu/shim/bb_env_vars.def` | Documenta el default BB_FPS=60. |
| `docs/ENVIRONMENT.md` | Regenerado con el inventario de variables. |
| `README.md` | Perfil 60 FPS, opciones de calidad y límites de memoria/SMAA/audio. |
| `README.es.md` | Misma actualización en español. |
| `docs/CONFIGURATION.md` | Explica pacing, VBlank, reservas de audio, afinidad, PTS, memoria y procedimiento de medición. |
| `tests/test_audio.c` | Prueba real con streams SDL y dispositivo dummy pausado: reserva inicial, PCM entero/float de 8 canales, layouts estándar/PS4, límites de volumen, relleno, reapertura, pacing por lotes y recuperación de retrasos. Incluye modo manual `--device-smoke` para probar el dispositivo real con silencio. |
| `tests/test_audio_runtime.py` | Compila y ejecuta esa regresión dentro de la suite unittest; requiere compilador C y desarrollo SDL3. |
| `tests/test_cachy_affinity.c` | Comprueba P-Cores para FMOD, SDL y el caso de nombre FMOD Decompress. |
| `tests/test_env_vars.py` | Comprueba la proyección 60/60, hooks, VBlank virtual, máscara P/E, pool acotado y anisotropía. |
| `tests/test_midnight_settings.py` | Expectativas del nuevo perfil y calidad recomendada. |
| `tests/test_run_settings.py` | Defaults a 60 y cobertura de objetivos altos explícitos sin heredar accidentalmente el límite 60. |
| `tests/test_settings.cpp` | Verifica los defaults gráficos C++ de respaldo. |
| `tests/test_upstream_07_settings.py` | Coherencia entre defaults nativos, esquema, hooks y presupuesto de memoria; comprueba que preset 1 no es nativo. |
| `docs/AUDIT_60FPS.md` | Este informe y los comandos reproducibles. |

Los parches FSR versionados que `build.sh` aplica automáticamente al submódulo
se usaron durante la compilación. Después se retiraron únicamente esos cambios
generados; el submódulo queda limpio y la siguiente compilación los reaplicará.

## Verificación realizada

| Comprobación | Resultado |
| --- | --- |
| `PYTHONPATH=launcher:tests:. python3 -m unittest discover tests/` | 231 pruebas, OK |
| `bash build.sh --test` | Compilación Clang/ThinLTO/mold y pruebas nativas, OK |
| `ninja -C out/gpu -j 4 cachyborne-tests` | Targets de pruebas compilados, OK |
| `ctest --test-dir out/gpu -L cachyborne --output-on-failure` | 24/24, OK |
| Política de afinidad compilada y ejecutada | OK |
| Wrapper público `cachyborne-launch.sh --dry-run` con perfil fixture publicado en directorio aislado | Ocho aserciones de entorno, OK; no se ejecutó el juego |
| `cachyborne-launch --dry-run` instalado, después de regenerar la preparación real | Once valores de entorno, catorce ajustes gráficos, requisitos de ejecución y sintaxis del plan shell, OK |
| Perfil activo, preferencias y defaults personales | 60/60, calidad solicitada y backend PipeWire coherentes |
| Dump original usado para preparar el perfil | 26.913 archivos DCX revisados, cero dañados |
| Prueba de audio con AddressSanitizer y UBSan | OK, sin diagnósticos |
| `SDL_AUDIODRIVER=pipewire BB_CPU_PINNING=1 BB_P_CPUS=0-11 out/audit-audio-test --device-smoke` | 2000 buffers; ambas ventanas con starved 0; gap máximo observado 5,51 ms |
| `out/bb-probe --vulkan-only` | RX 9070 XT RADV GFX1201; envío y readback de 4096 bytes, PASS |
| `git diff --check` | OK |

Los logs quedan en `out/audit-python-tests.log`, `out/audit-build-tests.log`,
`out/audit-cpp-build.log`, `out/audit-ctest.log`, `out/audit-audio-sanitized.log`,
`out/audit-pipewire.log` y `out/audit-vulkan-smoke.log`. El dry-run de la fixture queda en
`out/audit-dry-run.json`; sus rutas temporales sirven como evidencia de la prueba,
no como perfil instalable. La evidencia del perfil real está en
`out/audit-local-dry-run.json`, `out/audit-local-verification.json` y
`out/audit-local-setup.log`. El diff completo queda en `out/audit-60fps.patch`.

No se midieron escenas del juego ni frame-times. Sí se probó PipeWire con silencio
en el dispositivo real durante 2000 buffers: no hubo vaciados en las ventanas
registradas. Esta prueba acotada no garantiza cero clicks/pops durante una partida.
Los buffers de reserva añaden latencia; no se fuerza una política realtime
privilegiada. Se respetan la máscara del cpuset y la opción de desactivar pinning.

## Configuración aplicada en el PC objetivo

Se regeneró y publicó una nueva preparación desde el dump original, conservando
los cuatro mods seleccionados, bindings, HDR y rutas de guardado. Después se
guardaron las mismas opciones en `settings.json` y `defaults.json`. El perfil
personal fija `SDL_AUDIODRIVER=pipewire`; este ajuste específico del equipo no
se fuerza en los defaults portables del repositorio.

La copia previa de la configuración está en
`~/.config/cachyborne/backups/60fps-20261101-015940/`; conserva también el enlace
al perfil anterior. No se borraron las preparaciones anteriores. Los accesos
del escritorio y los wrappers instalados apuntan a este checkout.

Los ajustes personales y artefactos del juego quedan fuera de Git. Los commits
se separan en audio/afinidad, perfil/valores por defecto y documentación.

## Aplicar y comprobar el perfil real

Para migrar una preparación existente, usa tu dump descifrado 1.09:

```sh
cachyborne-setup /ruta/al/app0 --performance-profile 60
cachyborne-launch --dry-run | python3 -c '
import json, pathlib, sys
result = json.load(sys.stdin)
env = result["environment"]
expected = dict(BB_FPS="60", BB_FPS_LIMIT="60", BB_FRAME_HOOKS="1",
                BB_VBLANK_HZ="1000", BB_DMEM_MB="5056", BB_P_CPUS="0-11",
                BB_E_CPUS="12-19", RADV_TEX_ANISO="16")
for key, value in expected.items():
    assert env.get(key) == value, (key, env.get(key), value)
ini = dict(line.split("=", 1) for line in pathlib.Path(env["BB_CONFIG"]).read_text().splitlines()
           if "=" in line and not line.lstrip().startswith(("#", ";")))
quality = dict(texture_quality="full", vbao="1", anti_aliasing="smaa", preset="1",
               sharpen="1", sharpness="0.35", effect_ssao="1", effect_dynamic_shadows="1",
               effect_bloom="1", light_grid_opt="1", effect_chromatic_aberration="0",
               effect_motion_blur="0", effect_dof="0", frame_generation="off")
for key, value in quality.items():
    assert ini.get(key) == value, (key, ini.get(key), value)
print("OK: perfil 60 FPS y calidad verificados")
'
```

Si no se han instalado los wrappers, usa `./cachyborne-setup.sh` y
`./cachyborne-launch.sh` desde la raíz del repositorio. Un override de entorno
explícito o una preparación antigua puede hacer fallar estas aserciones; el
comando permite identificar exactamente qué valor difiere.

Para aceptación en el equipo objetivo:

```sh
BB_AUDIO_STATS=1 BB_SAVE_LOG=1 cachyborne-launch
```

Comprueba `starved 0`, gaps de audio, frame-times, cargas de zona, menú de gestos,
telas, sprint, pausa/reanudación y sincronía de voces durante cinemáticas. Los
60 FPS corresponden al ritmo del juego; los timestamps AAC conservan el tiempo
original de la película.
