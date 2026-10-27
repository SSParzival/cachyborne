#!/usr/bin/env python3
"""GTK4/Libadwaita control center, in the user's Bisexual Midnight palette."""
import copy
import os
import signal
import re
from pathlib import Path
import subprocess
import sys
import threading

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('Gdk', '4.0')
from gi.repository import Adw, Gio, GLib, Gtk, Gdk

import cachyborne as core
import cachyborne_options as options
from cachyborne_panels import NativePanels


def apply_style():
    manager = Adw.StyleManager.get_default()
    manager.set_color_scheme(Adw.ColorScheme.PREFER_DARK)
    provider = Gtk.CssProvider()
    errors = []
    provider.connect('parsing-error', lambda _provider, _section, error: errors.append(str(error)))
    provider.load_from_path(str(Path(__file__).with_name('styles') / 'midnight.css'))
    if errors:
        raise RuntimeError('Midnight stylesheet: ' + '; '.join(errors))
    def update(*_):
        display = Gdk.Display.get_default()
        if display is None:
            return
        Gtk.StyleContext.remove_provider_for_display(display, provider)
        if not manager.get_high_contrast():
            Gtk.StyleContext.add_provider_for_display(display, provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
    manager.connect('notify::high-contrast', update)
    update()
    return provider


def label(text, css='', wrap=False):
    widget = Gtk.Label(label=text, xalign=0, wrap=wrap)
    for name in css.split():
        widget.add_css_class(name)
    return widget


def action(text, callback, primary=False, icon=None):
    button = Gtk.Button()
    box = Gtk.Box(spacing=8, halign=Gtk.Align.CENTER)
    if icon:
        box.append(Gtk.Image.new_from_icon_name(icon))
    box.append(Gtk.Label(label=text))
    button.set_child(box)
    button.add_css_class('primary-action' if primary else 'secondary-action')
    button.connect('clicked', lambda *_: callback())
    return button


class Preferences(NativePanels, Adw.ApplicationWindow):
    def __init__(self, app):
        super().__init__(application=app, title='Cachyborne · Bisexual Midnight', default_width=1130, default_height=850)
        self.set_icon_name('cachyborne')
        self.add_css_class('cachyborne')
        self.settings = core.preferences()
        self.ini = dict(self.settings.get('runtime_ini', {}))
        self.fields, self.widgets = {}, {}
        self.busy = False
        self.setup_process = None
        self.cancelled = False
        self.close_after_setup = False
        self.set_size_request(820, 640)
        shell = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        stripe = Gtk.Box(); stripe.add_css_class('pride-line'); shell.append(stripe)
        header = Adw.HeaderBar()
        Gtk.Settings.get_default().set_property('gtk-decoration-layout', ':minimize,maximize,close')
        header.set_title_widget(label('BLOODBORNE  /  CACHYOS', 'eyebrow'))
        shell.append(header)
        body = Gtk.Box(vexpand=True)
        self.stack = Gtk.Stack(transition_type=Gtk.StackTransitionType.CROSSFADE, hexpand=True, vexpand=True)
        sidebar = self.sidebar()
        body.append(sidebar); body.append(self.stack); shell.append(body)
        self.toasts = Adw.ToastOverlay(child=shell)
        self.set_content(self.toasts)
        self.build_home(); self.build_performance(); self.build_graphics(); self.build_controls(); self.build_content(); self.build_online(); self.build_advanced(); self.build_setup()
        footer = Gtk.Box(spacing=12); footer.add_css_class('action-bar')
        self.spinner = Gtk.Spinner(); footer.append(self.spinner)
        self.status_text = label('Listo para tu próxima cacería', 'muted small'); self.status_text.set_hexpand(True)
        footer.append(self.status_text)
        footer.append(action('Guardar ajustes', self.save))
        footer.append(action('Lanzar juego', self.launch, primary=True, icon='media-playback-start-symbolic'))
        shell.append(footer)
        self.connect('close-request', self.on_close)
        self.autosave_source = None
        self.updating_widgets = False
        for key, (widget, kind, _) in self.widgets.items():
            event = {'entry':'changed', 'switch':'notify::active', 'combo':'notify::selected', 'spin':'notify::value'}[kind]
            widget.connect(event, self.queue_autosave)
            if key in ('ini.preset', 'upscaler', 'output_res'):
                widget.connect(event, self.on_scaling_changed)
        self.asset_check_source = None
        for key in ('game', 'fsr4_dir', 'render_res', 'upscaler', 'output_res', 'ini.preset'):
            widget, kind, _ = self.widgets[key]
            widget.connect('changed' if kind == 'entry' else 'notify::selected', self.queue_asset_check)
        self.queue_asset_check()
        self.refresh_status()
        self.navigate('setup' if '--setup' in sys.argv else 'home')

    def sidebar(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8, width_request=200)
        box.add_css_class('sidebar')
        brand = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8, margin_bottom=28)
        brand.append(label('CACHYBORNE', 'brand'))
        brand.append(label('BISEXUAL MIDNIGHT', 'eyebrow'))
        brand.append(label('La noche es tuya.', 'muted small'))
        box.append(brand)
        self.nav = {}
        first = None
        for key, title, icon in [('home', 'Inicio', 'weather-clear-night-symbolic'),
                                 ('performance', 'Rendimiento', 'speedometer-symbolic'),
                                 ('graphics', 'Imagen y sonido', 'video-display-symbolic'),
                                 ('controls', 'Controles', 'input-gaming-symbolic'),
                                 ('content', 'Mods y parches', 'folder-symbolic'),
                                 ('online', 'Online y cuentas', 'network-workgroup-symbolic'),
                                 ('advanced', 'Avanzado', 'preferences-system-symbolic'),
                                 ('setup', 'Preparar juego', 'folder-open-symbolic')]:
            button = Gtk.ToggleButton(); button.add_css_class('nav-button')
            row = Gtk.Box(spacing=12); row.append(Gtk.Image.new_from_icon_name(icon)); row.append(label(title))
            button.set_child(row)
            if first: button.set_group(first)
            else: first = button
            button.connect('toggled', lambda widget, name=key: self.stack.set_visible_child_name(name) if widget.get_active() else None)
            self.nav[key] = button; box.append(button)
        spacer = Gtk.Box(vexpand=True); box.append(spacer)
        foot = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=7); foot.add_css_class('sidebar-foot')
        foot.append(label('HECHO PARA TU PC', 'eyebrow'))
        foot.append(label('Intel i5-14600KF', 'small'))
        foot.append(label('Radeon RX 9070 XT', 'small'))
        foot.append(label('16 GB DDR5 · Linux nativo', 'muted small'))
        box.append(foot)
        return box

    def navigate(self, name):
        self.nav[name].set_active(True)
        self.stack.set_visible_child_name(name)

    def page(self, name, title, subtitle):
        scroll = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=22, margin_start=30, margin_end=30,
                      margin_top=26, margin_bottom=30)
        heading = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        heading.append(label(title, 'page-title')); heading.append(label(subtitle, 'muted', True))
        box.append(heading)
        clamp = Adw.Clamp(maximum_size=1050, child=box)
        scroll.set_child(clamp); self.stack.add_named(scroll, name)
        return box

    def group(self, page, title, description=None):
        group = Adw.PreferencesGroup(title=GLib.markup_escape_text(title))
        if description: group.set_description(description)
        page.append(group)
        return group

    def current(self, key):
        if key.startswith('ini.'): return self.ini.get(key[4:], '')
        if key.startswith('legacy.'): return self.settings.get('original_settings', {}).get(key[7:], options.LEGACY_DEFAULTS[key[7:]])
        return self.settings.get(key, '')

    def register(self, key, widget, getter, kind, values=None):
        self.fields[key] = getter
        self.widgets[key] = (widget, kind, values)

    def switch(self, group, key, title, subtitle=''):
        row = Adw.SwitchRow(title=title, subtitle=subtitle)
        value = self.current(key)
        row.set_active(value == '1' if key.startswith('ini.') else bool(value))
        group.add(row); self.register(key, row, row.get_active, 'switch')
        return row

    def entry(self, group, key, title, subtitle=''):
        row = Adw.EntryRow(title=title, text=str(self.current(key)))
        if subtitle: row.set_tooltip_text(subtitle)
        (group.add_row if isinstance(group, Adw.ExpanderRow) else group.add)(row)
        self.register(key, row, lambda: row.get_text().strip(), 'entry')
        return row

    def combo(self, group, key, title, values, labels):
        row = Adw.ComboRow(title=title, model=Gtk.StringList.new(labels))
        value = self.current(key)
        row.set_selected(values.index(value) if value in values else 0)
        group.add(row); self.register(key, row, lambda: values[row.get_selected()], 'combo', values)
        return row

    def spin(self, group, key, title, minimum, maximum, step=1, digits=0):
        row = Adw.SpinRow.new_with_range(minimum, maximum, step)
        row.set_title(title); row.set_digits(digits); row.set_value(float(self.current(key) or minimum))
        group.add(row)
        self.register(key, row, lambda: round(row.get_value(), digits) if digits else int(row.get_value()), 'spin')
        return row

    def build_home(self):
        page = self.page('home', 'Tu centro de cacería', 'Todo preparado. Cada ajuste, a tu manera.')
        hero = Gtk.Box(spacing=24); hero.add_css_class('hero')
        copy_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14, hexpand=True)
        copy_box.append(label('NATIVE LINUX  ·  RDNA 4', 'eyebrow'))
        copy_box.append(label('LA NOCHE\nES TUYA.', 'hero-title'))
        copy_box.append(label('Un perfil de alta fluidez para tu CachyOS.\nSin recompilar en cada lanzamiento.', 'muted', True))
        self.profile_badge = label('Perfil preparado', 'pill ready'); self.profile_badge.set_halign(Gtk.Align.START)
        copy_box.append(self.profile_badge)
        hero.append(copy_box)
        if core.icon_path().is_file():
            picture = Gtk.Picture.new_for_filename(str(core.icon_path()))
            picture.set_content_fit(Gtk.ContentFit.CONTAIN); picture.set_size_request(192, 192)
            picture.add_css_class('hero-art'); picture.set_overflow(Gtk.Overflow.HIDDEN); hero.append(picture)
        page.append(hero)
        metrics = Gtk.Box(spacing=12, homogeneous=True)
        for value, title, note, color in [('144', 'FPS OBJETIVO', 'Límite del perfil recomendado', 'pink'),
                                          ('6,94 ms', 'PRESUPUESTO POR CUADRO', 'El rendimiento real depende de la escena', 'purple'),
                                          ('16 GB', 'MEMORIA DEL SISTEMA', 'Juego en disco · cachés limitadas', 'blue')]:
            card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8); card.add_css_class('metric')
            card.append(label(value, f'metric-value {color}')); card.append(label(title, 'eyebrow', True))
            card.append(label(note, 'muted small', True)); metrics.append(card)
        page.append(metrics)
        profile = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12); profile.add_css_class('info-card')
        profile.append(label('144 FPS · Fluidez', 'info-title'))
        profile.append(label('FSR 4 · objetivo 144 FPS · salida 1440p · P-Cores para el juego. '
                             'Tu calidad de escalado se conserva. La precarga de 30 GB queda desactivada.', 'muted', True))
        buttons = Gtk.Box(spacing=10)
        buttons.append(action('Perfil recomendado para este PC (1440p · 144 FPS)', self.apply_recommended, primary=True))
        buttons.append(action('Preparar y aplicar', lambda: self.navigate('setup')))
        profile.append(buttons); page.append(profile)
        page.append(label('El objetivo de 144 FPS no es una garantía. Usa el contador y las estadísticas '
                          'para comprobar el rendimiento en tu partida.', 'muted small', True))

    def build_performance(self):
        page = self.page('performance', 'Fluidez, sin desperdicio', 'CPU híbrida, 16 GB y RADV. Ajustes pensados para tu hardware.')
        cpu = self.group(page, 'Intel i5-14600KF')
        self.switch(cpu, 'pinning', 'Afinidad por función del hilo', 'Juego y render en P-Cores; compilación en E-Cores.')
        self.entry(cpu, 'p_cpus', 'P-Cores', 'CPU 0–11 en la topología de tu equipo')
        self.entry(cpu, 'e_cpus', 'E-Cores', 'CPU 12–19 en la topología de tu equipo')
        memory = self.group(page, 'Memoria con límites claros')
        memory.add(Adw.ActionRow(title='256 MiB de caché de recursos · 2 compiladores',
                                 subtitle='5056 MiB de memoria directa hasta 1080p interno; 9152 MiB por encima.'))
        self.switch(memory, 'ramdisk', 'Precargar el juego completo en RAM', 'Desactivado para 16 GB: el dump ocupa unos 30 GB.')
        self.switch(memory, 'radv', 'Perfil AMD RADV / ACO', 'Caché persistente de 4G en disco y SAM gestionado por el driver.')
        self.switch(memory, 'gamemode', 'GameMode', 'Requiere gamemoderun instalado.')
        self.combo(memory, 'scheduler', 'Scheduler', ['current', 'scx_lavd', 'scx_bpfland'], ['Actual', 'scx_lavd activo', 'scx_bpfland activo'])
        scope = self.group(page, 'Gamescope · opcional', 'Se mantiene apagado en el perfil 144 para evitar un compositor adicional.')
        self.switch(scope, 'gamescope', 'Lanzar con Gamescope')
        self.entry(scope, 'width', 'Ancho de salida'); self.entry(scope, 'height', 'Alto de salida')
        self.switch(scope, 'hdr', 'HDR'); self.switch(scope, 'vrr', 'Adaptive Sync / VRR')
        self.switch(scope, 'gamescope_fsr', 'FSR espacial de Gamescope')
        page.append(action('Diagnóstico y hardware', lambda: self.navigate('advanced')))

    def build_graphics(self):
        page = self.page('graphics', 'Cada detalle, a tu gusto', 'Escalado, presentación y efectos. Sin ocultar el control fino.')
        display = self.group(page, 'Pantalla y escalado')
        self.combo(display, 'fps', 'FPS objetivo', ['30','60','90','120','144','165','uncap'], ['30','60','90','120','144','165','Desbloqueado'])
        self.spin(display, 'fps_limit', 'Límite personalizado (0 usa el objetivo)', 0, 1000)
        self.combo(display, 'upscaler', 'Escalador', ['off','taa','fsr3','fsr4','fsr411','dlss'], ['Sin escalado','TAA nativo','FSR 3.1','FSR 4','FSR 4.1.1','DLSS (NVIDIA)'])
        self.combo(display, 'ini.preset', 'Calidad del escalador', ['0','1','2','3','4'], ['Native AA','Quality','Balanced','Performance','Ultra Performance'])
        self.entry(display, 'render_res', 'Resolución interna (WxH)'); self.entry(display, 'output_res', 'Resolución de salida (WxH)')
        self.native_button(display, 'Calcular resolución interna desde el preset', self.calculate_resolution)
        self.switch(display, 'fullscreen', 'Pantalla completa')
        self.combo(display, 'present_mode', 'Presentación', ['mailbox','fifo','fiforelaxed','immediate'], ['Mailbox','FIFO / VSync','FIFO Relaxed','Immediate (permite tearing)'])
        self.switch(display, 'ini.sharpen', 'Nitidez RCAS'); self.spin(display, 'ini.sharpness', 'Intensidad de nitidez', 0, 2, .05, 2)
        self.switch(display, 'ini.object_motion', 'Vectores de movimiento de objetos')
        self.switch(display, 'ini.show_fps', 'Mostrar FPS en el juego')
        effects = self.group(page, 'Efectos de Bloodborne')
        for key, title, default in [('chromatic_aberration','Aberración cromática','0'), ('dof','Profundidad de campo','0'),
                                     ('motion_blur','Desenfoque de movimiento','0'), ('ssao','Oclusión ambiental SSAO','1'),
                                     ('game_aa','Antialiasing original','1'), ('dynamic_shadows','Sombras dinámicas','1'),
                                     ('ssr','Reflejos SSR','0'), ('bloom','Bloom','1')]:
            self.ini.setdefault('effect_' + key, default)
            self.switch(effects, 'ini.effect_' + key, title)
        self.ini.setdefault('model_lod', '0')
        self.combo(effects, 'ini.model_lod', 'Detalle de modelos', ['-2','0','1','2'], ['Máximo','Original','Bajo','Mínimo'])
        audio = self.group(page, 'Audio y assets opcionales')
        self.switch(audio, 'audio', 'Audio activado'); self.entry(audio, 'fsr411_dir', 'Assets de FSR 4.1.1')
        self.add_language_controls(page)
        page.append(action('Pantallas, GPU y assets FSR/DLSS', lambda: self.navigate('advanced')))

    def build_controls(self):
        page = self.page('controls', 'Tu forma de jugar', 'Todos los controles originales siguen disponibles.')
        controls = self.group(page, 'Mando y ratón')
        self.entry(controls, 'gamepad', 'Mando SDL (nombre o GUID)', 'Vacío: selección automática.')
        for key, default in [('mouse_look','1'), ('mouse_sensitivity','1.00'), ('mouse_invert_y','0')]: self.ini.setdefault(key, default)
        self.switch(controls, 'ini.mouse_look', 'Controlar cámara con el ratón', 'Clic para capturar; F1 para liberar.')
        self.spin(controls, 'ini.mouse_sensitivity', 'Sensibilidad', .1, 10, .05, 2)
        self.switch(controls, 'ini.mouse_invert_y', 'Invertir eje vertical')
        self.add_native_controls(page)
        page.append(action('Crear accesos de juego y configuración', self.desktop))

    def build_setup(self):
        page = self.page('setup', 'Una vez preparado, solo juega', 'Validación y enlace aquí. Lanzamiento directo después.')
        for number, title, text in [('01','Elige tu dump 1.09','Archivos descifrados de Bloodborne con la actualización ya combinada.'),
                                     ('02','Ajusta tu perfil','144 FPS objetivo, FSR 4 y memoria acotada para 16 GB.'),
                                     ('03','Valida y prepara','El perfil anterior se conserva si la preparación falla.')]:
            row = Gtk.Box(spacing=14); badge = label(number, 'step-number'); badge.set_valign(Gtk.Align.START)
            row.append(badge); text_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
            text_box.append(label(title, 'info-title')); text_box.append(label(text, 'muted small', True)); row.append(text_box); page.append(row)
        game = self.group(page, 'Archivos del juego')
        self.path = Adw.EntryRow(title='Ruta de app0 / CUSA03173')
        previous = core.read_json(core.CONFIG / 'game_profile.json', {})
        self.path.set_text(self.settings.get('game') or previous.get('source_game', previous.get('game', '')))
        self.register('game', self.path, lambda: self.path.get_text().strip(), 'entry')
        self.path.connect('changed', self.update_languages)
        game.add(self.path)
        self.asset_status = Adw.ActionRow(title='Comprobando archivos…')
        self.asset_status.set_title_lines(4)
        game.add(self.asset_status)
        buttons = Gtk.Box(spacing=10)
        buttons.append(action('Seleccionar carpeta', self.choose, icon='folder-open-symbolic'))
        self.setup_button = action('Validar y preparar', self.prepare, primary=True)
        buttons.append(self.setup_button); page.append(buttons)
        page.append(action('Guardar como mis predeterminados', lambda: self.save(as_default=True)))
        page.append(action('Restaurar mis predeterminados', self.restore_defaults))
        self.progress = Gtk.ProgressBar(show_text=True, text='Listo para preparar'); page.append(self.progress)
        self.status = Adw.ActionRow(title='Listo', subtitle='Los cambios se aplican al completar la preparación.')
        status_group = Adw.PreferencesGroup()
        status_group.add(self.status); page.append(status_group)
        self.log = Gtk.TextView(editable=False, monospace=True, wrap_mode=Gtk.WrapMode.WORD_CHAR)
        self.log.add_css_class('console')
        scroll = Gtk.ScrolledWindow(min_content_height=180, child=self.log)
        page.append(scroll)

    def queue_asset_check(self, *_):
        if self.asset_check_source is not None:
            GLib.source_remove(self.asset_check_source)
        self.asset_check_source = GLib.timeout_add(350, self.check_assets)

    def check_assets(self):
        self.asset_check_source = None
        try:
            self.asset_status.set_title(core.preparation_status(self.collect()))
        except (OSError, ValueError, TypeError) as error:
            self.asset_status.set_title(f'Revisa los ajustes: {error}')
        return GLib.SOURCE_REMOVE

    def refresh_status(self):
        profile = core.read_json(core.CONFIG / 'game_profile.json', {})
        prepared = profile.get('settings', {})
        self.profile_badge.set_text(f"Preparado · {prepared.get('fps', '?')} FPS objetivo" if profile else 'Setup pendiente')

    def collect(self, validate=False):
        settings = copy.deepcopy(self.settings)
        ini = dict(self.ini)
        original = dict(options.LEGACY_DEFAULTS) | settings.get('original_settings', {})
        for key, getter in self.fields.items():
            value = getter()
            if key.startswith('ini.'):
                # Explicit defaults override an older prepared INI after resetting a binding.
                ini[key[4:]] = ('1' if value else '0') if isinstance(value, bool) else str(value)
            elif key.startswith('legacy.'): original[key[7:]] = value
            else: settings[key] = value
        settings.update(game=self.path.get_text().strip(), runtime_ini=ini, original_settings=original)
        settings.update(self.content_selection())
        if validate:
            settings = options.reconcile(settings)
            settings.update(width=int(settings['width']), height=int(settings['height']))
            core.validate_settings(settings)
        return settings

    def set_widgets(self):
        self.updating_widgets = True
        for key, (widget, kind, values) in self.widgets.items():
            value = self.current(key)
            if kind == 'switch': widget.set_active(value == '1' if key.startswith('ini.') else bool(value))
            elif kind == 'entry': widget.set_text(str(value))
            elif kind == 'spin': widget.set_value(float(value or 0))
            elif kind == 'combo': widget.set_selected(values.index(value) if value in values else 0)

        self.updating_widgets = False

    def apply_recommended(self):
        try:
            self.settings = core.performance_settings(self.collect(), '144')
            self.ini = dict(self.settings['runtime_ini']); self.set_widgets()
            self.save(); self.report('Perfil 144 seleccionado. Prepara el juego para aplicarlo.')
        except (OSError, ValueError) as error:
            self.report(str(error))

    def report(self, text):
        lines = text.splitlines()
        if lines: self.status.set_title(lines[0][:180])
        if hasattr(self, 'status_text') and lines: self.status_text.set_text(lines[0][:100])
        buffer = self.log.get_buffer(); buffer.insert(buffer.get_end_iter(), text + '\n')
        extra = buffer.get_line_count() - 3000
        if extra > 0: buffer.delete(buffer.get_start_iter(), buffer.get_iter_at_line(extra)[1])
        if hasattr(self, 'progress'):
            match = re.search(r'(\d+) of (\d+) checked', text)
            if match:
                count, total = map(int, match.groups())
                self.progress.set_fraction(count / max(total, 1)); self.progress.set_text(f'Verificando archivos · {count:,} / {total:,}')
        return GLib.SOURCE_REMOVE

    def queue_autosave(self, *_):
        if self.autosave_source is not None: GLib.source_remove(self.autosave_source)
        self.autosave_source = GLib.timeout_add(700, self.autosave)

    def autosave(self):
        self.autosave_source = None
        try: core.save_preferences(self.collect())
        except OSError as error: self.report(f'No se pudo guardar el borrador: {error}')
        return GLib.SOURCE_REMOVE

    def on_scaling_changed(self, *_):
        if not self.updating_widgets: self.calculate_resolution()

    def calculate_resolution(self):
        try:
            import patches
            s = self.collect(validate=True)
            sizes = patches.scaled_sizes(s['runtime_ini'])
            size = sizes[0] if sizes else patches.render_size(s['runtime_ini']) or patches.output_size(s['runtime_ini'])
            self.widgets['render_res'][0].set_text(f'{size[0]}x{size[1]}')
        except (ValueError, TypeError) as error: self.report(str(error))

    def save(self, as_default=False):
        try:
            # Store raw widget intent before any conversion/validation can fail.
            self.settings = core.save_preferences(self.collect(), as_default=as_default)
            self.ini = dict(self.settings['runtime_ini'])
            try:
                normalized = self.collect(validate=True)
            except (ValueError, TypeError) as error:
                self.report(f'Borrador guardado. Corrige antes de preparar: {error}')
                return True
            self.settings = core.save_preferences(normalized, as_default=as_default)
            self.ini = dict(self.settings['runtime_ini'])
            self.report('Ajustes guardados' + (' como predeterminados personales.' if as_default else '. Prepara el juego para aplicarlos.'))
            return True
        except OSError as error:
            self.report(f'No se pudo guardar: {error}'); return False

    def restore_defaults(self):
        try:
            defaults = core.read_json(core.CONFIG / 'defaults.json', {})
            if not defaults: raise ValueError('Aún no hay predeterminados personales guardados.')
            self.settings = copy.deepcopy(core.DEFAULTS | defaults)
            self.ini = dict(self.settings['runtime_ini']); self.set_widgets(); self.refresh_content()
            self.save()
        except (ValueError, OSError) as error: self.report(str(error))

    def choose(self):
        chooser = Gtk.FileChooserNative(title='Seleccionar app0', transient_for=self, action=Gtk.FileChooserAction.SELECT_FOLDER)
        def response(dialog, result):
            if result == Gtk.ResponseType.ACCEPT: self.path.set_text(dialog.get_file().get_path())
            dialog.destroy()
        chooser.connect('response', response); chooser.show(); self.chooser = chooser

    def prepare(self):
        if self.busy:
            self.cancel_setup(); return
        if not self.save(): return
        try: self.collect(validate=True)
        except (ValueError, TypeError) as error:
            self.report(f'Ajustes conservados. Corrige antes de preparar: {error}'); return
        self.busy = True; self.cancelled = False
        self.setup_button.set_label('Cancelar preparación'); self.spinner.start()
        self.progress.set_fraction(0); self.progress.set_text('Validando el ejecutable…')
        self.report('Validando y preparando el juego…')
        game = self.settings['game']
        def worker():
            try:
                with subprocess.Popen([str(core.ROOT / 'cachyborne-setup.sh'), game], stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, text=True, start_new_session=True) as process:
                    self.setup_process = process
                    if self.cancelled:
                        os.killpg(process.pid, signal.SIGTERM)
                    for line in process.stdout: GLib.idle_add(self.report, line.rstrip())
                    code = process.wait()
                message = ('Preparación cancelada. Perfil anterior conservado.' if self.cancelled else
                           'Setup completado.' if code == 0 else f'Setup falló ({code}). Perfil anterior conservado.')
                GLib.idle_add(self.report, message)
                GLib.idle_add(self.finished, code == 0)
            except OSError as error:
                GLib.idle_add(self.report, str(error)); GLib.idle_add(self.finished, False)
        threading.Thread(target=worker, daemon=True).start()

    def finished(self, success=False):
        self.busy = False; self.setup_process = None
        self.setup_button.set_label('Validar y preparar'); self.setup_button.set_sensitive(True); self.spinner.stop()
        self.progress.set_fraction(1 if success else 0)
        self.progress.set_text('Perfil preparado' if success else 'Preparación detenida')
        self.refresh_status()
        if self.close_after_setup: self.close()
        return GLib.SOURCE_REMOVE

    def cancel_setup(self):
        self.cancelled = True
        if self.setup_process is not None and self.setup_process.poll() is None:
            try: os.killpg(self.setup_process.pid, signal.SIGTERM)
            except ProcessLookupError: pass
        self.report('Cancelando la preparación…')

    def on_close(self, _window):
        if self.busy:
            self.close_after_setup = True; self.cancel_setup(); return True
        if self.autosave_source is not None:
            GLib.source_remove(self.autosave_source); self.autosave_source = None
        if self.asset_check_source is not None:
            GLib.source_remove(self.asset_check_source); self.asset_check_source = None
        self.save()
        return False

    def launch(self):
        try:
            profile = core.load_profile(); core.check_runtime_requirements(profile)
            subprocess.Popen([str(core.ROOT / 'cachyborne-launch.sh')])
            self.report('Lanzamiento solicitado con el último perfil preparado.')
        except (OSError, ValueError, RuntimeError) as error: self.report(str(error))

    def desktop(self):
        try:
            core.install_shortcuts(on_desktop=True)
            self.report('Accesos de juego y configuración creados en el escritorio.')
        except (OSError, ValueError) as error: self.report(str(error))



class App(Adw.Application):
    def __init__(self):
        super().__init__(application_id='org.cachyborne.Preferences', flags=Gio.ApplicationFlags.NON_UNIQUE)
        self.css_provider = None

    def do_activate(self):
        if self.css_provider is None: self.css_provider = apply_style()
        window = self.get_active_window() or Preferences(self)
        window.present()


if __name__ == '__main__':
    sys.exit(App().run([sys.argv[0]]))
