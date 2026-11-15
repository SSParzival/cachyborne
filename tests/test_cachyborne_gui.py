"""GUI callback regressions that do not require opening a window or a display."""
from paths import ROOT
import importlib.util
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

try:
    sys.path.insert(0, str(ROOT / 'launcher'))
    spec = importlib.util.spec_from_file_location('cachyborne_gui', ROOT / 'launcher/cachyborne_gui.py')
    gui = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gui)
except (ImportError, ValueError):
    gui = None
finally:
    sys.path.remove(str(ROOT / 'launcher'))


@unittest.skipIf(gui is None, 'GTK4/Libadwaita Python bindings unavailable')
class GuiCallbackTests(unittest.TestCase):
    def test_empty_setup_log_line_does_not_crash_or_clear_status(self):
        view = SimpleNamespace(status=MagicMock(), log=MagicMock())
        view.log.get_buffer.return_value.get_line_count.return_value = 1
        gui.Preferences.report(view, '')
        view.status.set_title.assert_not_called()
        view.log.get_buffer.return_value.insert.assert_called_once()

    def test_shortcut_action_installs_both_desktop_entries(self):
        view = SimpleNamespace(report=MagicMock())
        with patch.object(gui.core, 'install_shortcuts') as install:
            gui.Preferences.desktop(view)
        install.assert_called_once_with(on_desktop=True)
        view.report.assert_called_once()

    def test_cancel_targets_only_the_setup_process_group(self):
        process = MagicMock(pid=12345)
        process.poll.return_value = None
        view = SimpleNamespace(setup_process=process, cancelled=False, report=MagicMock())
        with patch.object(gui.os, 'killpg') as stop:
            gui.Preferences.cancel_setup(view)
        stop.assert_called_once_with(12345, gui.signal.SIGTERM)
        self.assertTrue(view.cancelled)

    def test_setup_progress_line_updates_real_fraction(self):
        view = SimpleNamespace(status=MagicMock(), status_text=MagicMock(), log=MagicMock(), progress=MagicMock())
        view.log.get_buffer.return_value.get_line_count.return_value = 1
        gui.Preferences.report(view, '1000 of 2000 checked')
        view.progress.set_fraction.assert_called_once_with(0.5)

    def test_midnight_css_loads_without_parse_errors_and_host_summary_is_valid(self):
        provider = gui.apply_style()
        self.assertIsNotNone(provider)
        cpu, mem, distro = gui._host_hardware_summary()
        self.assertTrue(cpu and mem and distro)
        self.assertEqual(gui._format_size(512 * 1024**2), '512.0 MiB')
        self.assertEqual(gui._format_size(2 * 1024**3), '2.00 GiB')

    def test_clean_storage_updates_disk_row_and_reports_freed_bytes(self):
        view = SimpleNamespace(
            collect=lambda: {},
            report=MagicMock(),
            refresh_disk_status=MagicMock(),
            disk_row=MagicMock(),
        )
        with patch.object(gui.core, 'reclaim_disk_space', return_value=dict(freed_bytes=4 * 1024**2)):
            gui.Preferences.clean_storage(view)
        view.report.assert_called_once()
        self.assertIn('4.0 MiB', view.report.call_args.args[0])
        view.refresh_disk_status.assert_called_once()

    def test_refresh_disk_status_formats_reclaimable_summary(self):
        view = SimpleNamespace(settings={}, disk_row=MagicMock())
        with patch.object(gui.core, 'disk_usage_summary',
                          return_value=dict(prepared_total=10 * 1024**2, total_reclaimable=5 * 1024**2)):
            gui.Preferences.refresh_disk_status(view)
        subtitle = view.disk_row.set_subtitle.call_args.args[0]
        self.assertIn('10.0 MiB', subtitle)
        self.assertIn('5.0 MiB', subtitle)
