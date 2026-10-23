"""Check the first-run setup on a private desktop, without opening the game."""
import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import tempfile
import time
import tomllib
from recomp_error_dialog_test import PrivateProcess
from recomp_game_fixture import game_files, write_game_folder


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--xbe', type=Path)
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
    user.EnumChildWindows.argtypes = [wintypes.HWND, callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
    user.GetDlgItem.restype = wintypes.HWND
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    name = 'ShaolinSetupTest' + str(time.time_ns())
    desktop = user.CreateDesktopW(name, None, None, 0, 0x01ff, None)
    assert desktop, ctypes.get_last_error()
    previous = user.GetThreadDesktop(kernel.GetCurrentThreadId())
    assert user.SetThreadDesktop(desktop), ctypes.get_last_error()
    try:
        with tempfile.TemporaryDirectory(prefix='shaolin-setup-dialog-') as directory:
            root = Path(directory)
            with (root / 'process.log').open('wb') as output:
                process = PrivateProcess([str(args.exe.resolve()), '--data-dir', str(root / 'data')], name, output, root)
                dialogs, launchers, texts = [], [], set()

                def collect(window, _):
                    text = ctypes.create_unicode_buffer(8192)
                    user.GetWindowTextW(window, text, len(text))
                    texts.add(text.value)
                    return True

                child_callback = callback_type(collect)

                def observe(window, _):
                    pid = wintypes.DWORD()
                    user.GetWindowThreadProcessId(window, ctypes.byref(pid))
                    if pid.value == process.pid:
                        title = ctypes.create_unicode_buffer(256)
                        user.GetWindowTextW(window, title, len(title))
                        if title.value == 'Shaolin Monks Recomp - Settings':
                            launchers.append(window)
                        if title.value == 'Shaolin Monks Recomp':
                            user.EnumChildWindows(window, child_callback, 0)
                            if any('Choose a game file' in text for text in texts):
                                dialogs.append(window)
                    return True

                observer = callback_type(observe)
                try:
                    deadline = time.monotonic() + 20
                    while process.poll() is None and time.monotonic() < deadline and not dialogs and not launchers:
                        user.EnumDesktopWindows(desktop, observer, 0)
                        time.sleep(.05)
                    assert not launchers, 'First launch must select game files before opening settings'
                    assert dialogs, ('The integrated setup did not open', texts,
                                     (root / 'process.log').read_text(encoding='utf-8', errors='replace')[-3000:])
                    assert any('Choose an extracted game folder' in text for text in texts), texts
                    user.PostMessageW(dialogs[0], 0x0010, 0, 0)
                    process.wait(timeout=10)
                    assert not (root / 'data/game-source.toml').exists()
                    assert not (root / 'data/game').exists()
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
                    process.close()
            if args.xbe:
                game = root / 'game files'
                game.mkdir()
                write_game_folder(game, game_files(args.xbe.read_bytes()))

                def wait_window(process, expected):
                    windows = []

                    def find(window, _):
                        pid = wintypes.DWORD()
                        user.GetWindowThreadProcessId(window, ctypes.byref(pid))
                        title = ctypes.create_unicode_buffer(512)
                        user.GetWindowTextW(window, title, len(title))
                        if pid.value == process.pid and title.value == expected and user.IsWindowVisible(window):
                            windows.append(window)
                        return True

                    callback = callback_type(find)
                    deadline = time.monotonic() + 15
                    while process.poll() is None and time.monotonic() < deadline and not windows:
                        user.EnumDesktopWindows(desktop, callback, 0)
                        time.sleep(.05)
                    assert windows, 'Expected startup window: ' + expected
                    return windows[0]

                for stage in ('select', 'remembered', 'setup', 'unavailable'):
                    if stage == 'unavailable':
                        game.rename(root / 'moved game files')
                    with (root / f'{stage}.log').open('wb') as output:
                        command = [str(args.exe.resolve()), '--data-dir', str(root / 'data')]
                        if stage == 'setup':
                            command.append('--setup')
                        process = PrivateProcess(command, name, output, root)
                        try:
                            if stage == 'select':
                                dialog = wait_window(process, 'Shaolin Monks Recomp')
                                assert user.PostMessageW(dialog, 0x0466, 100, 0)  # TDM_CLICK_BUTTON
                                picker = wait_window(process, 'Shaolin Monks Recomp - Select your game')
                                field = user.GetDlgItem(picker, 1148)
                                assert field, 'The file chooser must expose its filename field'
                                source = ctypes.create_unicode_buffer(str(game / 'default.xbe'))
                                assert user.SendMessageW(field, 0x000c, 0, ctypes.addressof(source))
                                assert user.PostMessageW(picker, 0x0111, 1, 0)
                                launcher = wait_window(process, 'Shaolin Monks Recomp - Settings')
                                selection = root / 'data/game-source.toml'
                                assert Path(tomllib.loads(selection.read_text())['disc']) == game
                                remembered = selection.read_bytes()
                                assert not (root / 'data/settings.toml').exists()
                                assert user.PostMessageW(launcher, 0x0010, 0, 0)
                            elif stage == 'remembered':
                                launcher = wait_window(process, 'Shaolin Monks Recomp - Settings')
                                assert user.PostMessageW(launcher, 0x0010, 0, 0)
                            else:
                                dialog = wait_window(process, 'Shaolin Monks Recomp')
                                assert user.PostMessageW(dialog, 0x0010, 0, 0)
                            process.wait(timeout=10)
                            assert (root / 'data/game-source.toml').read_bytes() == remembered
                            assert not (root / 'data/settings.toml').exists()
                        finally:
                            if process.poll() is None:
                                process.terminate()
                                process.wait(timeout=5)
                            process.close()
    finally:
        user.SetThreadDesktop(previous)
        user.CloseDesktop(desktop)
    print('PASS: source selection precedes settings; valid selections are remembered, missing sources reopen setup, and cancellation preserves data')


if __name__ == '__main__':
    main()
