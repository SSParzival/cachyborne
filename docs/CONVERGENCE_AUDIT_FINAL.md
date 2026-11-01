# Auditoría consolidada de convergencia — 2026-11-01

Base: `c34e6b2d`, árbol inicialmente limpio. Hardware comprobado: Intel
Core i5-14600KF, 16 GB, Radeon RX 9070 XT / RADV GFX1201; CachyOS LTS,
Mesa 26.2.5, Clang 23.1.4, ThinLTO y mold. El presupuesto guest permanece
en 5056 MiB. No se ha establecido una clasificación del port más rápido.

**Estado global: aceptación completa pendiente.** Este informe distingue
implementación, pruebas de regresión y evidencia dentro del juego. No certifica
física Havok completa, POM ni cooperación cruzada sin sus pruebas necesarias.
Los resultados estructurados están en [el JSON](CONVERGENCE_VALIDATION_FINAL.json).

## Fuentes y diferencias arquitectónicas

Se investigaron copias locales y los refs fijados en
[la auditoría anterior](CONVERGENCE_AUDIT_2026-11-01.md): Lantern
`e6b943dd969d99561545315f3d801f10e4a6cf8d`, Supermedo
`b48052617a919fc4d6b10ae3395e209c9e94e17a`, ayozetr
`b27d0715b897486b6115a34b801661714c0a988f`, bbport 0.7
`0fc2921c45f8ff3900f6bfa43a299e9a4f532af4` y shadPS4
`8a66adabf74072e24162b098d7bfd3156b475393`. Las correcciones ya
certificadas de afinidad, staging, timeline, writeback de mips y español
permanecen como base; no se duplicaron hooks de timestep.

## WP1 — descompresión nativa

Implementado un hook real de `inflate(z_stream*, int)` en el ejecutable 1.09,
con firma verificada y trampoline original. El contrato real no es la función
de cuatro argumentos propuesta inicialmente. La ruta rápida libdeflate valida
estado, rangos y límites de 8 MiB, usa scratch transaccional por llamada y
notifica escrituras mediante el runtime. Los flujos parciales y formatos no
admitidos continúan por el guest. [Diseño y reproducción](NATIVE_DEFLATE.md).

Comparación directa contra el código original del guest: **257 flujos,
925.240.908 bytes**, incluidos 256 DCX reales. Igualdad de bytes, contadores,
checksum y continuación del estado terminal. Guest: **2471,548 ms,
374,4 MB/s**; nativo: **1270,170 ms, 728,4 MB/s**, aproximadamente 1,95×
en este corpus. Registro privado: `out/final-audit/guest-deflate-retail.log`.

No se extrapola esa tasa al tiempo total de lámpara. Una sesión real observó
32 flujos acelerados (580 KiB) frente a 7060 llamadas de fallback; gran parte
de la carga usa streaming. No se movió la ejecución dependiente del guest a
E-cores ni se afirma descompresión paralela en tiempo de ejecución.

## WP2 — sombras de contacto y PCSS

Integrado el compute shader GPL-2.0-or-later de Lantern con host Vulkan propio,
con crédito en `NOTICE`. Descriptores y rangos se comprueban; los destinos se
identifican por UID, las vistas nativas se liberan tras completar comandos y
las barreras preservan visibilidad. Se admiten los proxies de resolución reducida.
Los dos controles arrancan desactivados y tienen texto español en Midnight.

La primera ejecución PCSS falló con SIGSEGV en RADV al grabar descriptores.
Los descriptores de imagen del renderer guest separan vista y sampler;
trasladarlos directamente a un binding combinado dejó el sampler nulo incluso
con una vista válida. La integración ahora instala siempre su sampler propio.
Si falta atlas/UBO, su rango es insuficiente o el layout es indefinido, desactiva
PCSS para esa pasada y liga profundidad/constantes válidas. Si no queda ningún
efecto activo, conserva las sombras originales sin despachar compute. También
comprueba las vistas resultantes de los proxies antes de grabar comandos.

La regresión ejecuta ocho modos Vulkan, incluidos atlas sin sampler combinado,
atlas ausente, UBO ausente/corto y layout indefinido. Los casos de fallback
conservan RGB y alfa esperado. La evidencia fallida se mantiene separada en
`out/final-audit/lighting/before-descriptor-guards/`; no se mezcla con las nuevas
mediciones. Un arranque detenido en el menú también se archivó por separado.

La prueba Vulkan ejecuta el SPIR-V de producción: plano iluminado, atlas
ocluido, independencia de cascadas, preservación RGB y tamaño 33×19, con
contactos, PCSS y ambos. Una prueba de shader aislada no demuestra corrección
visual de todos los mapas ni coste despreciable dentro del juego.

Comparativa real con salida 0 y captura `.raw` en **los cuatro modos**:

| Modo | Compute, mediana de ventanas completas (ms/frame) | Mediana frame (ms) | p99 frame (ms) |
| --- | ---: | ---: | ---: |
| Base | — | 16,72 | 16,73 |
| Contactos, primera ejecución | 0,275 | 16,72 | 559,99 |
| PCSS | 0,603 | 16,72 | 16,73 |
| Contactos + PCSS | 0,636 | 16,72 | 16,73 |
| Contactos, repetición separada | 0,277 | 16,72 | 16,73 |

Escenario: posición inicial de Yharnam del mismo guardado copiado, límite
60 FPS, Mailbox, VBAO, frame generation desactivado, profiler GPU y telemetría
activados en todos los modos. Target reconstruido **2560×1440**, resolución
configurada **1706×960**, proxy alineado **1708×960**; ventana/captura final
**1920×1080**. Se usaron los assets nativos FP8 con procedencia SDK **2.4.0**,
upscaler **4.1.2.4555**. El nombre interno `fsr411` y la etiqueta histórica
«FSR 4.1.1» del profiler no identifican la versión de los assets cargados.

Los tiempos de frame corresponden a los últimos 20 segundos del reloj del CSV,
no al reloj de arranque del proceso. Se corrigió el análisis que mezclaba ambos
relojes y devolvía cero muestras en un arranque lento. Los costes compute son
segmentos del profiler con barreras incluidas; el límite de 60 FPS impide inferir
sobrecarga nula a partir de medianas iguales. PCSS varió entre **0,423 y
1,009 ms** por ventana completa; ambos entre **0,630 y 0,639 ms**. No se
certifica coste despreciable ni rendimiento a 144 FPS con estas sombras activadas.

La primera medición de contactos contiene pausas largas de CPU y se conserva
íntegra. Durante la investigación se observó `btrfs balance start -dusage 50
-musage 50 /` concurrente, swap-in y presión de I/O elevada (PSI). Esto limita
la atribución de esos outliers al renderer; no demuestra por sí solo una causa
única. Una repetición separada completó 1159 muestras finales con p99 **16,73 ms**.
Sus logs y métricas permanecen en `out/final-audit/lighting-contact-repeat/`;
la ejecución original no se sustituyó ni filtró. Los guardados originales
conservaron sus hashes en ambas comparaciones. Las capturas se revisaron como
comprobación visual de esta escena; no constituyen regresión visual exhaustiva.

**Corrección de la investigación inicial:** Lantern sí tiene implementación
POM. `vk_effect_shaders.cpp` consume `BbSettings::EffectPomCavity` e invoca
`Pom::PatchPom`; `vk_pom_patch.h` contiene los parches POM/cavity para tres
shaders, con comprobaciones de tamaño/checksum y referencias a IDs SPIR-V.
Buscar sólo el literal `effect_pom_cavity` ocultaba el consumidor mediante enum.
[Consumidor fijado](https://github.com/Awakenchan/LanternEngine/blob/e6b943dd969d99561545315f3d801f10e4a6cf8d/engine/gpu/shadps4/video_core/renderer_vulkan/vk_effect_shaders.cpp#L157).

POM queda **fuera de esta integración y sin validar en Cachyborne**. No se portó
ni se añadió un control sin backend. Su ausencia aquí no debe atribuirse a falta
de código en Lantern; adaptar esos parches al SPIR-V de este traductor y comprobar
su coste/corrección sigue pendiente.

## WP3 — equivalencia física

Instrumentación optativa offline del pacer guest, verificada por firma, con
poses válidas, mapas, reloj monotónico y cola acotada de muestras. El escritor
usa E-cores. `capture_physics.py` ejecuta el mismo replay sobre copias del mismo
guardado a 30/60/144 FPS. `physics_acceptance.py` interpola capturas reales,
comprueba mapa y origen común y exige error horizontal menor al 0,1%.

El analizador rechaza trazas inmóviles, faltantes, con saltos temporales o
muestras perdidas. Sus seis pruebas validan el análisis; **no son ensayos del
motor físico**. Poses del jugador no observan vértices de telas, contactos de
malla, durabilidad ni ventanas de daño: la aceptación física global permanece
falsa hasta instrumentar y ejecutar esos escenarios.

Capturas reales adicionales: tres ejecuciones completadas a límites 30, 60 y
144 FPS, con secuencia generada de sprint/salto de 8 s, sin alterar los guardados
de origen. El análisis devolvió **`physics_accepted: false`**, por cobertura
temporal insuficiente. Las mayores separaciones de muestras durante el replay
fueron **954,50 / 293,70 / 173,42 ms**, respectivamente. No se relajó el umbral
para convertir esas trazas en un aprobado. La actividad de almacenamiento
concurrente limita su utilidad como aceptación determinista; además no se
observaron contactos, vértices de ropa, durabilidad ni ventanas de daño.
Registro: `out/final-audit/physics/{manifest,result}.json` y CSV por tasa.

## WP4 — viaje repetido y memoria

`stress_warp_soak.py` copia los datos del usuario, fuerza offline, verifica
5056 MiB guest y despacha viajes auténticos mediante filas de lámpara verificadas.
Cada transición exige cambio de mapa, jugador válido, carga terminada y al
menos 31 s de asentamiento; la ejecución registrada utiliza 35 s. No se cuentan
peticiones sin transición como viajes completados.

Se muestrean cada aproximadamente 500 ms RSS, anónimo, PSS shmem, bloques del
memfd, VRAM y GTT DRM deduplicados por cliente. El runtime registra uso/presupuesto
VMA y capacidad de staging. Los límites de crecimiento final frente al primer
piso son 256 MiB RSS, 512 MiB VRAM y 32 MiB staging. Estas tolerancias son una
prueba finita de estabilidad, no una demostración matemática de ausencia de fugas.
La reserva extra del port, la memoria flexible y los bloques memfd no se confunden
con el presupuesto de memoria directa del juego.

Resultado medido: **15 idas y vueltas, 31 transiciones reales** incluyendo
normalización inicial, **1231,991 s y 2467 muestras**. Primer/último piso:
RSS **1262,5 → 751,6 MiB**, VRAM **3640,1 → 3804,2 MiB** y staging
**144 → 144 MiB**. El memfd asignado pasó de **770,7 a 830,3 MiB**.
Picos: RSS **1290,2 MiB**, VRAM **4596,0 MiB**. Se cumplieron todos los
límites y los hashes de los guardados de origen permanecieron iguales.
Registro: `out/final-audit/warp-soak-main/result.json` y `memory.csv`.

## WP5 — telemetría y HUD

`BB_LATENCY_HUD=1` habilita el HUD, con traducción española;
`BB_LATENCY_TRACE=/ruta/nueva.csv` guarda T1 sondeo SDL, T2 envío guest,
T3 envío Vulkan, T4 observación CPU de finalización GPU y T5 retorno de
`vkQueuePresentKHR`. Tickets y generaciones relacionan las etapas. La cola es
acotada, el productor nunca espera por telemetría y la escritura usa E-cores.
La prueba C++ comprueba orden, finalización única, generaciones y contención.

**T4 es una cota superior observada por CPU, no un timestamp GPU; T5 no es
scanout.** El intervalo desde sondeo describe la edad de la muestra asociada al
envío, no la respuesta física a un estímulo. No se presenta como medición física
input-to-display. Desactivado, no hay lecturas de reloj, consultas GPU, escrituras
ni hilos adicionales de telemetría; permanecen las ramas de comprobación. No se
promete un coste literalmente nulo sin medición.

Telemetría real de los últimos 20 s de las ejecuciones: edad mediana de sondeo
hasta retorno de present **63,524 ms** en base, **65,324 ms** con PCSS y
**66,808 ms** con ambos. Cota mediana de envío hasta observación de finalización
GPU: aproximadamente **50,08 ms**. Estas cotas incluyen colas y esperas de
presentación; **no son duración de ejecución de los shaders**. El JSON conserva
p99, máximos, diferencias sucesivas y la ejecución de contactos con outliers.

## WP6 — interoperabilidad Lantern

[Auditoría de contratos](LANTERN_ROOM_CONTRACT.md). El protocolo de sala 16,
sus transportes y sus estados de viaje/progreso son distintos de shadNet/BBSC.
No se implementó un puente completo ni se anuncia cooperación cruzada.
El usuario autorizó investigación con copia local y confirmó que no dispone de
replay de referencia ni cliente Lantern. No se ejecutó un handshake entre clientes.

## Compilación, regresiones y commits

Comprobaciones ejecutadas satisfactoriamente en este host:

- `bash build.sh --test`: Clang, CPU nativa, ThinLTO y mold; biblioteca GPU,
  probe, módulo de red y pruebas nativas compilados.
- `bash scripts/check-all.sh`: **29/29 CTest** y **249 pruebas Python**, Bash/AST,
  registro de variables, SPIR-V de movimiento y del shader de contactos/PCSS,
  smoke Vulkan con envío y readback de 4096 bytes. Ahora la suite valida las
  palabras SPIR-V exactas embebidas para contactos.
- `tests/test_audio.c`: regresión PCM/colas y **2000 búferes PipeWire** sin
  agotamientos. Registros `audio-unit.log` y `audio-pipewire.log`.
- El aviso de RADV sobre conformidad experimental y los avisos de políticas de
  dependencias CMake permanecen; no hubo fallo crítico de compilación o pruebas.

Commits de implementación de esta fase: `6255df44` (libdeflate), `2bfc385d`
(instrumentación/soak/análisis), `253f9310` (telemetría), `608b2504` (captura
multitasa), `c94d2150` (arranque del arnés) y **`5c338a57`** (sombras y guardas).
El JSON identifica ese último commit de implementación. El registro previo a
144 FPS de `c34e6b2d` obtuvo mediana **7,00 ms** y p99 **7,01 ms** con sombras
nuevas desactivadas; se conserva como **evidencia anterior**, no como benchmark
actual de PCSS a 144 FPS.

Reproducción de las aceptaciones sobre una nueva copia privada:

```sh
python3 scripts/stress_warp_soak.py --profile /ruta/game_profile.json --out out/nuevo-soak --iterations 15
python3 scripts/capture_physics.py --profile /ruta/game_profile.json --replay /ruta/inputs.txt --duration 8 --out out/nueva-fisica
python3 scripts/physics_acceptance.py out/nueva-fisica/manifest.json --out out/nueva-fisica/result.json
```

La comparación gráfica original y la repetición se conservan con sus scripts
en `out/final-audit/lighting/run.py` y `lighting-contact-repeat/run.py`; necesitan
los datos privados de este host. La suite de regresión de código es reproducible
sin publicar esos datos.

## Reproducción y alcance

Los artefactos privados permanecen en `out/final-audit/`: guardados aislados,
logs, CSV de poses/memoria/latencia y capturas. No se distribuyen ejecutables,
partidas ni recursos propietarios. Cada programa de aceptación verifica los
hashes de los guardados de origen; los programas finalizan sólo su propio grupo
de procesos. Las métricas consolidadas deben interpretarse con el escenario y
las opciones concretas, sin extrapolar a todos los niveles o FPS.
