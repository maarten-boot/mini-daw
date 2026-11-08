# mini-daw demo checklist

For a short (about 5 minute) demo on Windows 10. Build: the CI download of run 42421955192 (commit `0309fed`, P19 + crosscheck #14 fixes; the same build as in `dist/windows`): `mini-daw-0.1.0-setup.exe`, `mini-daw.exe`, `mini-daw.com`.

## 1. The day before: prepare the machine

- [ ] Install with `mini-daw-0.1.0-setup.exe` (per user, no admin rights). Start it once from the Start menu.
- [ ] **Settings → Audio devices…**: the output (speakers or headphones) and the input (Line In or the audio interface). Check that the **Output** meter (top line of the Recording tab; also on the Audio setup tab) moves when something plays.
- [ ] **Settings → Instruments (VST®3)… → Scan**: the Arturia instruments appear in the list. (Plugins in another folder: **Browse…**, choose that folder, then Scan.) A module that fails or crashes is listed with the reason and skipped from then on.
- [ ] Plug in the MIDI keyboard. Add a MIDI track and arm it: its "messages / receiving" count moves when you play.
- [ ] Text size (**Ctrl+=** / **Ctrl+−**) and window size: the whole track table must be visible, including the **Take** column. Maximise the window.
- [ ] Close other audio programs (DAWs, browsers playing sound). Use **headphones**: they avoid feedback and hide the faint pulsing noise on the input.

## 2. Rehearse once (this is also test T-VST-20 and T-VST-30)

Note anything that goes wrong. **View → Log (Ctrl+L)** shows what mini-daw did; the log file is `mini-daw.log` in `%APPDATA%\mini-daw`.

1. [ ] **File → New project…**, name it "Demo".
2. [ ] **Add MIDI track**, choose the keyboard as input, and choose an Arturia instrument in the output list. A track "… sound" appears right below it at once (silent: "Sound from: instrument").
3. [ ] **Edit**: the instrument's own window opens above mini-daw. Pick a preset and play: you hear it at once. Close the window; **Edit** opens it again with the same preset.
4. [ ] **Arm** the track, then **Record (Ctrl+R)**, play a few bars, then **Stop recording (Ctrl+R)**. The "… sound" track now holds the instrument's recording.
5. [ ] **Home**, then **Play (Space)**: you hear what you played.
6. [ ] **Render**: the percentage runs, then the "… sound" track gets its `-render.wav`.
7. [ ] Switch **Sound from** to "this recording", then play: it sounds the same, and only once (not doubled). Switch it back.
8. [ ] Overdub: **disarm** the MIDI track, **Add audio track** with the input, tick **Monitor** and **Arm** it. The line under the transport says Record also plays the other track. **Home**, **Record**: play along with the MIDI track; **Stop recording**, **Home**, **Play**: both play.
9. [ ] Quit mini-daw while the instrument window is open, then start it again: the "Demo" project comes back with the instrument, its preset and the takes.

## 3. The demo (about 5 minutes)

| Step | Show | Say |
|------|------|-----|
| 1 | Start mini-daw: the last project opens | Small and focused: record MIDI and audio, play back, VST®3 instruments |
| 2 | MIDI track → Arturia instrument → **Edit** → preset → play | The plugin's own window; plays live with low delay |
| 3 | Arm → **Record** → a few bars → Stop → **Play** | The MIDI and the instrument's sound are both recorded |
| 4 | **Render**, then **Sound from** | Renders faster than real time; you choose what plays, so nothing doubles |
| 5 | Audio track → Monitor → overdub → play both | Audio and MIDI on one timeline, the overdub lined up with what you heard |
| 6 | Volume, **M**ute, **S**olo; level meters with dBFS and CLIP | Mix basics; the levels are always readable as text too |
| 7 | **Ctrl+Shift+Up** (contrast), **Ctrl+=** (text size) | Readable for everyone: contrast presets up to pure black/white |

## 4. If something goes wrong

| Problem | What to do |
|---------|------------|
| No sound | Look at the **Output** meter: if it moves, check the speakers or headphones; if not, check **Settings → Audio devices…** and the track's **M** / volume |
| The keyboard does nothing | Is the track armed? If the Level column says "DISCONNECTED" or "CLOSED", plug the keyboard in again, or arm off and on |
| The plugin crashes mini-daw | Start it again: takes already on disk are safe, and the scan marks the plugin so it is skipped. Continue with another instrument |
| The instrument window does not appear | Click **Edit** again (the button reads **Close** while it is open); otherwise continue without it |
| Ticking or pulsing on the input | Use headphones, or show MIDI instruments only |
| The Take column is cut off | Maximise the window or press **Ctrl+−** |

## 5. Afterwards

Tell Claude what happened at each step in section 2. It goes into `docs/planning/Tests.md` as T-VST-20 and T-VST-30, along with the other Windows checks: playback, projects, the installer pages and Start menu.
