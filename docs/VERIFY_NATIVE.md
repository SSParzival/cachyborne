# Verificación del perfil 1440p Native AA

El perfil predeterminado usa entrada y salida de 2560×1440, preset 0 (1x),
FSR 4.1 FP8 y simulación a 60 FPS, sin generación de frames. Los assets FSR
deben estar preparados previamente mediante el flujo existente del launcher.

```sh
python3 scripts/verify_native.py --out out/native-verification
```

La prueba usa copias de los guardados, red apagada, compositor headless y audio
dummy. Espera 8 GiB disponibles durante 20 segundos continuos, con un plazo de
600 segundos. Conserva los límites de memoria/PSI del harness. Si no hay margen,
escribe un resultado fallido sin lanzar el juego. No cierra aplicaciones ajenas.

Después de 30 segundos de calentamiento mide **todo** el recorrido de 120
segundos. Conserva frames, poses, hashes de binarios e integridad de guardados
en el directorio indicado. `acceptance.json` exige backend FSR 4.1 real,
configuración nativa, ventana completa de gameplay, media 60±0,1 FPS,
p99 <=17,5 ms, peor frame <=33,34 ms y ausencia de fallos de runtime/cierre.
El conteo de frames de la ventana también debe quedar a ±0,1 FPS del objetivo
(en ventanas muy cortas se admite la incertidumbre de un frame en el conteo).
Un fallo no se convierte en aprobación recortando una parte lenta del recorrido.
`--duration` permite ampliar la ventana hasta 1.800 segundos; para esos casos
usa `--replay` con un recorrido de la misma duración. El valor predeterminado
sigue siendo 120 segundos y se conserva siempre la ventana completa.

La imagen de comprobación se obtiene antes de iniciar el replay. El capturador
espera el archivo raw completo de la resolución solicitada y dos segundos sin
modificaciones. Así, la asignación y escritura del readback diagnóstico no
forman parte de la ventana de juego. No se recortan intervalos de una ventana
ya registrada: los intentos anteriores conservan sus resultados originales.
El manifiesto registra `snapshot_before_replay` y `snapshot_quiet_seconds`.

Para medir un área determinada, el juego debe confirmar la transición y todas
las poses medidas deben pertenecer al mapa solicitado:

```sh
# Catedral, m24_00_00_00
python3 scripts/verify_native.py --out out/native-cathedral \
    --warp-lamp 2402950 --expected-map 0x18000000
# Byrgenwerth, m32_00_00_00
python3 scripts/verify_native.py --out out/native-byrgenwerth \
    --warp-lamp 3202950 --expected-map 0x20000000
# Pesadilla de Mensis, m26_00_00_00
python3 scripts/verify_native.py --out out/native-mensis \
    --warp-lamp 2602950 --expected-map 0x1a000000
```

Los destinos se comprobaron en `ReturnPointParam` del dump 1.09. Una captura
valida la pareja lámpara/mapa contra esa tabla antes de lanzar el juego y
registra el identificador de bloque y su nombre conocido en el manifiesto.
Byrgenwerth (`3202950`) y Mensis (`2602950`) son destinos distintos. Una captura
de área exige partir desde otro mapa. El mapa esperado usa el formato de
`poses.csv`, no el entero little-endian del registro de lámparas. El recorrido
de pad predeterminado avanza desde la lámpara y puede provocar muerte o carga;
el verificador rechaza esas ventanas. `--replay /ruta/recorrido.pad` permite
usar un recorrido específico y conserva su copia y hash. Un barrido de cámara
desde la lámpara tampoco demuestra cobertura de toda el área ni de todas sus
cámaras. `--host-trace` conserva muestras CPU, memoria y E/S con
sobrecoste de diagnóstico. Una medición headless no certifica VRR, audio
físico, mando ni presentación en el monitor del usuario.

Para una prueba visible con los guardados copiados y audio real, usa
`--desktop --monitor N`, donde `N` es el número/nombre de SDL que muestra
`out/bb-gpu-capabilities --displays`. Esta opción abre el juego en el escritorio
y conserva HDR, fullscreen y VRR del perfil. Rechaza drivers dummy/offscreen,
audio dummy/disk y un monitor solicitado que no exista. No cambia modos de
pantalla ni el dispositivo de audio predeterminado. Registra el backend real;
la ausencia de vaciados de cola y la ruta al monitor/salida física deben
comprobarse en los logs y en el compositor. Una pantalla virtual no certifica
un monitor físico. El modo headless sigue siendo el predeterminado.

## Inspección de tablas de BBHOST

La herramienta offline adaptada de BBHOST lee las definiciones del propio dump
y muestra campos con sus nombres reales. No modifica archivos ni guardados:

```sh
python3 scripts/param_tables.py ReturnPointParam --app0 CUSA03173 \
    --id 3202950 --fields areaNo,blockNo,returnPointEntityId
```

La comparación contra `tools/bbparam.py` de BBHOST en
`bdc5cdf8be5f37188da9c2368e92be444fdc04cb` dio identidad en **63 definiciones,
65 tablas y 79.759 filas** del dump local. Evidencia:
`out/param-tables-differential.json`. Se conservan las filas de IDs repetidos;
el servicio de edición de tablas en vivo y la ABI de plugins siguen pendientes.

También se pueden generar mods de archivo mediante la sintaxis de BBHOST:

```toml
# ReturnPointParam.toml — ejemplo de una edición por nombre de campo
[row.3202950]
returnAnimId = 101163
```

```sh
python3 scripts/param_tables.py ReturnPointParam --app0 CUSA03173 \
    --overrides ReturnPointParam.toml --out /ruta/a/mis-mods/mi-param-mod
```

El resultado usa el formato loose-file del gestor de mods existente. `[row.all]`
se aplica primero; los IDs concretos tienen prioridad. Los elementos de arrays
usan claves entre comillas, por ejemplo `"campo[2]" = 1`. Se admiten campos
numéricos, conservando los bits vecinos. Los campos string no son editables.
Los campos desconocidos, IDs ausentes, índices inválidos y valores fuera del
almacenamiento se rechazan antes de publicar el mod. El archivo original y los
mods existentes no se sobrescriben. La salida contiene datos derivados del
dump local y permanece fuera del repositorio; no se incluye en las releases.

Para combinar varias tablas en un único banco, coloca los archivos
`NOMBRE_DE_TABLA.toml` en un directorio y usa:

```sh
python3 scripts/param_tables.py --app0 CUSA03173 \
    --overrides-dir /ruta/a/overrides --out /ruta/a/mis-mods/parametros
```

Esto evita que dos mods que contienen el banco completo se sustituyan entre
sí. Se validan todas las tablas antes de publicar la salida. La prueba de
integración leyó el cambio generado en el repositorio real del juego mediante
el comando de aceptación `SEQ param TABLA ID OFFSET_BYTES`; es una lectura
acotada de una palabra, sin modificar memoria ni aplicar overrides en vivo.

El diagnóstico opt-in de aceptación también admite
`SEQ param_ids TABLA CAPACIDAD`: devuelve el total de filas y hasta `CAPACIDAD`
IDs, en el orden original y conservando duplicados. Cero consulta sólo el total;
el límite diagnóstico es 4.096 IDs. El lector nativo usa bloques acotados de
128 registros compartidos con la búsqueda de filas, sin caché ni sondeo en el
perfil normal. La lectura se ejecuta en el hilo de actualización mientras las
tablas están cargadas. Esto prepara la utilidad de `param_ids` de BBHOST; la
tabla pública completa de servicios de plugins sigue pendiente.
