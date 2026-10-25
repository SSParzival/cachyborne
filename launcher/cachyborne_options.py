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
    ui_language="en",
    game_dir="",
    user_dir="",
    mods_dir="",
    mods_enabled=True,
    patches_dir="",
    language="1",
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
    readbacks="",
    preupload="",
    mangohud=False,
    frame_stats=False,
    save_log=False,
    crash_diag=False,
    memory_model="auto",
    as_0_3=False,
    gpu_profile=False,
    vk_validation=False,
    extra_env="",
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

LANGUAGES = [
    ("1", "English"),
    ("18", "English (UK)"),
    ("3", "Español"),
    ("20", "Español (Latinoamérica)"),
    ("8", "Русский"),
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
    "BB_LANGUAGE",
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
    "BB_READBACKS",
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


def reconcile(settings):
    """Project native widgets into the exact INI/environment consumed by the runtime."""
    s = copy.deepcopy(settings)
    old = dict(LEGACY_DEFAULTS) | s.get("original_settings", {})
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
    env.update(
        BB_LANGUAGE=old["language"],
        BB_MENU_LANGUAGE=old["ui_language"] or "en",
        BB_SKIP_NETWORK_CHOICE="1" if old["skip_network_choice"] else "0",
        BB_HDR="1" if s["hdr"] else "0",
        BB_FPS=old["fps_mode"],
        BB_FPS_LIMIT=str(old["fps_limit"]),
    )
    for key, var in [
        ("display", "BB_DISPLAY"),
        ("draw_pipe", "BB_DRAW_PIPE"),
        ("readbacks", "BB_READBACKS"),
        ("preupload", "BB_PREUPLOAD"),
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
    if old["memory_model"] in ("new", "old"):
        env["BB_PC_MODEL"] = "1" if old["memory_model"] == "new" else "0"
    if old["as_0_3"]:
        env.update(
            BB_AS_0_3="1",
            BB_PC_MODEL="0",
            BB_GUEST_IN_PLACE="0",
            BB_HOST_COPY_WAITS="all",
            BB_PRODUCER_CHECK="1",
        )
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
    s["runtime_ini"].update(upscaler=s["upscaler"], output_res=s["output_res"])
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
