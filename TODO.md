# TODO

## Bugs
- [ ] Stop scrollback compression from clobbering the user's clipboard
  - [ ] Replace clipboard-based capture in `src/clipboard.c` with direct VTE text extraction
  - [ ] Keep scrollback capture off the GTK main thread (invariant: no UI stalls)
  - [ ] Verify manually: clipboard contents unchanged after `Ctrl+Shift+B`

## Planned Features
- [ ] Implement built-in color schemes and apply them to new terminals
  - [ ] Add `src/colors.c` implementing `src/colors.h` (invariant: no unimplemented headers)
  - [ ] Apply scheme in `src/terminal.c:setup_terminal` (invariant: all terminals consistent)
  - [ ] Add a way to change schemes (keybinding or settings) (needs design decision)
  - [ ] Verify manually: palette/foreground/background change per scheme selection

- [ ] Create a settings dialog and persist preferences
  - [ ] Decide storage mechanism: GSettings vs config file (needs design decision)
  - [ ] Add UI in `src/window.c` and wire actions in `src/main.c`
  - [ ] Persist and reload: transparency, scrollback lines, font, color scheme
  - [ ] Verify manually: settings survive restart and apply to existing terminals

## Refactoring
- [ ] Remove unconditional stdout logging from normal operation
  - [ ] Replace `g_print()` debug traces with `g_debug()` in `src/tab.c` and `src/window.c`
  - [ ] Keep user-facing errors on stderr via GLib logging (`g_warning`, `g_critical`)
  - [ ] Verify manually: launching `./build/1term` emits no stdout in steady state

- [ ] Standardize error handling and cleanup paths
  - [ ] Consolidate repeated cleanup patterns in `src/clipboard.c` (invariant: no partial files)
  - [ ] Use a single error-reporting style across `src/terminal.c` and `src/clipboard.c`
  - [ ] Verify: build with `-Wall -Wextra` produces no new warnings (needs setup)

## Documentation
- [ ] Fix keybinding docs to match actual scope (all windows)
  - [ ] Update `docs/KEYBINDINGS.md` to reflect global transparency/scrollback toggles
  - [ ] Verify: docs match behavior in `src/window.c:update_*_all`

- [ ] Document CLI `--help`/`--version`
  - [ ] Update `README.md` to include the new flags
  - [ ] Verify: output matches `src/main.c:print_usage`

- [ ] Update dependency docs to match Meson build
  - [ ] Add `libzstd` to `docs/INSTALL.md` and ensure package lists match `meson.build`
  - [ ] Verify: fresh install instructions include all required -dev packages

- [ ] Refresh development docs around debugging/logging
  - [ ] Update `docs/DEVELOPMENT.md` (remove “uncomment g_print” guidance)
  - [ ] Document how to enable debug logs via `G_MESSAGES_DEBUG`
