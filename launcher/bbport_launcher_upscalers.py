# SPDX-FileCopyrightText: Copyright 2026 bbport contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher's upscaler files: FSR 4.1.1 build from AMD's DLL, FSR 4.1.2 driver models, NVIDIA's DLSS libraries."""

from bbport_launcher_common import *  # noqa: F401,F403


class UpscalerFiles:
    """Part of LauncherWindow (bbport_launcher.py)."""

    def update_fsr411_row(self, progress=None):
        building = self.fsr411_build is not None
        self.fsr411_button.set_label(tr('Cancel') if building else tr('Choose DLL…'))
        if hasattr(self, "fsr412_button"):
            self.fsr412_button.set_label(tr('Cancel') if building else tr('Choose a file…'))
        if progress:
            self.fsr411_row.set_subtitle(progress)
        elif not building:
            self.fsr411_row.set_subtitle(tr(
                'Build FSR 4.1.1 from amd_fidelityfx_upscaler_dx12.dll 4.1.x (OptiScaler: the FSR4_LATEST folder, or from a game with FSR 4.1). Needs GE-Proton 10+, Proton Experimental or Proton-CachyOS; 2–5 minutes (RDNA4: twice as long, with the FP8 variant)'))

    def alert(self, heading, body):
        if hasattr(Adw, "AlertDialog"):
            dialog = Adw.AlertDialog(heading=heading, body=body)
            dialog.add_response("ok", "OK")
            dialog.present(self)
        else:
            self.toasts.add_toast(Adw.Toast(title=f"{heading}: {body}", timeout=10))

    def update_dlss_row(self):
        own = ngx_libraries(self.user_dir() / "dlss")
        bundled = ngx_libraries(binaries_dir())
        own_fg = ngx_libraries(self.user_dir() / "dlss", NGX_FG_PREFIX)
        bundled_fg = ngx_libraries(binaries_dir(), NGX_FG_PREFIX)
        bridge = (binaries_dir() / "libbbport_dlss.so").is_file()
        if own:
            text = tr('Your own: {}').format(own[-1].name[len(NGX_PREFIX):])
        elif bundled:
            text = tr('From this build: {}').format(bundled[-1].name[len(NGX_PREFIX):])
        else:
            text = tr("None: choose libnvidia-ngx-dlss.so.* from NVIDIA's DLSS SDK ({})").format(DLSS_SDK_URL)
        # The Frame Generation library (the same SDK folder): the game searches both places.
        if own_fg:
            text += "\n" + tr('Frame Generation: your own {}').format(own_fg[-1].name[len(NGX_FG_PREFIX):])
        elif bundled_fg:
            text += "\n" + tr('Frame Generation: from this build {}').format(bundled_fg[-1].name[len(NGX_FG_PREFIX):])
        else:
            text += "\n" + tr('Frame Generation: no libnvidia-ngx-dlssg.so.* (choose it the same way; DLSS SDK 310.7 or newer)')
        if not bridge:
            text += "\n" + tr('This build has no DLSS bridge (libbbport_dlss.so): DLSS will not work')
        self.dlss_row.set_subtitle(text)
        self.dlss_reset_button.set_sensitive(bool(own or own_fg))
        if hasattr(self, "sharpness_row"):  # built after this row
            self.update_upscaler_status()

    def choose_ngx(self):
        dialog = Gtk.FileDialog(title=tr("NVIDIA's DLSS library (libnvidia-ngx-dlss.so.*, libnvidia-ngx-dlssg.so.*)"))
        files = Gtk.FileFilter()
        files.set_name("libnvidia-ngx-dlss*.so.*, nvngx_dlss*.dll")
        for pattern in ("libnvidia-ngx-dlss.so*", "libnvidia-ngx-dlssg.so*", "*.dll", "*.DLL"):
            files.add_pattern(pattern)
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(files)
        dialog.set_filters(filters)
        dialog.set_default_filter(files)

        def finish(dialog, result):
            try:
                chosen = dialog.open_finish(result)
            except GLib.Error:
                return
            if chosen and chosen.get_path():
                self.install_ngx(Path(chosen.get_path()))
        dialog.open(self, None, finish)

    def install_ngx(self, path):
        if path.suffix.lower() == ".dll":
            self.alert(tr('A Linux library is needed'), tr(
                "{} is the Windows DLSS library; NVIDIA's Linux driver does not load it. libnvidia-ngx-dlss.so.<version> from NVIDIA's DLSS SDK is needed: {}").format(path.name, DLSS_SDK_URL))
            return
        try:
            with open(path, "rb") as f:
                elf = f.read(4) == b"\x7fELF"
        except OSError as error:
            self.alert(tr('Could not read the file'), str(error))
            return
        prefix = ngx_prefix_of(path.name)
        if prefix is None or not elf:
            self.alert(tr('This is not the DLSS library'), tr(
                "libnvidia-ngx-dlss.so.<version> (DLSS) or libnvidia-ngx-dlssg.so.<version> (frame generation) from NVIDIA's DLSS SDK is needed: {}").format(DLSS_SDK_URL))
            return
        target = self.user_dir() / "dlss"
        try:
            target.mkdir(parents=True, exist_ok=True)
            for old in ngx_libraries(target, prefix):  # one of each kind
                old.unlink()
            shutil.copy2(path, target / path.name)
        except OSError as error:
            self.alert(tr('Could not copy the library'), str(error))
            return
        version = path.name[len(prefix):]
        self.toasts.add_toast(Adw.Toast(title=(
            tr('DLSS Frame Generation {}: ready') if prefix == NGX_FG_PREFIX else tr('DLSS {}: ready')
        ).format(version)))
        self.update_dlss_row()

    def reset_ngx(self):
        for prefix in (NGX_PREFIX, NGX_FG_PREFIX):
            for old in ngx_libraries(self.user_dir() / "dlss", prefix):
                try:
                    old.unlink()
                except OSError:
                    pass
        self.update_dlss_row()

    def choose_dll(self, title, done):
        dialog = Gtk.FileDialog(title=title)
        dll = Gtk.FileFilter()
        dll.set_name("DLL")
        dll.add_pattern("*.dll")
        dll.add_pattern("*.DLL")
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(dll)
        dialog.set_filters(filters)
        dialog.set_default_filter(dll)
        folder = self.settings.get("fsr411_dll_dir", "")
        if folder and Path(folder).is_dir():
            dialog.set_initial_folder(Gio.File.new_for_path(folder))

        def finish(dialog, result):
            try:
                chosen = dialog.open_finish(result)
            except GLib.Error:
                return
            if chosen and chosen.get_path():
                done(chosen.get_path())
        dialog.open(self, None, finish)

    def on_fsr411_button(self, _button):
        if self.fsr411_build:
            self.stop_fsr411_build()
            return
        self.choose_dll(tr('AMD upscaler DLL (amd_fidelityfx_upscaler_dx12.dll)'),
                        self.on_fsr411_dll)

    def on_fsr412_button(self, _button):
        if self.fsr411_build:
            self.stop_fsr411_build()
            return
        dialog = Gtk.FileDialog(title=tr("AMD's driver: the Adrenalin installer (.exe) or amdxcffx64.dll"))
        files = Gtk.FileFilter()
        files.set_name("EXE, DLL")
        for pattern in ("*.exe", "*.EXE", "*.dll", "*.DLL"):
            files.add_pattern(pattern)
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(files)
        dialog.set_filters(filters)
        dialog.set_default_filter(files)

        def finish(dialog, result):
            try:
                chosen = dialog.open_finish(result)
            except GLib.Error:
                return
            if chosen and chosen.get_path():
                self.on_fsr412_driver(chosen.get_path())
        dialog.open(self, None, finish)

    def on_fsr412_driver(self, driver):
        """The driver's FSR 4 module answers through AMD's upscaler DLL: the one FSR 4.1.1 was
        built from, else chosen now."""
        upscaler = self.settings.get("fsr411_dll", "")
        if upscaler and Path(upscaler).is_file():
            self.start_fsr411_build(driver, upscaler)
            return
        self.toasts.add_toast(Adw.Toast(title=tr(
            'The FSR 4.1.x DLL is needed too: choose amd_fidelityfx_upscaler_dx12.dll'), timeout=8))
        self.choose_dll(tr('AMD upscaler DLL (amd_fidelityfx_upscaler_dx12.dll)'),
                        lambda path: self.start_fsr411_build(driver, path))

    def on_fsr411_dll(self, upscaler, loader=None):
        import dll_info
        self.settings["fsr411_dll_dir"] = str(Path(upscaler).parent)
        self.settings["fsr411_dll"] = str(upscaler)
        status, problem, details = dll_info.inspect(upscaler, loader)
        kind = details.get("kind")
        version = details.get("version", "?")
        if status == dll_info.UNSUPPORTED:
            if kind == "fsr4_0":
                body = tr(
                    "{}: FSR {}. This is AMD's official FSR 4.0.x: it is enabled on RDNA4 GPUs (RX 9000) only and does not start under Proton on other GPUs, so it cannot be recorded.\n\nFSR 4.1.x is needed: for example FSR4_LATEST from OptiScaler, or the DLL of a game with FSR 4.1.").format(
                        Path(upscaler).name, version)
            elif kind == "not_fsr4":
                body = tr('{}: this DLL has no FSR 4 inside. amd_fidelityfx_upscaler_dx12.dll version 4.1.x is needed.').format(Path(upscaler).name)
            else:
                models = ", ".join(details.get("models", [])) or tr('none')
                body = tr(
                    "{}: version {}, FSR 4 models: {}.\n\nbbport replays AMD's official FSR 4.1.x only (model v07_fp8_no_scale): for example FSR4_LATEST from OptiScaler, or amd_fidelityfx_upscaler_dx12.dll from a game with FSR 4.1. Community builds (4.0.2b and the like) are laid out differently and are not supported yet.").format(
                        Path(upscaler).name, version, models)
            self.alert(tr('This DLL does not fit'), body)
            return
        if status == dll_info.NO_LOADER:
            if kind == "loader_is_upscaler":
                self.alert(tr('This is not the loader'), tr(
                    '{} is the upscaler DLL. The loader is called amd_fidelityfx_loader_dx12.dll (amd_fidelityfx_dx12.dll in OptiScaler) and lies in the same game folder as the upscaler DLL.').format(Path(loader).name))
                return
            if loader:
                self.alert(tr('The loader does not fit'), tr(
                    '{}: version {}. A FidelityFX loader 2.x is needed, from the same game as the upscaler DLL.').format(Path(loader).name, details.get("loader_version", "?")))
                return
            # A DLL without the FidelityFX API exports (AMD's own export it: no loader needed), and no
            # loader next to it or one folder up: ask for it.
            self.toasts.add_toast(Adw.Toast(title=tr(
                "No loader next to the DLL: choose amd_fidelityfx_loader_dx12.dll from the game's folder"),
                timeout=8))
            self.choose_dll(tr('FidelityFX loader (amd_fidelityfx_loader_dx12.dll or amd_fidelityfx_dx12.dll)'),
                            lambda path: self.on_fsr411_dll(upscaler, path))
            return
        self.start_fsr411_build(upscaler, details.get("loader"))

    def start_fsr411_build(self, upscaler, loader):
        command, env = fsr411_build_command(upscaler, loader)
        launcher = Gio.SubprocessLauncher.new(
            Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_MERGE)
        launcher.set_environ([f"{k}={v}" for k, v in env.items()])
        launcher.set_cwd(str(PORT_DIR))
        try:
            # setsid: Proton and its helpers form one process group, cancelled together.
            self.fsr411_build = launcher.spawnv(["setsid", *command])
        except GLib.Error as error:
            self.toasts.add_toast(Adw.Toast(title=tr('Could not start: {}').format(error.message)))
            return
        self.append_log(tr('\n— building FSR 4.1.1 from {} —\n').format(upscaler))
        stream = Gio.DataInputStream.new(self.fsr411_build.get_stdout_pipe())
        stream.read_line_async(GLib.PRIORITY_DEFAULT, None, self.on_fsr411_line)
        self.fsr411_build.wait_async(None, self.on_fsr411_built)
        self.update_fsr411_row(tr('Preparing…'))
        self.update_launch_button()

    def on_fsr411_line(self, stream, result):
        try:
            line, _length = stream.read_line_finish_utf8(result)
        except GLib.Error:
            return
        if line is None:
            return
        self.append_log(line + "\n")
        if self.fsr411_build:
            step = re.match(r"\[\s*(\d+)/(\d+)\]", line)
            if step:
                self.update_fsr411_row(tr("Recording the DLL's passes under Proton: {} of {}").format(*step.groups()))
            elif line.startswith("Translating"):
                self.update_fsr411_row(tr('Translating the passes to SPIR-V…'))
        stream.read_line_async(GLib.PRIORITY_DEFAULT, None, self.on_fsr411_line)

    def stop_fsr411_build(self):
        pid = int(self.fsr411_build.get_identifier())
        self.fsr411_cancelled = True
        try:
            os.killpg(pid, signal.SIGTERM)
        except OSError:
            self.fsr411_build.force_exit()

    def on_fsr411_built(self, process, result):
        try:
            process.wait_finish(result)
        except GLib.Error:
            pass
        status = process.get_exit_status() if process.get_if_exited() else -1
        cancelled = getattr(self, "fsr411_cancelled", False)
        self.fsr411_cancelled = False
        self.fsr411_build = None
        self.update_fsr411_row()
        self.update_launch_button()
        self.append_log(tr('— FSR 4.1.1 build finished (code {}) —\n').format(status))
        if status == 0:
            for index, (_label, value) in enumerate(UPSCALERS):
                if value == "fsr411":
                    self.upscaler_row.set_selected(index)
            self.update_upscaler_status()
            self.store()
            self.toasts.add_toast(Adw.Toast(title=tr('FSR 4.1.1 built and selected')))
        elif cancelled:
            self.toasts.add_toast(Adw.Toast(title=tr('FSR 4.1.1 build cancelled')))
        else:
            message = FSR411_BUILD_ERRORS.get(status, 'The build failed (code {}): details in the log')
            self.alert(tr('FSR 4.1.1 was not built'), tr(message).format(status))
