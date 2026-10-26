"""All additional bbport controls, rendered inside the native Midnight window."""

import os
from pathlib import Path
import subprocess
import sys
import threading
from gi.repository import Adw, GLib, Gtk
import cachyborne as core
import cachyborne_options as options


class NativePanels:
    def native_button(self, group, title, callback):
        row = Adw.ActionRow(title=title)
        button = Gtk.Button(label="Abrir", valign=Gtk.Align.CENTER)
        button.add_css_class("secondary-action")
        button.connect("clicked", lambda *_: callback())
        row.add_suffix(button)
        group.add(row)
        return button

    def select_path(self, key, directory=True):
        chooser = Gtk.FileChooserNative(
            title="Seleccionar archivo o carpeta",
            transient_for=self,
            action=Gtk.FileChooserAction.SELECT_FOLDER
            if directory
            else Gtk.FileChooserAction.OPEN,
        )

        def response(dialog, result):
            if result == Gtk.ResponseType.ACCEPT:
                self.widgets[key][0].set_text(dialog.get_file().get_path())
            dialog.destroy()

        chooser.connect("response", response)
        chooser.show()
        self.chooser = chooser

    def add_native_controls(self, page):
        group = self.group(
            page,
            "Asignación de teclado y mando",
            "Escribe nombres SDL; vacío desasigna. Restablecer recupera el valor original.",
        )
        self.binding_defaults = {}
        for name, title, keyboard, pad in options.CONTROL_BINDINGS:
            row = Adw.ExpanderRow(title=title)
            group.add(row)
            for kind, default in [("key", keyboard), ("pad", pad)]:
                if default is None:
                    continue
                key = f"ini.{kind}.{name}"
                self.ini.setdefault(f"{kind}.{name}", default)
                self.binding_defaults[key] = default
                entry = self.entry(
                    row, key, "Teclado / ratón" if kind == "key" else "Mando"
                )
                reset = Gtk.Button(
                    icon_name="edit-undo-symbolic",
                    valign=Gtk.Align.CENTER,
                    tooltip_text="Restablecer original",
                )
                reset.connect("clicked", lambda _, e=entry, d=default: e.set_text(d))
                entry.add_suffix(reset)
                capture = Gtk.Button(
                    icon_name="input-keyboard-symbolic",
                    valign=Gtk.Align.CENTER,
                    tooltip_text="Capturar entrada",
                )
                capture.connect(
                    "clicked", lambda _, e=entry, k=kind: self.capture_input(e, k)
                )
                entry.add_suffix(capture)

    def capture_input(self, entry, kind):
        def worker():
            try:
                result = subprocess.run(
                    [str(core.ROOT / "out/bb-gpu-capabilities"), "--read-input", kind],
                    capture_output=True,
                    text=True,
                    timeout=20,
                )
                if result.returncode:
                    raise ValueError(result.stderr.strip() or "Captura cancelada")
                GLib.idle_add(
                    entry.set_text, result.stdout.strip().removeprefix(kind + " ")
                )
            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                GLib.idle_add(self.report, str(error))

        threading.Thread(target=worker, daemon=True).start()

    def build_content(self):
        page = self.page(
            "content",
            "Mods y parches",
            "Selección y orden guardados junto con tu perfil. El último mod tiene prioridad.",
        )
        paths = self.group(page, "Carpetas y contenido")
        self.entry(paths, "user_dir", "Carpeta de guardados (vacío: predeterminada)")
        self.switch(paths, "mods_enabled", "Activar mods")
        for key, title in [
            ("mods_dir", "Directorio de mods"),
            ("mods_config", "Archivo mods.json"),
            ("patches_dir", "Directorio de parches XML"),
            ("patches_config", "Archivo patches.json"),
        ]:
            self.entry(paths, key, title)
            self.native_button(
                paths,
                "Seleccionar " + title.lower(),
                lambda k=key: self.select_path(k, not k.endswith("config")),
            )
        self.mod_group = self.group(page, "Mods · orden de carga")
        self.patch_group = self.group(page, "Parches XML · Bloodborne 1.09")
        self.content_rows = []
        self.mod_rows = []
        self.patch_rows = []
        self.native_button(
            paths, "Actualizar lista desde las carpetas", self.refresh_content
        )
        self.refresh_content()

    def refresh_content(self):
        sys.path.insert(0, str(core.ROOT / "scripts"))
        import mods, patches

        for group, row in self.content_rows:
            group.remove(row)
        self.content_rows = []
        self.mod_rows = []
        self.patch_rows = []
        try:
            root = Path(self.fields["mods_dir"]() or core.DATA / "mods").expanduser()
            config = self.settings.get("mod_selection")
            if config is None:
                config = core.read_json(
                    Path(
                        self.fields["mods_config"]() or core.CONFIG / "mods.json"
                    ).expanduser(),
                    {},
                )
            available = mods.discover(root)
            ordered = list(
                dict.fromkeys(
                    [n for n in config.get("order", []) if n in available] + available
                )
            )
            for name in ordered:
                row = Adw.SwitchRow(
                    title=GLib.markup_escape_text(name),
                    active=name not in config.get("disabled", []),
                )
                for delta, icon in [(-1, "go-up-symbolic"), (1, "go-down-symbolic")]:
                    button = Gtk.Button(icon_name=icon, valign=Gtk.Align.CENTER)
                    button.connect(
                        "clicked", lambda _, n=name, d=delta: self.move_mod(n, d)
                    )
                    row.add_suffix(button)
                row.connect(
                    "notify::active",
                    lambda *_: (
                        self.queue_autosave()
                        if hasattr(self, "autosave_source")
                        else None
                    ),
                )
                self.mod_group.add(row)
                self.content_rows.append((self.mod_group, row))
                self.mod_rows.append((name, row))
            selection = self.settings.get("patch_selection")
            if selection is None:
                selection = core.read_json(
                    Path(
                        self.fields["patches_config"]() or core.CONFIG / "patches.json"
                    ).expanduser(),
                    {},
                )
            found = patches.external_patches(
                Path(self.fields["patches_dir"]() or core.DATA / "patches").expanduser()
            )
            for key, _, meta in found:
                enabled = key in selection.get("enabled", []) or (
                    key not in selection.get("disabled", [])
                    and meta.get("isEnabled", "false").lower() == "true"
                )
                row = Adw.SwitchRow(
                    title=GLib.markup_escape_text(meta.get("Name") or key),
                    subtitle=GLib.markup_escape_text(key),
                    active=enabled,
                )
                row.connect(
                    "notify::active",
                    lambda *_: (
                        self.queue_autosave()
                        if hasattr(self, "autosave_source")
                        else None
                    ),
                )
                self.patch_group.add(row)
                self.content_rows.append((self.patch_group, row))
                self.patch_rows.append((key, row))
        except (OSError, ValueError) as error:
            row = Adw.ActionRow(
                title="No se pudo leer el contenido",
                subtitle=GLib.markup_escape_text(str(error)),
            )
            self.mod_group.add(row)
            self.content_rows.append((self.mod_group, row))

    def content_selection(self):
        return dict(
            mod_selection=dict(
                order=[n for n, _ in self.mod_rows],
                disabled=[n for n, r in self.mod_rows if not r.get_active()],
            ),
            patch_selection=dict(
                enabled=[n for n, r in self.patch_rows if r.get_active()],
                disabled=[n for n, r in self.patch_rows if not r.get_active()],
            ),
        )

    def move_mod(self, name, delta):
        selection = self.content_selection()
        order = selection["mod_selection"]["order"]
        index = order.index(name)
        target = index + delta
        if 0 <= target < len(order):
            order[index], order[target] = order[target], order[index]
            self.settings.update(selection)
            self.refresh_content()
            self.queue_autosave()

    def build_online(self):
        page = self.page(
            "online",
            "Juego online y cuentas",
            "Las credenciales permanecen en tu configuración local privada.",
        )
        general = self.group(page, "Conexión")
        self.switch(general, "legacy.online", "Activar online")
        self.combo(
            general,
            "legacy.online_pool",
            "Comunidad",
            ["seamless", "huntersdream"],
            ["Seamless Hunt / shadNet", "The Hunter’s Dream"],
        )
        for key, title in [
            ("online_bbsc", "Sincronización BBSC"),
            ("online_upnp", "UPnP"),
        ]:
            self.switch(general, "legacy." + key, title)
        seamless = self.group(page, "Seamless Hunt / shadNet")
        for key, title in [
            ("online_community", "Servidor de comunidad"),
            ("online_server", "Servidor de juego"),
            ("online_webapi", "Web API"),
            ("online_npid", "NPID"),
        ]:
            self.entry(seamless, "legacy." + key, title)
        self.secret(seamless, "legacy.online_password", "Contraseña de sesión")
        thd = self.group(page, "The Hunter’s Dream")
        for key, title in [
            ("thd_server", "Servidor THD"),
            ("thd_online_id", "Online ID"),
        ]:
            self.entry(thd, "legacy." + key, title)
        self.secret(thd, "legacy.thd_token", "Token THD")
        self.native_button(thd, "Iniciar sesión en THD", self.sign_in)
        self.native_button(thd, "Cerrar sesión local", self.sign_out)
        self.native_button(general, "Comprobar conexión", self.check_online)

    def secret(self, group, key, title):
        row = Adw.PasswordEntryRow(title=title, text=str(self.current(key)))
        group.add(row)
        self.register(key, row, lambda: row.get_text(), "entry")

    def sign_out(self):
        for key in ("thd_online_id", "thd_token"):
            self.widgets["legacy." + key][0].set_text("")
        self.save()

    def sign_in(self):
        sys.path.insert(0, str(core.ROOT / "scripts"))
        import online

        server = self.fields["legacy.thd_server"]()

        def worker():
            try:

                def code(uri, code):
                    GLib.idle_add(self.report, f"Abre {uri} e introduce {code}")

                identity, token = online.thd_sign_in(server, code)

                def done():
                    self.widgets["legacy.thd_online_id"][0].set_text(identity)
                    self.widgets["legacy.thd_token"][0].set_text(token)
                    self.save()

                GLib.idle_add(done)
            except Exception as error:
                GLib.idle_add(self.report, str(error))

        threading.Thread(target=worker, daemon=True).start()

    def check_online(self):
        sys.path.insert(0, str(core.ROOT / "scripts"))
        import online

        settings = self.collect()["original_settings"]

        def worker():
            try:
                if settings["online_pool"] == "huntersdream":
                    result = online.check_thd(settings["thd_server"])
                else:
                    result = online.check(
                        settings["online_server"],
                        settings["online_webapi"],
                        settings["online_community"],
                    )
                for name, address, ok, detail in result:
                    GLib.idle_add(
                        self.report, f"{name}: {address}: {'OK' if ok else detail}"
                    )
            except Exception as error:
                GLib.idle_add(self.report, str(error))

        threading.Thread(target=worker, daemon=True).start()

    def build_advanced(self):
        page = self.page(
            "advanced",
            "Hardware y diagnóstico",
            "Todos los controles originales en la misma interfaz Midnight.",
        )
        hardware = self.group(page, "Idioma y dispositivo")
        choices = options.available_languages(self.settings.get("game", ""))
        self.language_values = [code for code, _ in choices] or [""]
        self.language_row = self.combo(
            hardware,
            "legacy.language",
            "Idioma del juego",
            self.language_values,
            [name for _, name in choices] or ["Selecciona primero un dump"],
        )
        self.language_row.set_sensitive(bool(choices))
        self.entry(
            hardware,
            "legacy.ui_language",
            "Idioma del menú del juego (en / es / ru / …)",
        )
        self.entry(hardware, "legacy.display", "Monitor SDL (vacío: automático)")
        self.entry(hardware, "legacy.gpu", "GPU Vulkan (nombre detectado)")
        self.native_button(
            hardware, "Detectar pantallas, GPU y mandos", self.detect_hardware
        )
        pipeline = self.group(page, "Render y memoria")
        for key, title, values, labels in [
            (
                "draw_pipe",
                "Draw pipeline",
                ["", "0", "1"],
                ["Automático", "Desactivado", "Activado"],
            ),
            (
                "readbacks",
                "Lecturas GPU",
                ["", "0", "2"],
                ["Relaxed / predeterminado", "Desactivadas", "Precise"],
            ),
            (
                "preupload",
                "Precarga de recursos",
                ["", "0", "2"],
                ["Normal", "Desactivada", "Completa (+3 GB VRAM)"],
            ),
            (
                "memory_model",
                "Modelo de memoria",
                ["auto", "new", "old"],
                ["Automático", "Nuevo", "Anterior"],
            ),
        ]:
            self.combo(pipeline, "legacy." + key, title, values, labels)
        self.switch(pipeline, "legacy.as_0_3", "Comportamiento de memoria 0.3")
        diagnostics = self.group(page, "Diagnóstico")
        for key, title in [
            ("mangohud", "MangoHud"),
            ("frame_stats", "Estadísticas por cuadro"),
            ("save_log", "Guardar log"),
            ("crash_diag", "Diagnóstico de fallos"),
            ("gpu_profile", "Perfil GPU"),
            ("vk_validation", "Validación Vulkan"),
            ("skip_network_choice", "Omitir elección online / offline"),
        ]:
            self.switch(diagnostics, "legacy." + key, title)
        self.entry(
            diagnostics,
            "legacy.extra_env",
            "Variables adicionales KEY=valor (separadas por espacios)",
        )
        extra = self.group(page, "Resolución y herramientas del juego")
        for key, default in [
            ("aspect_ratio", "auto"),
            ("live_resolution", "0"),
            ("skip_intro", "1"),
            ("debug_camera", "0"),
            ("debug_menu", "0"),
        ]:
            self.ini.setdefault(key, default)
        self.entry(extra, "ini.aspect_ratio", "Relación de aspecto (auto / 16:9 / …)")
        self.combo(
            extra,
            "ini.live_resolution",
            "Cambiar resolución en vivo",
            ["0", "1", "auto"],
            ["Desactivado", "Activado", "Detectar"],
        )
        for key, title in [
            ("skip_intro", "Omitir introducción"),
            ("debug_camera", "Cámara de depuración"),
            ("debug_menu", "Menú de depuración"),
        ]:
            self.switch(extra, "ini." + key, title)
        fsr = self.group(page, "FSR · ajuste fino")
        for key, default in [
            ("jitter", "1"),
            ("reactive", "0"),
            ("reactive_scale", "1.00"),
            ("reactive_threshold", "0.20"),
            ("reactive_max", "0.90"),
            ("debug_view", "0"),
            ("fsr4_auto_exposure", "1"),
            ("fsr4_invert_jitter", "0"),
            ("frame_generation", "0"),
        ]:
            self.ini.setdefault(key, default)
            self.entry(fsr, "ini." + key, key.replace("_", " ").capitalize())
        assets = self.group(page, "Assets de escaladores")
        self.entry(
            assets, "fsr4_dir", "Directorio FSR 4 (vacío: fsr4_shaders del proyecto)"
        )
        self.native_button(
            assets,
            "Descargar assets FSR 4",
            lambda: self.run_tool(
                ["bash", str(core.ROOT / "tools/fetch_fsr4_assets.sh")],
                os.environ
                | {
                    "BB_FSR4_DIR": self.fields["fsr4_dir"]()
                    or str(core.ROOT / "fsr4_shaders")
                },
            ),
        )
        self.entry(assets, "fsr411_dll", "DLL de AMD FSR 4.1.1")
        self.native_button(
            assets,
            "Seleccionar DLL FSR 4.1.1",
            lambda: self.select_path("fsr411_dll", False),
        )
        self.entry(assets, "fsr411_loader", "DLL del loader (opcional)")
        self.native_button(assets, "Generar assets FSR 4.1.1", self.build_fsr411)
        self.entry(
            assets, "dlss_library", "Biblioteca DLSS Linux (libnvidia-ngx-dlss.so.*)"
        )
        self.native_button(assets, "Instalar biblioteca DLSS", self.install_dlss)
        self.native_button(
            assets, "Quitar biblioteca DLSS local", lambda: self.install_dlss(True)
        )

    def detect_hardware(self):
        def worker():
            try:
                for mode in ("--displays", "--gpus", "--gamepads"):
                    rows = options.capabilities(core.ROOT, mode)
                    if mode == "--gpus":
                        # Tool emits index, device name, and optional extra columns.
                        self.settings["gpu_ids"] = {
                            r[3]: r[0] for r in rows if len(r) == 5
                        }
                    GLib.idle_add(
                        self.report,
                        mode + ":\n" + "\n".join(" · ".join(row) for row in rows),
                    )
            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                GLib.idle_add(self.report, str(error))

        threading.Thread(target=worker, daemon=True).start()

    def run_tool(self, command, env=None):
        self.navigate("setup")

        def worker():
            try:
                with subprocess.Popen(
                    command,
                    cwd=core.ROOT,
                    env=env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                ) as process:
                    for line in process.stdout:
                        GLib.idle_add(self.report, line.rstrip())
                    code = process.wait()
                GLib.idle_add(self.report, f"Herramienta finalizada ({code}).")
            except OSError as error:
                GLib.idle_add(self.report, str(error))

        threading.Thread(target=worker, daemon=True).start()

    def build_fsr411(self):
        dll = self.fields["fsr411_dll"]()
        loader = self.fields["fsr411_loader"]()
        if not Path(dll).is_file():
            self.report("Selecciona primero la DLL AMD FSR 4.1.1.")
            return
        destination = self.fields["fsr411_dir"]() or str(core.ROOT / "fsr4_411")
        self.run_tool(
            ["bash", str(core.ROOT / "tools/fsr4cap/build_assets.sh"), dll]
            + ([loader] if loader else []),
            os.environ | {"BB_FSR411_OUT": destination},
        )

    def install_dlss(self, remove=False):
        import shutil

        try:
            folder = (
                Path(self.fields["user_dir"]() or core.DATA / "user").expanduser()
                / "dlss"
            )
            if remove:
                for path in folder.glob("libnvidia-ngx-dlss.so.*"):
                    path.unlink()
            else:
                source = Path(self.fields["dlss_library"]()).expanduser()
                if (
                    not source.name.startswith("libnvidia-ngx-dlss.so.")
                    or source.read_bytes()[:4] != b"\x7fELF"
                ):
                    raise ValueError("Selecciona una biblioteca DLSS Linux ELF válida.")
                folder.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, folder / source.name)
            self.report("Biblioteca DLSS local actualizada.")
        except (OSError, ValueError) as error:
            self.report(str(error))

    def update_languages(self, *_):
        choices = options.available_languages(self.path.get_text().strip())
        current = self.fields["legacy.language"]()
        self.language_values[:] = [code for code, _ in choices] or [""]
        self.language_row.set_model(
            Gtk.StringList.new(
                [name for _, name in choices] or ["Selecciona primero un dump"]
            )
        )
        preferred = (
            current
            if current in self.language_values
            else next(
                (code for code in ("20", "3", "1") if code in self.language_values),
                self.language_values[0],
            )
        )
        self.language_row.set_selected(self.language_values.index(preferred))
        self.language_row.set_sensitive(bool(choices))
