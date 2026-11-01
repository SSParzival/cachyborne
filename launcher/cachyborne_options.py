"""Native Midnight configuration schema and runtime projection; no legacy GTK window."""

import copy
import shlex
import subprocess
from pathlib import Path

CONTROL_BINDINGS = [
    ("cross", "Interactuar / Cruz", "F, Return", "a"),
    ("circle", "Círculo", "", "b"),
    ("dodge", "Esquivar", "Space", None),
    ("sprint", "Correr", "Left Shift", None),
    ("walk", "Caminar lentamente (mantener)", "Left Alt", None),
    ("square", "Objeto rápido / Cuadrado", "Tab, R", "x"),
    ("triangle", "Vial de sangre / Triángulo", "Q", "y"),
    ("l1", "L1 · transformar arma", "E, Mouse X2", "leftshoulder"),
    ("r1", "R1 · ataque", "Mouse Left", "rightshoulder"),
    ("l2", "L2 · disparar", "Mouse Right", "lefttrigger"),
    ("r2", "R2 · ataque fuerte", "Left Shift+Mouse Left, Mouse X1", "righttrigger"),
    ("l3", "L3", "Left Ctrl", "leftstick"),
    ("r3", "R3 · fijar objetivo", "C, Mouse Middle", "rightstick"),
    ("options", "Menú / Options", "Escape", "start"),
    ("touchpad", "Gestos / panel izquierdo", "G", "back, touchpad"),
    ("touchpad_right", "Objetos personales / panel derecho", "Backspace", ""),
    ("up", "Cruceta arriba", "3, Up, Wheel Up", "dpup"),
    ("down", "Cruceta abajo", "4, Down, Wheel Down", "dpdown"),
    ("left", "Cruceta izquierda", "1, Left", "dpleft"),
    ("right", "Cruceta derecha", "2, Right", "dpright"),
    ("move_up", "Avanzar", "W", None),
    ("move_down", "Retroceder", "S", None),
    ("move_left", "Mover a la izquierda", "A", None),
    ("move_right", "Mover a la derecha", "D", None),
    ("look_up", "Mirar arriba", "I", None),
    ("look_down", "Mirar abajo", "K", None),
    ("look_left", "Mirar a la izquierda", "J", None),
    ("look_right", "Mirar a la derecha", "L", None),
]

LEGACY_DEFAULTS = dict(
    ui_language="es",
    game_dir="",
    user_dir="",
    mods_dir="",
    mods_enabled=True,
    patches_dir="",
    language="20",
    fullscreen=True,
    hdr=False,
    present_mode="Mailbox",
    gamepad="",
    gamepad_name="",
    display="",
    gpu="",
    skip_network_choice=True,
    fps_mode="uncap",
    fps_limit=144,
    draw_pipe="",
    mangohud=False,
    frame_stats=False,
    save_log=False,
    crash_diag=False,
    gpu_profile=False,
    vk_validation=False,
    extra_env="",
    trophy_key="",
    online=False,
    online_bbsc=True,
    online_community="http://play.seamlesshunt.com",
    online_server="play.seamlesshunt.com:31313",
    online_webapi="http://play.seamlesshunt.com:31315",
    online_npid="",
    online_password="",
    online_upnp=True,
    online_pool="seamless",
    thd_server="",
    thd_online_id="",
    thd_token="",
)

# Each original control is owned by one native field or a computed selector value.
CANONICAL_FIELDS = dict(
    game_dir="game",
    user_dir="user_dir",
    mods_dir="mods_dir",
    mods_enabled="mods_enabled",
    patches_dir="patches_dir",
    fullscreen="fullscreen",
    hdr="hdr",
    gamepad="gamepad",
    fps_limit="fps_limit",
)
COMPUTED_FIELDS = {"present_mode", "fps_mode", "gamepad_name"}
NATIVE_LEGACY_FIELDS = set(LEGACY_DEFAULTS) - set(CANONICAL_FIELDS) - COMPUTED_FIELDS

VOICE_LANGUAGES = [("eng", "Inglés (predeterminado)"), ("esa", "Español Latinoamericano"),
                   ("ese", "Español España"), ("jaj", "Japonés"), ("ded", "Alemán"),
                   ("frf", "Francés"), ("iti", "Italiano"), ("ptb", "Portugués (Brasil)"), ("auto", "Según el juego")]

LANGUAGES = [
    ("1", "English"),
    ("18", "English (UK)"),
    ("3", "Español"),
    ("20", "Español (Latinoamérica)"),
    ("0", "日本語"),
    ("2", "Français"),
    ("4", "Deutsch"),
    ("5", "Italiano"),
    ("6", "Nederlands"),
    ("7", "Português"),
    ("17", "Português (Brasil)"),
    ("16", "Polski"),
    ("19", "Türkçe"),
    ("9", "한국어"),
    ("11", "简体中文"),
    ("10", "繁體中文"),
    ("12", "Suomi"),
    ("13", "Svenska"),
    ("14", "Dansk"),
    ("15", "Norsk"),
]
PRESENCE_FLAGS = {
    "mangohud": "MANGOHUD",
    "frame_stats": "BB_FRAME_STATS",
    "save_log": "BB_SAVE_LOG",
    "gpu_profile": "BB_GPU_PROFILE",
    "vk_validation": "BB_VK_VALIDATION",
}
KNOWN_ENV = set(PRESENCE_FLAGS.values()) | {
    "BB_FAULT_WAIT",
    "BB_READBACKS",
    "BB_TROPHY_KEY",
    "BB_LANGUAGE",
    "BB_VOICE_LANGUAGE",
    "BB_MENU_LANGUAGE",
    "BB_DISPLAY",
    "BB_GPU_ID",
    "BB_SKIP_NETWORK_CHOICE",
    "BB_HDR",
    "BB_FPS",
    "BB_FPS_LIMIT",
    "BB_FULLSCREEN",
    "BB_PRESENT_MODE",
    "BB_GAMEPAD",
    "BB_DRAW_PIPE",
    "BB_PREUPLOAD",
    "BB_PC_MODEL",
    "BB_GUEST_IN_PLACE",
    "BB_AS_0_3",
    "BB_FREE_CHECK",
    "BB_WRITE_LOG",
    "BB_PRODUCER_CHECK",
    "BB_HOST_COPY_WAITS",
    "BB_ONLINE",
    "BB_BBSC",
    "BB_NET_BACKEND",
    "BB_COMMUNITY_SERVER",
    "BB_SHADNET_SERVER",
    "BB_SHADNET_WEBAPI",
    "BB_SHADNET_NPID",
    "BB_SHADNET_PASSWORD",
    "BB_UPNP",
    "BB_NET_GEN",
    "BB_THD_ONLINE_ID",
    "BB_THD_TOKEN",
}


# Retired upstream 0.6 memory paths. Purge values in saved drafts and inherited shells.
RETIRED_ENV = {"BB_PC_MODEL", "BB_GUEST_IN_PLACE", "BB_AS_0_3", "BB_PREUPLOAD", "BB_UFFD"}
VOICE_GAME_CODES = dict(auto="auto", eng="engb", esa="esar", ese="eses", jaj="jajp", ded="dede", frf="frfr", iti="itit", ptb="ptbr")
TEXT_SYSTEM_CODES = dict(enus="1", engb="18", jajp="0", frfr="2", itit="5", dede="4", eses="3", esar="20", ptbr="17", ptpt="7", nlnl="6", plpl="16", dadk="14", nono="15", fifi="12", svse="13", trtr="19", zhcn="11", zhtw="10", kokr="9")
TEXT_LANGUAGES = [("auto", "Seguir idioma del sistema PS4")] + [(code, dict(LANGUAGES)[system]) for code, system in TEXT_SYSTEM_CODES.items()]

# Native schema for every new INI control. Defaults use the upstream spelling and types
# (INI values are strings); GUI labels/pages and validation share this definition.
# key: default, page, Spanish title, choices or numeric bounds
INI_OPTIONS = {
    "game_text_language": ("auto", "graphics", "Idioma de textos (cualquier región)", tuple(code for code, _ in TEXT_LANGUAGES)),
    "vbao": ("0", "graphics", "Oclusión ambiental GT-VBAO (requiere SSAO)", ("0", "1")),
    "anti_aliasing": ("auto", "graphics", "Antialiasing", ("auto", "game", "fxaa", "smaa", "off")),
    "texture_quality": ("auto", "graphics", "Calidad de texturas en VRAM", ("auto", "full", "half", "quarter")),
    "frame_generation": ("off", "graphics", "Generación de cuadros", ("off", "fsr3", "dlss")),
    "frame_generation_multiplier": ("2", "graphics", "Multiplicador DLSS (según GPU)", ("2", "3", "4")),
    "low_latency": ("on", "performance", "Baja latencia (Reflex / AMD Anti-Lag 2)", ("off", "on", "boost")),
    "camera_speed_x": ("1.00", "controls", "Velocidad horizontal de cámara con mando", (0.25, 2.0)),
    "camera_speed_y": ("1.00", "controls", "Velocidad vertical de cámara con mando", (0.25, 2.0)),
    "fsr4_model": ("auto", "advanced", "Modelo de FSR 4.1 (assets compatibles)", ("auto", "fp8", "int8", "int8_rdna2", "int8_lite")),
    "light_grid_opt": ("1", "performance", "Optimizar cuadrícula de luces", ("0", "1")),
}
INI_CANONICAL_FIELDS = {"upscaler": "upscaler", "output_res": "output_res", "game_voice_language": "voice_language"}
INI_DEFAULTS = {key: spec[0] for key, spec in INI_OPTIONS.items()} | {
    "upscaler": "fsr411", "preset": "4", "sharpen": "1", "sharpness": "0.50", "object_motion": "1",
    "show_fps": "1", "output_res": "auto", "model_lod": "0", "mouse_look": "1", "mouse_sensitivity": "1.00",
    "mouse_invert_y": "0", "mouse_invert_x": "0", "button_prompts": "auto", "game_voice_language": "auto",
    **{"effect_" + key: value for key, value in (("chromatic_aberration", "1"), ("dof", "1"), ("motion_blur", "1"), ("ssao", "1"), ("dynamic_shadows", "1"), ("ssr", "0"), ("bloom", "1"))},
    "skip_intro": "0", "debug_camera": "0", "debug_menu": "0",
}


def validate_ini(ini):
    import math
    for key, (default, page, title, values) in INI_OPTIONS.items():
        value = ini.get(key, default)
        if isinstance(values[0], (float, int)):
            try:
                number = float(value)
            except (TypeError, ValueError):
                raise ValueError(f"{title}: valor numérico inválido") from None
            if not math.isfinite(number) or not values[0] <= number <= values[1]:
                raise ValueError(f"{title}: fuera de rango")
        elif str(value) not in values:
            # Pre-0.7 frame generation was a boolean INI value.
            if key != "frame_generation" or str(value) not in ("0", "1"):
                raise ValueError(f"{title}: valor inválido {value}")


def reconcile(settings):
    """Project native widgets into the exact INI/environment consumed by the runtime."""
    s = copy.deepcopy(settings)
    old = dict(LEGACY_DEFAULTS) | {k: v for k, v in s.get("original_settings", {}).items() if k in LEGACY_DEFAULTS}
    for original, canonical in CANONICAL_FIELDS.items():
        if canonical in s:
            old[original] = s[canonical]
    old["present_mode"] = {"fiforelaxed": "FifoRelaxed"}.get(
        s["present_mode"], s["present_mode"].capitalize()
    )
    old["fps_mode"] = s["fps"] if s["fps"] in ("30", "60", "90", "uncap") else "uncap"
    old["fps_limit"] = int(s.get("fps_limit", 0)) or (
        0 if s["fps"] == "uncap" else int(s["fps"])
    )
    env = {
        key: value
        for key, value in s.get("runtime_env", {}).items()
        if key not in KNOWN_ENV
    }
    for key in RETIRED_ENV:
        env.pop(key, None)
    env.update(
        BB_LANGUAGE=old["language"],
        BB_VOICE_LANGUAGE=s.get("voice_language", "eng"),
        BB_MENU_LANGUAGE=old["ui_language"] or "en",
        BB_SKIP_NETWORK_CHOICE="1" if old["skip_network_choice"] else "0",
        BB_HDR="1" if s["hdr"] else "0",
        BB_FPS=old["fps_mode"],
        BB_FPS_LIMIT=str(old["fps_limit"]),
    )
    for key, var in [
        ("display", "BB_DISPLAY"),
        ("draw_pipe", "BB_DRAW_PIPE"),
    ]:
        if old[key] != "":
            env[var] = str(old[key])
    if old["gpu"] and old["gpu"] in s.get("gpu_ids", {}):
        env["BB_GPU_ID"] = str(s["gpu_ids"][old["gpu"]])
    for key, var in PRESENCE_FLAGS.items():
        if old[key]:
            env[var] = "1"
    if old["crash_diag"]:
        env.update(BB_FREE_CHECK="1", BB_WRITE_LOG="1", BB_PRODUCER_CHECK="1")
    if not isinstance(s.get("fault_wait", False), bool):
        raise ValueError("Conservar proceso al fallar: se requiere un valor booleano")
    if s.get("fault_wait"):
        env["BB_FAULT_WAIT"] = "1"
    readbacks = s.get("readbacks") or settings.get("original_settings", {}).get("readbacks", "")
    if readbacks:
        if str(readbacks) not in ("0", "1", "2"):
            raise ValueError("Lecturas GPU: valor inválido")
        s["readbacks"] = str(readbacks)
        env["BB_READBACKS"] = str(readbacks)
    if old["trophy_key"]:
        import re
        if not re.fullmatch(r"[0-9a-fA-F]{32}", old["trophy_key"].strip()):
            raise ValueError("La clave de trofeos debe contener 32 dígitos hexadecimales")
        env["BB_TROPHY_KEY"] = old["trophy_key"].strip()
    if old["online"]:
        env.update(
            BB_ONLINE="1", BB_NET_GEN="20", BB_UPNP="1" if old["online_upnp"] else "0"
        )
        if old["online_pool"] == "huntersdream":
            env.update(
                BB_NET_BACKEND="thd",
                BB_COMMUNITY_SERVER=old["thd_server"] or "https://thehuntersdream.com",
                BB_THD_ONLINE_ID=old["thd_online_id"],
                BB_THD_TOKEN=old["thd_token"],
            )
        else:
            env.update(
                BB_SHADNET_SERVER=old["online_server"],
                BB_SHADNET_WEBAPI=old["online_webapi"],
                BB_COMMUNITY_SERVER=old["online_community"],
                BB_SHADNET_NPID=old["online_npid"],
                BB_SHADNET_PASSWORD=old["online_password"],
            )
            if old["online_bbsc"]:
                env["BB_BBSC"] = "1"
    for item in shlex.split(old["extra_env"]):
        if "=" not in item:
            raise ValueError(f"Variable adicional sin valor: {item}")
        key, value = item.split("=", 1)
        env[key] = value
    for key in RETIRED_ENV:
        env.pop(key, None)
    if s.get("fsr4_dir"):
        env["BB_FSR4_DIR"] = s["fsr4_dir"]
    s["original_settings"] = old
    s["runtime_env"] = env
    binding_defaults = {
        f"{kind}.{name}": default
        for name, _, keyboard, pad in CONTROL_BINDINGS
        for kind, default in (("key", keyboard), ("pad", pad))
        if default is not None
    }
    s["runtime_ini"] = {
        key: str(binding_defaults[key] if value is None else value)
        for key, value in s.get("runtime_ini", {}).items()
        if value is not None or key in binding_defaults
    }
    s["runtime_ini"] = dict(INI_DEFAULTS) | s["runtime_ini"]
    s["runtime_ini"]["game_voice_language"] = VOICE_GAME_CODES[s.get("voice_language", "eng")]
    text = s["runtime_ini"].get("game_text_language", "auto")
    if text != "auto" and text in TEXT_SYSTEM_CODES:
        s["runtime_env"]["BB_LANGUAGE"] = TEXT_SYSTEM_CODES[text]
    s["runtime_ini"].update(upscaler=s["upscaler"], output_res="auto" if s.get("output_auto") else s["output_res"], menu_language=old["ui_language"] or "es")
    return s


def capabilities(root, mode):
    result = subprocess.run(
        [str(Path(root) / "out/bb-gpu-capabilities"), mode],
        capture_output=True,
        text=True,
        timeout=12,
    )
    if result.returncode:
        raise ValueError(result.stderr.strip() or "No se pudo detectar hardware")
    return [line.split("\t") for line in result.stdout.splitlines() if "\t" in line]


def fsr4_problem(directory, output, preset):
    import json, struct

    width, height = map(int, output.split("x"))
    tier = "2160" if width > 1920 or height > 1080 else "1080"
    names = ["native", "quality", "balanced", "performance", "ultraperf"]
    try:
        index = int(preset)
        if not 0 <= index < len(names):
            raise ValueError("preset")
        model = names[index]
    except (ValueError, IndexError, TypeError):
        return "Preset FSR 4 inválido"
    base = f"fsr4_model_v07_i8_{model}"
    files = [
        "rcas.spv",
        "spd_auto_exposure.spv",
        f"{base}_initializers.bin",
        f"{base}_pre_weights.bin",
        f"{base}_shader_manifest.json",
        f"{base}_{tier}_pre.spv",
        f"{base}_{tier}_post.spv",
    ]
    files += [f"{base}_{tier}_pass{i}.spv" for i in range(1, 13)]
    for name in files:
        path = Path(directory) / name
        if not path.is_file() or not path.stat().st_size:
            return f"Falta el asset FSR 4: {name}"
        if path.suffix == ".spv":
            with path.open("rb") as stream:
                header = stream.read(20)
            if (
                len(header) < 20
                or header[:4] != struct.pack("<I", 0x07230203)
                or path.stat().st_size % 4
            ):
                return f"Asset SPIR-V FSR 4 dañado: {name}"
        if path.suffix == ".json":
            try:
                json.loads(path.read_text())
            except (ValueError, OSError):
                return f"Manifiesto FSR 4 dañado: {name}"
    return None


# PS4 system-language IDs mapped to the retail game's message archives.
LANGUAGE_FOLDERS = dict(
    zip(
        (
            "0",
            "1",
            "2",
            "3",
            "4",
            "5",
            "6",
            "7",
            "8",
            "9",
            "10",
            "11",
            "12",
            "13",
            "14",
            "15",
            "16",
            "17",
            "18",
            "19",
            "20",
        ),
        (
            "jpnjp",
            "engus",
            "frafr",
            "spaes",
            "deude",
            "itait",
            "nldnl",
            "porpt",
            "rusru",
            "korkr",
            "zhotw",
            "zhocn",
            "finfi",
            "swese",
            "dandk",
            "norno",
            "polpl",
            "porbr",
            "enggb",
            "turtr",
            "spaar",
        ),
    )
)


def available_languages(game):
    """Only languages with both nonempty item and menu archives in this dump."""
    root = Path(game).expanduser() / "dvdroot_ps4/msg" if game else None
    if root is None:
        return []

    def present(code):
        folder = root / LANGUAGE_FOLDERS[code]
        return all(
            any(
                (folder / (name + suffix)).is_file()
                and (folder / (name + suffix)).stat().st_size > 0
                for suffix in (".msgbnd.dcx", ".msgbnd")
            )
            for name in ("item", "menu")
        )

    return [(code, title) for code, title in LANGUAGES if present(code)]


def validate_game_language(settings, game):
    choices = available_languages(game)
    if not choices:
        raise ValueError("No hay archivos de idioma completos en dvdroot_ps4/msg.")
    available = {code for code, _ in choices}
    old = settings.setdefault("original_settings", {})
    text = settings.get("runtime_ini", {}).get("game_text_language", "auto")
    requested = TEXT_SYSTEM_CODES.get(text) or str(
        old.get("language")
        or settings.get("runtime_env", {}).get("BB_LANGUAGE")
        or "20"
    )
    if requested not in available:
        if requested in ("3", "20"):
            requested = next(
                (code for code in ("20", "3", "1") if code in available), choices[0][0]
            )
        else:
            raise ValueError(
                f"El idioma {requested} no está disponible en este dump; selecciona uno instalado."
            )
    old.update(language=requested, ui_language=old.get("ui_language") or "es")
    settings.setdefault("runtime_env", {}).update(
        BB_LANGUAGE=requested, BB_MENU_LANGUAGE=old["ui_language"]
    )
    return choices


def monitor_output(root, selected=""):
    """Resolve a monitor once during setup; direct launch uses the prepared size."""
    rows = capabilities(root, "--displays")
    rows = [row for row in rows if len(row) == 3]
    if not rows:
        raise ValueError("No se detectó monitor; indica una resolución de salida manual")
    chosen = next((row for i, row in enumerate(rows, 1) if selected in (str(i), row[0])), None)
    chosen = chosen or next((row for row in rows if row[2] == "1"), rows[0])
    import re
    if not re.fullmatch(r"[0-9]+x[0-9]+", chosen[1]):
        raise ValueError("El monitor no informa una resolución válida")
    return chosen[1]
