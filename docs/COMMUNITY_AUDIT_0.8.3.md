# Revisión de seguimiento — Cachyborne 0.8.3

Base local: `4c6d60f945e2bfb317b007a426e3701ad8d60922` (0.8.2).
Se inspeccionaron los commits mediante `git show`; las adaptaciones conservan la
arquitectura de Cachyborne. El objetivo es 1440p en RX 9070 XT/RADV, i5-14600KF
con afinidad P/E existente y 16 GB. 4K/ultrawide queda fuera del trabajo solicitado.
La [revisión anterior](COMMUNITY_AUDIT_0.8.2.md) conserva la procedencia de las
integraciones de 0.7, FSR, cámara y shaders.

## TIER 1 — Writeback de todos los mips residentes

1. **Referencia:** ayozetr/bloodborne_linux, autor ayozetr,
   `42ec043c3e69dc1deb2dca7d28302ebfd0368d40`, archivo original
   `gpu/shadps4/video_core/texture_cache/texture_cache.cpp`.
   Inspección: `git show 42ec043c -- '*texture_cache*'`.
2. **Mecanismo:** descargar únicamente mip 0 deja datos antiguos en los niveles
   pequeños tras una expulsión de caché. La adaptación planifica cada mip y corte,
   respetando offsets, pitch en bloques BC y dimensiones decrecientes. Mantiene
   el destino directo y los mirrors en orden; el fallback usa staging compacto y
   escribe exclusivamente las filas válidas. El cómputo compartido del alcance
   de una copia Vulkan redondea bloques y distingue bytes de depth/stencil.
3. **Hardware:** evita restauraciones incorrectas de texturas lejanas; reduce el
   staging respecto a copiar toda la distribución con relleno. No crea sombras
   de texturas persistentes, workers ni nuevas sumisiones en la ruta directa.
   Metadatos de los primeros 16 cortes viven en almacenamiento inline. La ruta
   CPU adquiere el bloqueo VMA e invalida buffers por corte, no por fila.
4. **Compatibilidad:** usa `image.info.mips_layout`, incluidos los offsets que
   conserva `Reduced()`. Stencil del render target tiene una asignación aparte,
   cuyo hash se incluye en el guard de GC. La dirección de stencil se actualiza
   al volver a enlazar el depth target, abriendo un nuevo intervalo de referencia. Las filas se validan antes de escribir
   bajo un bloqueo VMA; esto **no** excluye escrituras CPU directas concurrentes
   ni identifica toda reutilización interna del heap. Tiling/MSAA y stencil
   multinivel sin descriptor suficiente se retienen: no se inventa su layout.
5. **Código:** `gpu/textures/image_download.{h,cpp}`,
   `texture_cache_memory.cpp`, `image.cpp`, `gpu/render/runtime.cpp`,
   `gpu/shim/{bbgpu.cpp,core/memory.h}`, `src/runtime_memory/gpu_traps.c`.
   Pruebas: `test_image_download.cpp`, `test_image_write_baseline.cpp` y
   `test_runtime.c`. No requiere nueva clave JSON: corrige la coherencia existente.

## TIER 1 — Tiempo de partículas por pasos completos

1. **Referencia:** ayozetr/bloodborne_linux, ayozetr, coautor nick24051998,
   `a9f292a7e804237ffbab1e7c14009acbbe81354d`; `scripts/effect_time.py`,
   `scripts/patches.py`, `tests/test_effect_time.py` y documentación.
   Inspección directa de `git show a9f292a7:scripts/effect_time.py`.
2. **Mecanismo:** algunos efectos no vencen correctamente con pasos menores de
   1/60 s. Un acumulador conserva el resto y entrega 0, 1/60 o 2/60; la simulación
   general continúa usando el tiempo medido. No se actualizan partículas cuando
   todavía no hay un paso completo. Se conserva el flag de tiempo cero del juego.
3. **Hardware:** pertinente a 144 FPS; evita la causa descrita de acumulación de
   partículas y draws. Estado de tamaño constante, sin asignaciones por frame ni
   cambios de afinidad. No se atribuye una ganancia FPS sin comparación del boss.
4. **Compatibilidad:** el hook existente ya redirige `0x1f77d4a`. Se sustituye
   conjuntamente el branch y la carga en `0x1f77d48`, verificando los diez bytes
   originales; se excluye esa entrada del redirect anterior al instalarse.
   Usa la página nativa de stubs, sin ocupar las cuevas del fork. Si no coincide
   la firma, conserva la ruta anterior y lo registra.
5. **Código:** `gpu/shim/bbport_effect_time.h`, `bbport_frame_rate.cpp` y
   `tests/test_effect_time.cpp`. La prueba ejecuta el stub generado y verifica
   conservación del tiempo a 30/60/90/120/144/240/480 FPS, flags y pasos vacíos.
   Activado automáticamente; `BB_EFFECT_TIME=0` permite diagnóstico. No se añade
   un control Midnight de afinación de físicas sin necesidad de decisión del usuario.

## TIER 1 — Evento online sin payload

1. **Referencia:** ayozetr/bloodborne_linux, ayozetr, coautor ethan,
   `eec77ba22395af10628dccfe6ad576a5a4932123`; `scripts/crash_guards.py`,
   `scripts/patches.py`, `tests/test_crash_guards.py` y documentación.
   Inspección: `git show eec77ba2:scripts/crash_guards.py`.
2. **Mecanismo:** case 0 suma 0x80 a un payload r12 sin comprobarlo. El guard
   comprueba r12 antes de esa suma; un null usa la salida desconocida del juego
   en `0x1eb3dd0`. Un payload presente ejecuta las instrucciones originales.
3. **Hardware:** evita ese acceso null después de un evento incompleto; un test
   y branch por evento, sin copia de textura, memoria persistente grande ni worker.
4. **Compatibilidad:** firma exacta en `0x1eb3de8`; stub propio del sistema nativo
   de hooks. No ocupa la cueva de Quit to Desktop/Rebirth ni modifica transportes,
   salas, cuentas o guardados. Tampoco oculta fallos de punteros no nulos inválidos.
5. **Código:** `gpu/games/bloodborne/hooks_guards.cpp`, `hooks.cpp`,
   `hooks_internal.h`, `gpu/shim/bbport_guest_guards.h`,
   `tests/test_guest_guards.cpp`. El test ejecuta la máquina generada con null y
   payloads válidos. Diagnóstico `BB_CRASH_GUARDS=0`; sin nueva opción JSON.

## TIER 2 — Conservados y límites de integración

FSR 4.1.2 FP8, cámara directa por ratón, reset de historial y bias FSR ya están
integrados y siguen en el perfil instalado. No se vuelve a descargar el mismo
modelo verificado ni se cambia la configuración de afinidad o los mods del usuario.
Un HUD nuevo necesita su propio intervalo medido y lectura no bloqueante; no se
presenta el inverso de los FPS como latencia de render.

Las extensiones Lantern requieren compatibilidad real con su protocolo de salas,
viaje y progreso: el guard de eventos no implementa esas funciones. Su port no
se sustituye por opciones sin backend. La exclusión de escrituras CPU concurrentes
requiere un protocolo de propiedad probado con el heap y la emulación; el bloqueo
VMA añadido aquí solo cubre cambios de mapeo durante cada transacción.

## TIER 3 — Adaptaciones descartadas

No se copia de vuelta `guest_size` desde staging sin inicializar: Vulkan no
escribe todo el padding. No se aplican reservas arbitrarias de RecordData ni
batching ilimitado que retenga staging en 16 GB. No se interpretan imágenes tiled
como lineales, se importan cuevas compartidas a ciegas, ni se sustituyen los hooks
de tela/drag ya existentes por otra política de tiempo superpuesta.

La validación y los límites concretos están en [VALIDATION_0.8.3.md](VALIDATION_0.8.3.md).

## Diffs exactos de la adaptación

- [Writeback, runtime y regresiones](https://github.com/SSParzival/cachyborne/commit/ac2e7eac3b35bf39b103c89252ea5bf6bffaff07).
- [Acumulador de partículas y stub](https://github.com/SSParzival/cachyborne/commit/1dc8bf64c5055dd876d442652075790b0cbbda3c).
- [Guard de eventos sin datos](https://github.com/SSParzival/cachyborne/commit/56346674e0e33ee962ea6aea8af1f120734ca140).

Cada commit conserva su propio registro de prueba en CMake; las opciones de
diagnóstico nuevas se documentan en el catálogo de entorno de esta versión.
