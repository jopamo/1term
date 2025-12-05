# Development Guide

## Project Structure

```
src/
├── main.c              # Application entry point, GtkApplication setup
├── window.c/h          # Window class and window management
├── tab.c/h             # Tab management, notebook signals
├── terminal.c/h        # Terminal setup, keybindings, PTY spawning
├── clipboard.c/h       # Clipboard integration, scrollback compression
└── 1term.h            # Common includes and global declarations

assets/
├── icons/              # Application icons (PNG, SVG)
└── desktop/            # Desktop entry file

docs/                   # Documentation files
```

## Build System

The project uses Meson with Ninja backend. Key configuration in `meson.build`:

- C standard: C2x
- Optimization: -O3 with LTO
- Dependencies: GTK4, VTE, GLib, Zstd

## Adding New Features

1. Identify which module the feature belongs to:
   - UI changes → `window.c`
   - Terminal behavior → `terminal.c`
   - Tab management → `tab.c`
   - Clipboard/compression → `clipboard.c`

2. Add declarations to the corresponding header file.

3. Implement in the source file.

4. Update documentation if necessary.

## Debugging

Enable debug prints by uncommenting `g_print` statements throughout the code.

To run with address sanitizer:

```bash
meson setup build-asan -Db_sanitize=address
meson compile -C build-asan
```

## Testing

Currently no automated test suite. Manual testing involves running the terminal and verifying keybindings, tab creation, and scrollback compression.

## Contributing

1. Fork the repository.
2. Create a feature branch.
3. Ensure the project builds successfully.
4. Submit a pull request with a clear description of changes.