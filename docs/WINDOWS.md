# RavenSpot on Windows: hands-on checklist

The Windows build compiles on CI and passes the C++ and QML unit tests there,
plus a daemon smoke test (start, `status`, `show`, `hide`, `quit`). Everything
below needs a real Windows 10/11 desktop and has **not** been checked yet.

Run the build with logging so problems show up in a console:

```powershell
$env:QT_FORCE_STDERR_LOGGING = "1"
.\ravenspot.exe --daemon
```

## Window, hotkey, focus
- [ ] `Alt+Space` opens and closes the menu. If another tool already owns it,
      `Ctrl+Alt+Space` is used. `RAVENSPOT_HOTKEY=win+shift+space` overrides.
- [ ] The menu takes keyboard focus right away when opened by the hotkey and by
      `ravenspot.exe toggle` from another console (foreground-lock workaround).
- [ ] Clicking another window closes the menu; the right-click menu does not.
- [ ] Transparent corners and glow look right. Qt uses Direct3D 11 here; if the
      background is black, try `QSG_RHI_BACKEND=opengl`.
- [ ] Optional backdrop: `RAVENSPOT_WIN_BACKDROP=acrylic` / `mica` (Windows 11).
      It may cover the whole card rectangle; it stays off by default.
- [ ] Dragging the title bar moves the window smoothly (native move loop) and
      the next open keeps that position; a different monitor re-centres.
- [ ] Multi-monitor / mixed DPI: opens centred on the monitor under the cursor.

## Apps
- [ ] All Apps lists Start Menu shortcuts (user + all users) with icons;
      uninstallers/readmes are hidden.
- [ ] Store apps (Calculator, Settings, Photos…) appear after ~1–2 s
      (Get-StartApps) with icons.
- [ ] Launching works; launching an app that is already open focuses it.
- [ ] Categories look sensible (folder/name heuristic in `AppIndexer_win.cpp`).
- [ ] Right-click: Show in Explorer, Properties, Run at startup (Startup folder),
      Uninstall (opens Settings → Installed apps).

## Files, folders, clipboard
- [ ] File search needs `fd` on PATH (`winget install sharkdp.fd`); without it
      the Files results stay empty.
- [ ] Open / Open containing folder / Recycle Bin / Rename / Properties.
- [ ] Extract here / Compress (Windows `tar.exe`).
- [ ] Open with → system "Open with" dialog.
- [ ] Recent files come from `%APPDATA%\Microsoft\Windows\Recent`.
- [ ] Clipboard history fills while RavenSpot runs (no history from before).

## Commands, terminal, links
- [ ] `>` commands run in Windows Terminal (or a PowerShell console) and stay open.
- [ ] Shift+Enter inline commands run in PowerShell; output is UTF-8 (try
      `Write-Output 'ğüşiöç'`); Stop ends child processes too.
- [ ] Paste a YouTube link + Enter → prompt. "Open in default browser" works.
      "Download the video" with no yt-dlp → install prompt (winget). After the
      install, a new paste downloads into Downloads with live progress.
      Check that yt-dlp's console output is readable (encoding) and that a
      missing ffmpeg is reported clearly (yt-dlp needs it to merge formats).
- [ ] Menu entries written for Linux (`menu.jsonc` actions using bash/KDE
      tools) either work through Git Bash or should be hidden on Windows.
      Menu guards run only when Git for Windows' `bash.exe` is installed.

## System
- [ ] Volume slider and mute change the default playback device.
- [ ] Power: Lock, Sleep, Sign out, Restart, Shut down (no confirmation, same as Linux).
- [ ] "Restart Plasma" restarts Explorer instead.
- [ ] Power profile buttons switch Windows power plans (on Modern Standby
      laptops usually only "Balanced" exists, so nothing changes).
- [ ] "Block sleep" keeps the PC and screen awake.
- [ ] CPU meter, RAM, disk, uptime show values; GPU % and temperature show 0/--.
- [ ] Task Manager opens for the CPU/GPU tiles; the update badge opens Windows Update.
- [ ] Sounds play (Qt Multimedia, `sounds/` next to the exe).

## Known gaps (not ported)
- GPU load and CPU temperature (need vendor APIs/WMI).
- KDE-only features: colour schemes, Discover, session save, Nashi.
- Windows Update pending count.
- A command's child process could escape the Job object if it starts in the
  first instant before the job is assigned (acceptable for interactive use).
