"""A compositor exiting must not leave its game/reaper running."""
from paths import ROOT
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from isolated_game import stop_group


class IsolatedCleanupTests(unittest.TestCase):
    def test_child_ignoring_term_is_killed_after_session_leader_exits(self):
        if not Path('/proc/self/stat').exists():
            self.skipTest('Linux process state required')
        for leader_exits_first in (False, True):
            with self.subTest(leader_exits_first=leader_exits_first), tempfile.TemporaryDirectory() as folder:
                ready = Path(folder) / 'ready'
                child = (
                    'import os,signal,sys,time; from pathlib import Path; '
                    'signal.signal(signal.SIGTERM,signal.SIG_IGN); '
                    'Path(sys.argv[1]).write_text(str(os.getpid())); time.sleep(60)'
                )
                leader = (
                    'import subprocess,sys,time; from pathlib import Path; '
                    'subprocess.Popen([sys.executable,"-c",sys.argv[1],sys.argv[2]]); '
                    '\nwhile not Path(sys.argv[2]).exists(): time.sleep(.01)\n'
                    'if sys.argv[3]=="wait": time.sleep(60)\n'
                )
                process = subprocess.Popen(
                    [sys.executable, '-c', leader, child, str(ready),
                     'exit' if leader_exits_first else 'wait'],
                    start_new_session=True, stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                try:
                    deadline = time.monotonic() + 5
                    while not ready.exists() and time.monotonic() < deadline:
                        time.sleep(.01)
                    self.assertTrue(ready.exists(), 'Child never became ready')
                    child_pid = int(ready.read_text())
                    if leader_exits_first:
                        self.assertEqual(process.wait(timeout=5), 0)
                    stop_group(process)
                    status = Path(f'/proc/{child_pid}/stat')
                    deadline = time.monotonic() + 2
                    while time.monotonic() < deadline:
                        try:
                            # A zombie has stopped and cannot retain the game heap.
                            state = status.read_text().rsplit(')', 1)[1].split()[0]
                        except FileNotFoundError:
                            break
                        if state == 'Z':
                            break
                        time.sleep(.01)
                    else:
                        self.fail('Owned child survived compositor cleanup')
                finally:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait(timeout=5)
