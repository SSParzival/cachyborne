#!/usr/bin/env python3
"""Run a prepared retail game and require actual GPU output, not just successful setup.

Copies the user directory into a private test directory. Does not certify a playthrough.
Use --replay for a recorded BB_PAD_REPLAY route; inspect screenshots/logs for visual defects.
"""

import argparse
import copy
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from isolated_game import headless_command, isolate_profile, stop_group, MemoryWatch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "launcher"))
import cachyborne as core


def check_rendered_output(log, directory):
    if re.search(
        r"STOP:|ErrorDeviceLost|Device lost while|Assertion Failed|Host fault|Guest fault|Fault \(signal \d+\)",
        log,
    ):
        raise RuntimeError("El juego registró un fallo; revisa game.log.")
    samples = [float(value) for value in re.findall(r"Frame stats: ([0-9.]+) FPS", log)]
    if not samples or max(samples) <= 0:
        raise RuntimeError("No se registraron cuadros presentados.")
    frames = list(Path(directory).glob("f*_final_*x*_*.raw"))
    valid = False
    for frame in frames:
        match = re.search(r"_(\d+)x(\d+)_(rgba|bgra)\.raw$", frame.name)
        if not match:
            continue
        width, height = map(int, match.group(1, 2))
        data = frame.read_bytes()
        # A blank/solid swapchain is insufficient evidence that the game loaded.
        if len(data) == width * height * 4 and len(set(data[::4])) > 8:
            valid = True
            break
    if not valid:
        raise RuntimeError("No se capturó una imagen GPU válida con contenido visible.")
    return samples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--replay", type=Path)
    args = parser.parse_args()
    if args.seconds < 15:
        parser.error("--seconds must be at least 15")
    profile = copy.deepcopy(core.load_profile(args.profile))
    isolate_profile(profile)
    memory_watch = MemoryWatch(core.available_memory)
    core.check_runtime_requirements(profile)
    out = (
        args.out or core.ROOT / "out" / ("smoke-" + time.strftime("%Y%m%d-%H%M%S"))
    ).resolve()
    out.mkdir(parents=True, exist_ok=False, mode=0o700)
    frames = out / "frames"
    frames.mkdir()
    isolated = out / "user"
    shutil.copytree(profile["user"], isolated)
    profile["user"] = str(isolated)
    command, env = core.launch_command(profile, extra=["--timeout", "0"])
    trigger = out / "capture"
    env.update(
        BB_FRAME_STATS="1", BB_FINAL_DUMP_TRIGGER=str(trigger), BB_DUMP_DIR=str(frames)
    )
    if args.replay:
        env["BB_PAD_REPLAY"] = str(args.replay.resolve())
    command, env = headless_command(command, env, profile['settings']['output_res'],
                                    profile['settings'].get('fps_limit') or 60)
    logpath = out / "game.log"
    print(f"Prueba real con guardados aislados: {out}", flush=True)
    with logpath.open("w") as log:
        process = subprocess.Popen(
            command,
            cwd=core.ROOT,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        started = time.monotonic()
        deadline = started + args.seconds
        capture_times = sorted({15, args.seconds // 2, args.seconds - 2})
        try:
            while time.monotonic() < deadline:
                memory_watch.check()
                if process.poll() is not None:
                    raise RuntimeError(
                        f"El juego terminó antes de tiempo ({process.returncode})."
                    )
                if capture_times and time.monotonic() - started >= capture_times[0]:
                    trigger.touch()
                    capture_times.pop(0)
                time.sleep(1)
            samples = check_rendered_output(logpath.read_text(errors="replace"), frames)
        finally:
            stop_group(process)
    print(
        f"PASS: {args.seconds}s, imagen GPU visible, {len(samples)} muestras FPS; última {samples[-1]:.1f}."
    )
    print(
        "Una prueba de arranque no garantiza 144 FPS en todas las zonas ni ausencia de defectos visuales."
    )


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
