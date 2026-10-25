#!/usr/bin/env python3
"""CachyOS preferences; ingestion and play have independent entry points."""
import os
from pathlib import Path
import subprocess
import sys
import threading

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Adw, Gio, GLib, Gtk

import cachyborne as core


class Preferences(Adw.PreferencesWindow):
    def __init__(self, app):
        super().__init__(application=app, title='Cachyborne', default_width=850, default_height=760)
        self.settings = core.preferences()
        self.fields = {}
        self.busy = False
        hardware = self.page('Rendimiento CachyOS & Hardware', 'speedometer-symbolic')
        cpu = self.group(hardware, 'Intel i5-14600KF')
        self.switch(cpu, 'pinning', 'Fijar afinidad a P-Cores')
        self.entry(cpu, 'p_cpus', 'CPU de rendimiento', '0-11 en la topología objetivo; comprueba lscpu -e')
        self.entry(cpu, 'e_cpus', 'CPU de tareas en segundo plano', '12-19: compilación de pipelines y caché')
        memory = self.group(hardware, 'Perfil de 16 GB de RAM y driver AMD')
        self.switch(memory, 'ramdisk', 'Pre-cargar juego en RAM', 'Desactivado en el perfil de 16 GB: el dump completo necesita unos 30 GB. Conserva 8 GiB libres.')
        self.switch(memory, 'radv', 'Perfil RADV / ACO para RX 9070 XT', 'Caché persistente de 4G; SAM gestionado por Mesa y el firmware')
        self.switch(memory, 'gamemode', 'Modo GameMode de Feral')
        self.combo(memory, 'scheduler', 'Scheduler requerido', ['current', 'scx_lavd', 'scx_bpfland'],
                   ['Actual', 'scx_lavd (activo en el sistema)', 'scx_bpfland (activo en el sistema)'])
        scope = self.group(hardware, 'Gamescope')
        self.switch(scope, 'gamescope', 'Usar Gamescope')
        self.entry(scope, 'width', 'Ancho de pantalla')
        self.entry(scope, 'height', 'Alto de pantalla')
        self.switch(scope, 'hdr', 'HDR')
        self.switch(scope, 'vrr', 'VRR / Adaptive Sync')
        self.switch(scope, 'gamescope_fsr', 'Escalado espacial FSR de Gamescope')

        graphics = self.page('Gráficos & Pantalla', 'video-display-symbolic')
        display = self.group(graphics, 'Renderizado')
        self.combo(display, 'fps', 'Límite de FPS', ['30', '60', '90', '120', '144', '165', 'uncap'],
                   ['30', '60', '90', '120', '144', '165', 'Desbloqueado'])
        self.combo(display, 'upscaler', 'Escalador', ['off', 'fsr3', 'fsr411'], ['Nativo', 'FSR 3.1', 'FSR 4.1.1 (requiere assets)'])
        self.entry(display, 'fsr411_dir', 'Directorio de assets FSR 4.1.1')
        self.entry(display, 'render_res', 'Resolución interna (WxH)')
        self.entry(display, 'output_res', 'Resolución de salida (WxH)')
        self.switch(display, 'fullscreen', 'Pantalla completa')
        self.combo(display, 'present_mode', 'Presentación', ['mailbox', 'fifo', 'immediate'],
                   ['Mailbox — sin tearing si está disponible', 'FIFO — sincronizada', 'Immediate — permite tearing'])
        peripherals = self.group(graphics, 'Audio & Controles')
        self.switch(peripherals, 'audio', 'Activar audio')
        self.entry(peripherals, 'gamepad', 'Mando (nombre o GUID SDL)', 'Vacío: selección automática de SDL3')

        setup = self.page('Configuración & Setup', 'preferences-system-symbolic')
        actions = self.group(setup, 'Preparación y lanzamiento')
        actions.set_description('Los ajustes se publican al completar Setup. Lanzar usa el último perfil validado.')
        self.path = Adw.EntryRow(title='Ruta del juego (app0)')
        previous = core.read_json(core.CONFIG / 'game_profile.json', {})
        self.path.set_text(self.settings.get('game') or previous.get('source_game', previous.get('game', '')))
        actions.add(self.path)
        self.button(actions, 'Seleccionar directorio', self.choose)
        self.entry(actions, 'mods_dir', 'Directorio de mods (opcional)')
        self.entry(actions, 'mods_config', 'Selección mods.json (opcional)')
        self.entry(actions, 'patches_dir', 'Directorio de parches XML (opcional)')
        self.entry(actions, 'patches_config', 'Selección patches.json (opcional)')
        self.setup_button = self.button(actions, 'Configurar Juego / Setup', self.prepare)
        self.button(actions, 'Lanzar Juego', self.launch)
        self.button(actions, 'Crear Acceso Directo', self.desktop)
        self.button(actions, 'Guardar ajustes para el próximo Setup', self.save)
        self.status = Adw.ActionRow(title='Listo')
        self.status.set_subtitle('Setup valida y prepara; Lanzar ejecuta directamente bb-probe.')
        actions.add(self.status)
        logs = self.group(setup, 'Registro de setup / precarga')
        self.log = Gtk.TextView(editable=False, monospace=True, wrap_mode=Gtk.WrapMode.WORD_CHAR)
        scroll = Gtk.ScrolledWindow(min_content_height=180, child=self.log)
        logs.add(scroll)
        self.pages = setup
        if '--setup' in sys.argv:
            self.set_visible_page(setup)

    def page(self, title, icon):
        page = Adw.PreferencesPage(title=title, icon_name=icon)
        self.add(page)
        return page

    def group(self, page, title):
        group = Adw.PreferencesGroup(title=GLib.markup_escape_text(title))
        page.add(group)
        return group

    def switch(self, group, key, title, subtitle=''):
        row = Adw.ActionRow(title=title, subtitle=subtitle)
        widget = Gtk.Switch(active=bool(self.settings[key]), valign=Gtk.Align.CENTER)
        row.add_suffix(widget)
        row.set_activatable_widget(widget)
        group.add(row)
        self.fields[key] = lambda: widget.get_active()

    def entry(self, group, key, title, subtitle=''):
        row = Adw.EntryRow(title=title)
        row.set_text(str(self.settings[key]))
        if subtitle:
            row.set_tooltip_text(subtitle)
        group.add(row)
        self.fields[key] = lambda: row.get_text().strip()

    def combo(self, group, key, title, values, labels):
        row = Adw.ComboRow(title=title, model=Gtk.StringList.new(labels))
        row.set_selected(values.index(self.settings[key]))
        group.add(row)
        self.fields[key] = lambda: values[row.get_selected()]

    def button(self, group, title, callback):
        row = Adw.ActionRow(title=title, activatable=True)
        button = Gtk.Button(label=title, valign=Gtk.Align.CENTER)
        button.connect('clicked', lambda *_: callback())
        row.add_suffix(button)
        row.set_activatable_widget(button)
        group.add(row)
        return button

    def report(self, text):
        self.status.set_title(text.splitlines()[0][:180])
        buffer = self.log.get_buffer()
        buffer.insert(buffer.get_end_iter(), text + '\n')
        return GLib.SOURCE_REMOVE

    def save(self):
        try:
            settings = self.settings | {key: getter() for key, getter in self.fields.items()}
            settings['width'] = int(settings['width'])
            settings['height'] = int(settings['height'])
            settings['game'] = self.path.get_text().strip()
            core.validate_settings(settings)
            core.atomic_json(core.CONFIG / 'settings.json', settings)
            self.settings = settings
            self.report('Ajustes guardados. Ejecuta Setup para aplicarlos al perfil.')
            return True
        except (OSError, ValueError) as error:
            self.report(str(error))
            return False

    def choose(self):
        chooser = Gtk.FileChooserNative(title='Seleccionar app0', transient_for=self,
                                        action=Gtk.FileChooserAction.SELECT_FOLDER)
        def response(dialog, result):
            if result == Gtk.ResponseType.ACCEPT:
                self.path.set_text(dialog.get_file().get_path())
            dialog.destroy()
        chooser.connect('response', response)
        chooser.show()
        self.chooser = chooser

    def prepare(self):
        if self.busy or not self.save():
            return
        self.busy = True
        self.setup_button.set_sensitive(False)
        game = self.settings['game']
        self.report('Validando y preparando el juego…')
        def worker():
            try:
                with subprocess.Popen([str(core.ROOT / 'cachyborne-setup.sh'), game],
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as process:
                    for line in process.stdout:
                        GLib.idle_add(self.report, line.rstrip())
                    code = process.wait()
                GLib.idle_add(self.report, 'Setup completado.' if code == 0 else f'Setup falló ({code}). Perfil anterior conservado.')
            except OSError as error:
                GLib.idle_add(self.report, str(error))
            finally:
                GLib.idle_add(self.finished)
        threading.Thread(target=worker, daemon=True).start()

    def finished(self):
        self.busy = False
        self.setup_button.set_sensitive(True)
        return GLib.SOURCE_REMOVE

    def launch(self):
        try:
            profile = core.load_profile()
            core.check_runtime_requirements(profile)
            subprocess.Popen([str(core.ROOT / 'cachyborne-launch.sh')])
            self.report('Lanzamiento solicitado con el último perfil preparado.')
        except (OSError, RuntimeError) as error:
            self.report(str(error))

    def desktop(self):
        try:
            self.report(f'Acceso directo creado: {core.desktop()}')
        except OSError as error:
            self.report(str(error))


class App(Adw.Application):
    def __init__(self):
        super().__init__(application_id='org.cachyborne.Preferences', flags=Gio.ApplicationFlags.NON_UNIQUE)

    def do_activate(self):
        window = self.get_active_window() or Preferences(self)
        window.present()


if __name__ == '__main__':
    sys.exit(App().run([sys.argv[0]]))
