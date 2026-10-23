"""Exercise the native settings window on a private desktop."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import io
from pathlib import Path
import tarfile
import tempfile
import time
import tomllib
from recomp_error_dialog_test import PrivateProcess
from recomp_game_fixture import game_files, write_game_folder


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--game', required=True, type=Path)
    args = parser.parse_args()
    user = ctypes.WinDLL('user32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32')
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.CreateDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    user.CreateDesktopW.restype = wintypes.HANDLE
    user.CloseDesktop.argtypes = [wintypes.HANDLE]
    user.GetThreadDesktop.argtypes = [wintypes.DWORD]
    user.GetThreadDesktop.restype = wintypes.HANDLE
    user.SetThreadDesktop.argtypes = [wintypes.HANDLE]
    user.EnumDesktopWindows.argtypes = [wintypes.HANDLE, callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
    user.GetDlgItem.restype = wintypes.HWND
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.IsWindowEnabled.argtypes = [wintypes.HWND]
    user.IsWindow.argtypes = [wintypes.HWND]
    user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    desktop_name = 'ShaolinLauncher' + str(time.time_ns())
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x01ff, None)
    assert desktop
    previous = user.GetThreadDesktop(kernel.GetCurrentThreadId())
    assert user.SetThreadDesktop(desktop)
    try:
        with tempfile.TemporaryDirectory(prefix='shaolin-launcher-') as directory:
            root = Path(directory)
            data = root / 'data'
            data.mkdir()
            settings = data / 'settings.toml'
            settings.write_text('fps_cap = 30\ncustom_key = "preserve"\n[controller]\ndeadzone = 0.23\n')
            before = settings.read_bytes()

            def wait_window(process, title):
                windows = []

                def observe(window, _):
                    pid = wintypes.DWORD()
                    user.GetWindowThreadProcessId(window, ctypes.byref(pid))
                    text = ctypes.create_unicode_buffer(256)
                    user.GetWindowTextW(window, text, len(text))
                    if pid.value == process.pid and text.value == title and user.IsWindowVisible(window):
                        windows.append(window)
                    return True

                observer = callback_type(observe)
                deadline = time.monotonic() + 15
                while process.poll() is None and not windows and time.monotonic() < deadline:
                    user.EnumDesktopWindows(desktop, observer, 0)
                    time.sleep(.05)
                assert windows, 'The expected dialog did not open: ' + title
                return windows[0]

            for save in (False, True):
                with (root / 'process.log').open('wb') as output:
                    process = PrivateProcess([str(args.exe.resolve()), '--configure', '--data-dir', str(data)], desktop_name, output, root)
                    windows = []

                    def observe(window, _):
                        pid = wintypes.DWORD()
                        user.GetWindowThreadProcessId(window, ctypes.byref(pid))
                        text = ctypes.create_unicode_buffer(256)
                        user.GetWindowTextW(window, text, len(text))
                        if pid.value == process.pid and text.value == 'Shaolin Monks Recomp - Settings' and user.IsWindowVisible(window):
                            windows.append(window)
                        return True

                    observer = callback_type(observe)
                    try:
                        deadline = time.monotonic() + 15
                        while process.poll() is None and not windows and time.monotonic() < deadline:
                            user.EnumDesktopWindows(desktop, observer, 0)
                            time.sleep(.05)
                        assert windows, 'The settings launcher did not open'
                        window = windows[0]
                        combo = user.GetDlgItem(window, 2101)
                        assert user.SendMessageW(combo, 0x0146, 0, 0) >= 6, 'Brazilian Portuguese must always be selectable'
                        label = ctypes.create_unicode_buffer(128)
                        user.SendMessageW(combo, 0x0148, 5, ctypes.addressof(label))
                        assert label.value == 'Brazilian Portuguese'
                        assert user.SendMessageW(user.GetDlgItem(window, 2204), 0x1304, 0, 0) == 7, 'The Mods tab must be present'
                        assert not user.GetDlgItem(window, 2305), 'The launcher must not offer a frame-rate selector'
                        if save:
                            # Select French through the same language field the user edits.
                            user.SendMessageW(user.GetDlgItem(window, 2101), 0x014e, 1, 0)
                            sensitivity = ctypes.create_unicode_buffer('145')
                            user.SendMessageW(user.GetDlgItem(window, 2901), 0x000c, 0, ctypes.addressof(sensitivity))
                            user.SendMessageW(user.GetDlgItem(window, 2913), 0x014e, 7, 0)
                            user.PostMessageW(window, 0x0111, 2201, 0)
                        else:
                            user.PostMessageW(window, 0x0010, 0, 0)
                        assert process.wait(timeout=10) == 0
                    finally:
                        if process.poll() is None:
                            process.terminate()
                            process.wait(timeout=5)
                        process.close()
                if not save:
                    assert settings.read_bytes() == before, 'Closing the launcher must preserve settings'
                else:
                    config = tomllib.loads(settings.read_text())
                    assert config['language'] == 'fr'
                    assert config['fps_cap'] == 60, 'Saving must replace the old frame-rate preference with 60 FPS'
                    assert config['custom_key'] == 'preserve'
                    assert abs(config['controller']['deadzone'] - .23) < .0001
                    assert config['mouse']['sensitivity'] == 145
                    assert config['mouse']['bindings']['side1'] == 'left_trigger'
            saved = settings.read_bytes()
            # Selecting a local package must install it without another confirmation.
            game = root / 'original-game'
            write_game_folder(game, game_files(args.game.read_bytes()))
            payload = b'Local mod installation regression'
            manifest = dict(schema=2, id='LocalMod', name='Local mod', summary='Synthetic package', authors=['Tester'],
                            type='data', install_dir='packages/LocalMod', version='1.0.0', recomp='0.2.0',
                            game={'xbe_sha256': hashlib.sha256(args.game.read_bytes()).hexdigest()}, patches=[],
                            files=[dict(path='files/local.bin', size=len(payload), sha256=hashlib.sha256(payload).hexdigest())])
            archive = root / 'local.tar.xz'
            with tarfile.open(archive, 'w:xz', format=tarfile.USTAR_FORMAT) as tar:
                for name, content in {'mod.json': json.dumps(manifest).encode(), 'files/local.bin': payload}.items():
                    entry = tarfile.TarInfo(name)
                    entry.size = len(content)
                    tar.addfile(entry, io.BytesIO(content))
            local_data = root / 'local-install'
            with (root / 'local-install.log').open('wb') as output:
                process = PrivateProcess([str(args.exe.resolve()), '--configure', '--data-dir', str(local_data), '--disc', str(game)], desktop_name, output, root)
                try:
                    window = wait_window(process, 'Shaolin Monks Recomp - Settings')
                    tabs = user.GetDlgItem(window, 2204)
                    for _ in range(6): user.SendMessageW(tabs, 0x0100, 0x27, 0)
                    deadline = time.monotonic() + 30
                    while not user.IsWindowEnabled(user.GetDlgItem(window, 3008)) and time.monotonic() < deadline:
                        time.sleep(.02)
                    user.PostMessageW(window, 0x0111, 3008, 0)
                    picker = wait_window(process, 'Install a mod from file')
                    field = user.GetDlgItem(picker, 1148)
                    filename = ctypes.create_unicode_buffer(str(archive))
                    user.SendMessageW(field, 0x000c, 0, ctypes.addressof(filename))
                    user.PostMessageW(picker, 0x0111, 1, 0)
                    registry = local_data / 'mods/installed.toml'
                    rows = None
                    while time.monotonic() < deadline:
                        try:
                            rows = tomllib.loads(registry.read_text())['mods']
                            break
                        except (FileNotFoundError, PermissionError):
                            pass
                        time.sleep(.02)
                    assert rows is not None, 'Choosing a local mod must install it without a confirmation popup'
                    assert len(rows) == 1 and rows[0]['id'] == 'LocalMod' and rows[0]['enabled']
                    assert (local_data / 'mods/packages/LocalMod/files/local.bin').read_bytes() == payload
                    user.PostMessageW(window, 0x0010, 0, 0)
                    assert process.wait(timeout=10) == 0
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
                    process.close()
            # Installed languages must remain available when the live catalog changes.
            catalog_data = root / 'new-language'
            (catalog_data / 'cache').mkdir(parents=True)
            catalog_mod = dict(id='LanguagePack_JA', name='Japanese translation', summary='Synthetic catalog entry',
                               authors=['Tester'], type='language', language='ja', language_name='Japanese',
                               install_dir='packages/LanguagePack_JA', versions=[dict(version='1.0.0', recomp='0.1.0', size=1,
                               sha256='0' * 64, url='https://github.com/OAleex/ShaolinMonksRecompKrypt/releases/download/test/test.tar.xz')])
            (catalog_data / 'cache/krypt.json').write_text(json.dumps(dict(schema=1,
                game={'xbe_sha256': '48240902bf4a04eb89376217c529531032d3370749bcce91a194878cc49db74a'}, mods=[catalog_mod])))
            local_pack = catalog_data / 'mods/packages/LanguagePack_JA'
            local_pack.mkdir(parents=True)
            (local_pack / 'README.txt').write_bytes(b'test')
            local_metadata = dict(catalog_mod, schema=2, version='1.0.0', recomp='0.2.0', patches=[],
                game={'xbe_sha256': '48240902bf4a04eb89376217c529531032d3370749bcce91a194878cc49db74a'},
                files=[dict(path='README.txt', size=4, sha256=hashlib.sha256(b'test').hexdigest())])
            (catalog_data / 'mods/installed.toml').write_text('[[mods]]\nid = "LanguagePack_JA"\nversion = "1.0.0"\nenabled = false\nmanifest = '
                + json.dumps(json.dumps(local_metadata)) + '\n')
            with (root / 'catalog-language.log').open('wb') as output:
                process = PrivateProcess([str(args.exe.resolve()), '--configure', '--data-dir', str(catalog_data)], desktop_name, output, root)
                try:
                    window = wait_window(process, 'Shaolin Monks Recomp - Settings')
                    combo = user.GetDlgItem(window, 2101)
                    deadline = time.monotonic() + 10
                    while user.SendMessageW(combo, 0x0146, 0, 0) < 7 and time.monotonic() < deadline:
                        time.sleep(.05)
                    count = user.SendMessageW(combo, 0x0146, 0, 0)
                    label = ctypes.create_unicode_buffer(128)
                    selected = None
                    for index in range(count):
                        user.SendMessageW(combo, 0x0148, index, ctypes.addressof(label))
                        if label.value == 'Japanese': selected = index
                    assert selected is not None, 'An installed language must remain selectable after catalog refresh'
                    user.SendMessageW(combo, 0x014e, selected, 0)
                    user.PostMessageW(window, 0x0111, (1 << 16) | 2101, combo)
                    question = wait_window(process, 'LanguagePack required')
                    user.PostMessageW(question, 0x0111, 7, 0)
                    deadline = time.monotonic() + 3
                    while user.IsWindow(question) and time.monotonic() < deadline:
                        time.sleep(.02)
                    user.PostMessageW(window, 0x0111, 2201, 0)
                    assert process.wait(timeout=15) == 0
                    assert tomllib.loads((catalog_data / 'settings.toml').read_text())['language'] == 'ja'
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
                    process.close()
            # Catch a missing selection handler, lost language choice, or a download
            # incorrectly attempted for an installed but disabled language pack.
            for installed in (False, True):
                flow_data = root / ('disabled-pack' if installed else 'uninstalled-pack')
                flow_data.mkdir()
                flow_settings = flow_data / 'settings.toml'
                flow_settings.write_text('language = "en"\n')
                registry = flow_data / 'mods/installed.toml'
                marker = flow_data / 'mods/languages/LanguagePack_PT-BR/notes/keep.txt'
                if installed:
                    marker.parent.mkdir(parents=True)
                    marker.write_bytes(b'keep installed files')
                    manifest = dict(schema=1, id='LanguagePack_PT-BR', name='Test LanguagePack', summary='Synthetic installed pack',
                                    authors=['Tester'], type='language', language='pt-BR', install_dir='languages/LanguagePack_PT-BR',
                                    version='1.0.0', recomp='0.1.0',
                                    game={'xbe_sha256': '48240902bf4a04eb89376217c529531032d3370749bcce91a194878cc49db74a'},
                                    files=[dict(path='notes/keep.txt', size=20, sha256=hashlib.sha256(marker.read_bytes()).hexdigest())],
                                    patches=[])
                    registry.write_text('[[mods]]\nid = "LanguagePack_PT-BR"\nversion = "1.0.0"\nenabled = false\nmanifest = '
                                        + json.dumps(json.dumps(manifest)) + '\n')
                with (root / ('enable-language.log' if installed else 'decline-language.log')).open('wb') as output:
                    process = PrivateProcess([str(args.exe.resolve()), '--configure', '--data-dir', str(flow_data)], desktop_name, output, root)
                    try:
                        window = wait_window(process, 'Shaolin Monks Recomp - Settings')
                        combo = user.GetDlgItem(window, 2101)
                        user.SendMessageW(combo, 0x014e, 5, 0)
                        user.PostMessageW(window, 0x0111, (1 << 16) | 2101, combo)
                        question = wait_window(process, 'LanguagePack required')
                        user.PostMessageW(question, 0x0111, 6 if installed else 7, 0)
                        deadline = time.monotonic() + 15
                        while user.IsWindow(question) and time.monotonic() < deadline:
                            time.sleep(.02)
                        assert not user.IsWindow(question), 'The language question must close'
                        tabs = user.GetDlgItem(window, 2204)
                        if installed:
                            assert user.SendMessageW(tabs, 0x130b, 0, 0) == 6, 'Accepting must open the Mods tab'
                            progress = user.GetDlgItem(window, 3012)
                            assert progress and user.IsWindowVisible(progress), 'Mods must show the installation progress bar'
                            kind = ctypes.create_unicode_buffer(64)
                            user.GetClassNameW(progress, kind, len(kind))
                            assert kind.value.lower() == 'msctls_progress32', 'Progress must use the native progress control'
                            rows = []
                            while time.monotonic() < deadline:
                                try:
                                    rows = tomllib.loads(registry.read_text())['mods']
                                except (FileNotFoundError, PermissionError):
                                    time.sleep(.02)
                                    continue
                                if rows[0]['enabled']:
                                    break
                                time.sleep(.05)
                            assert rows and rows[0]['enabled'], 'Accepting must enable the installed pack without another install click'
                            assert marker.read_bytes() == b'keep installed files', 'Enabling must preserve installed content'
                            assert user.SendMessageW(combo, 0x0147, 0, 0) == 5, 'The requested language must stay selected'
                            enabled = ctypes.create_unicode_buffer(64)
                            while time.monotonic() < deadline:
                                user.GetWindowTextW(user.GetDlgItem(window, 3004), enabled, len(enabled))
                                if enabled.value == 'Disable':
                                    break
                                time.sleep(.05)
                            assert enabled.value == 'Disable', 'Mods must show that the pack is enabled'
                            # Let the tab control send its own notification inside
                            # the application process, without a cross-process pointer.
                            for _ in range(6):
                                user.SendMessageW(tabs, 0x0100, 0x25, 0)  # WM_KEYDOWN, VK_LEFT
                            assert user.SendMessageW(tabs, 0x130b, 0, 0) == 0
                            assert user.IsWindowVisible(combo), 'The user must be able to return to the language selector'
                        else:
                            assert user.SendMessageW(tabs, 0x130b, 0, 0) == 0, 'Declining must keep the General tab open'
                            assert not registry.exists(), 'Declining must not install or enable a mod'
                            user.SendMessageW(combo, 0x014e, 1, 0)
                        user.PostMessageW(window, 0x0111, 2201, 0)
                        assert process.wait(timeout=15) == 0
                        assert tomllib.loads(flow_settings.read_text())['language'] == ('pt-BR' if installed else 'fr')
                    finally:
                        if process.poll() is None:
                            process.terminate()
                            process.wait(timeout=5)
                        process.close()
            with (root / 'temporary-session.log').open('wb') as output:
                process = PrivateProcess([str(args.exe.resolve()), '--data-dir', str(data), '--disc', str(root / 'missing.iso')], desktop_name, output, root)
                try:
                    window = wait_window(process, 'Shaolin Monks Recomp - Settings')
                    user.SendMessageW(user.GetDlgItem(window, 2101), 0x014e, 2, 0)
                    user.PostMessageW(window, 0x0111, 2202, 0)
                    question = wait_window(process, 'Unsaved settings')
                    user.PostMessageW(question, 0x0111, 2, 0)
                    time.sleep(.15)
                    assert process.poll() is None and settings.read_bytes() == saved
                    user.PostMessageW(window, 0x0111, 2202, 0)
                    question = wait_window(process, 'Unsaved settings')
                    user.PostMessageW(question, 0x0111, 7, 0)
                    error = wait_window(process, 'Shaolin Monks Recomp')
                    assert settings.read_bytes() == saved, 'Play without saving must preserve the file'
                    time.sleep(.3)
                    user.PostMessageW(error, 0x0010, 0, 0)
                    assert process.wait(timeout=10) == 3
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
                    process.close()
            with (root / 'missing-language-pack.log').open('wb') as output:
                missing_data = root / 'missing-data'
                missing_data.mkdir()
                (missing_data / 'settings.toml').write_text('language = "pt-BR"\n')
                process = PrivateProcess([str(args.exe.resolve()), '--data-dir', str(missing_data),
                                          '--disc', str(root / 'missing.iso')], desktop_name, output, root)
                try:
                    window = wait_window(process, 'Shaolin Monks Recomp - Settings')
                    user.PostMessageW(window, 0x0111, 2202, 0)
                    warning = wait_window(process, 'LanguagePack required')
                    user.PostMessageW(warning, 0x0111, 7, 0)
                    deadline = time.monotonic() + 3
                    while user.IsWindow(warning) and time.monotonic() < deadline:
                        time.sleep(.02)
                    assert not user.IsWindow(warning), 'The warning must close before the launcher receives Close'
                    assert process.poll() is None, 'Play must remain in the launcher without an active LanguagePack'
                    user.PostMessageW(window, 0x0010, 0, 0)
                    assert process.wait(timeout=10) == 0
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
                    process.close()
            assert 'Error dialog closed. Application data folder: ' + str(data) in (
                root / 'temporary-session.log').read_text(encoding='utf-8', errors='replace')
            assert settings.read_bytes() == saved
            pack = root / 'LanguagePack_PT-BR'
            pack.mkdir()
            with (root / 'language-pack.log').open('wb') as output:
                process = PrivateProcess([str(args.exe.resolve()), '--configure', '--data-dir', str(data),
                                          '--language-pack', str(pack)], desktop_name, output, root)
                try:
                    window = wait_window(process, 'Shaolin Monks Recomp - Settings')
                    combo = user.GetDlgItem(window, 2101)
                    assert user.SendMessageW(combo, 0x0146, 0, 0) >= 6
                    label = ctypes.create_unicode_buffer(128)
                    user.SendMessageW(combo, 0x0148, 5, ctypes.addressof(label))
                    assert label.value == 'Brazilian Portuguese'
                    user.SendMessageW(combo, 0x014e, 5, 0)
                    assert user.SendMessageW(combo, 0x0147, 0, 0) == 5
                    user.PostMessageW(window, 0x0111, 2201, 0)
                    assert process.wait(timeout=10) == 0
                    config = tomllib.loads(settings.read_text())
                    assert config['language'] == 'pt-BR'
                    assert config['custom_key'] == 'preserve'
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
                    process.close()
    finally:
        user.SetThreadDesktop(previous)
        user.CloseDesktop(desktop)
    print('PASS: launcher settings, fixed 60 FPS without a selector, direct local mod installation, language packs, and post-error folder handling')


if __name__ == '__main__':
    main()
