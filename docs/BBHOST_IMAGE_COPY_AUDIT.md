# Copias de imagen: comparación y medición previa

## Alcance

Se compararon `copy_elide.h`, `full_write.h` y los puntos de integración de
BBHost v0.2.24 (`b0098e04b889e2e70b4af84be1ce809d7af3ff95`) con el commit
`7dbe6f60f33fff45ea1bcbf6c6e61a6b35466e13`. El ledger no cambia entre esos
dos commits. Las modificaciones posteriores de `render.cpp` relacionadas con
locks, imports y census no equivalen a cambios del ledger.

Fuente: [copy_elide.h de BBHost](https://github.com/droogie/bbhost/blob/7dbe6f60f33fff45ea1bcbf6c6e61a6b35466e13/src/host/copy_elide.h).
BBHost activa esta optimización por defecto en GPU integrada y la desactiva en
GPU dedicada. Su beneficio documentado en Radeon 680M no demuestra beneficio
en la RX 9070 XT de este equipo. No se activó ni se importó todavía la elisión.

## Diferencias que impiden un traslado directo

El ledger de BBHost puede diferir una copia, redirigir lecturas al origen,
materializarla cuando cambia el origen y descartarla si se sobrescribe o retira
el destino. También permite intercambiar backings cuando se prueba una
sobrescritura completa. Necesita distinguir lecturas, escrituras parciales,
sobrescrituras y retiros; no basta con omitir un `vkCmdCopyImage`.

En Cachyborne:

- `Rasterizer::IsComputeImageCopy` ya reconoce copias de compute y evita parte
  del trabajo de retile y upload. El filtro de hashes de copias de buffers es
  otra función; ninguno constituye el ledger de imágenes.
- `Runtime::CopyImageRegions` recibe copias de imagen y mip, incluida expansión
  de recursos. La alternativa `CopyImageThroughBuffer` requiere seguimiento
  aparte. Blits, resolves, uploads y presentación también necesitan cobertura.
- `BindTexturesFromSet` dispone de una ruta memoizada anterior a `FindTexture`.
  Una redirección exige invalidar o versionar ese memo y los views asociados.
  Resolver sólo en `Runtime::Transit` puede ser demasiado tarde: los descriptores
  ya pueden contener el backing antiguo.
- Cada imagen posee sus backings, views y estado de layouts. Intercambiar sólo
  handles Vulkan no conserva la propiedad ni la coherencia de esos datos.
- El retiro puede comenzar desde el hilo guest: `flush_hooks` libera el lock de
  VMA y llama al hook, que llega a `UnmapMemory` y `FreeImage`. No es seguro
  grabar comandos allí sin un protocolo con el productor de comandos. Tampoco
  se debe esperar al productor manteniendo el mutex de texturas.
- Probar una sobrescritura completa requiere geometría y shaders conocidos,
  ausencia de discard, máscaras y blend compatibles, cobertura completa y
  tratamiento de depth/stencil. El tamaño del viewport por sí solo no lo prueba.

## Instrumentación añadida

Los timers opcionales `image copy`, `image mip copy` e
`image copy through buffer` atribuyen las transferencias a su propia etiqueta.
El camino HLE antes podía dejarlas atribuidas a la etiqueta GPU anterior.
`BB_GPU_PROFILE=1` habilita esta medición; permanece desactivada por defecto.

La captura de comandos registra entradas `TRANSFER` con identificadores de
imagen, rangos guest, formato, dimensiones, mip/layers/samples y regiones
Vulkan. Son eventos propios en orden; no desaparecen por el límite de muestras
de buffers después de los primeros cuatro draws. No se cambió ninguna copia
Vulkan ni se redirigió ningún recurso. La captura no es todavía un census
completo de todas las operaciones ni una prueba de equivalencia GPU.

`frame-capture-test` verifica la API pública con transferencias intercaladas
entre passes de ocho draws y compute, y exclusión de estados Idle/Armed. La
captura activa deshabilita helpers/memos del renderizador; sus tiempos no se
deben presentar como rendimiento del perfil normal.

## Comparación de consumo pendiente

La comparación propuesta `BB_GPU_SPIN_US=200` frente a `0` utiliza el mismo
binario congelado, ruta nativa, guardados copiados y muestreo de recursos en
ambas condiciones. El intento `out/native-spin-pilot-frozen` expiró durante la
admisión de 8 GiB estables por 20 segundos: **no inició ninguna condición**.
No hay un resultado de consumo ni se modificó el valor predeterminado.
El intento previo se canceló antes del lanzamiento para congelar los binarios;
también se conserva como tal. No se debilitaron los límites de memoria.

La siguiente decisión requiere medir transferencias en el juego y completar
el protocolo de reads/writes/retiros y views antes de ensayar elisión opcional
con comparación del contenido GPU. No se atribuye una ganancia aún inexistente.

## Pruebas y corrección de limpieza

`out/image-copy-capture-check-exit.txt` registra código **0**: **61/61 CTest**,
**288 Python**, pruebas C de runtime, SPIR-V y readback Vulkan. La nueva captura
de transferencias se compiló en `libbbgpu.so`, SHA-256
`1c37f32ad0e014632bfc7530a49b90a0636da8836f3ab645881477d9dc48e884`.
La aceptación anterior de 120 segundos corresponde al binario anterior; no se
traslada automáticamente a éste.

El primer diagnóstico `out/image-copy-diagnostic` se detuvo por el límite
runtime de 2 GiB disponibles, antes de alcanzar el mundo. Los guardados y los
binarios permanecieron intactos. El compositor salió, pero su reaper y el juego
permanecieron en el grupo de procesos: esperar sólo al líder no garantizaba la
limpieza. Se terminó ese grupo propio y se corrigió `stop_group` para finalizar
también los miembros que sobreviven al líder. No se tocaron aplicaciones del
usuario.

`tests/test_isolated_cleanup.py` reproduce ambos casos con un hijo que ignora
SIGTERM: líder todavía activo y líder ya terminado. La implementación anterior
falla ambos; la nueva pasa. Tras esta corrección pasan **289 pruebas Python**
(`out/image-copy-cleanup-python-tests.log`). Los límites de memoria no cambian.

El segundo diagnóstico `out/image-copy-diagnostic-current` terminó la ruta de
20 segundos sin fallos y conservó cinco transferencias completas a 2560×1440:
tres RGBA8, una depth y una RGBA16F. Su manifest conserva **passed=false** porque
faltó activar `BB_GPU_PROFILE` en el entorno efectivo. También conserva el
primer error de directorio de captura, corregido y rearmado durante esa sesión.
No se usa como medición de costes GPU ni como aceptación de FPS. El reintento
con directorio preparado y entorno explícito se identifica por
`out/image-copy-diagnostic-profiled`.

Ese reintento **pasa**: cinco transferencias en una captura de 212 entradas,
guardados y hashes intactos, sin fallos de runtime ni cierre. Se revisó también
el frame final de Central Yharnam a 2560×1440; la imagen contiene mundo,
personaje y HUD. El runtime confirma FSR 4.1 FP8 y preset 0, sin frame generation.

Las últimas cinco ventanas del profiler atribuyen a las copias alrededor de
**0,20 ms/frame**: aproximadamente 0,086 ms a las tres copias RGBA8, 0,064 ms a
RGBA16F y 0,051 ms a depth. El detalle queda en
`BBHOST_IMAGE_COPY_DIAGNOSTIC.json`. Es una medición instrumentada de una ruta;
no demuestra una mejora de elisión ni estima el coste de su bookkeeping o la
fracción de copias que sería legal eliminar. Se conserva la decisión de no
activar una elisión incompleta en esta GPU dedicada.
