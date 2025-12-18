# 1term
A simple terminal emulator built with GTK 4 and VTE.
<img width="954" height="215" alt="image" src="https://github.com/user-attachments/assets/e3cb4568-622f-4e8d-b956-2edfdc24510d" />

## Features

- Tabbed interface
- Copy-on-select + clipboard integration
- Toggleable transparency
- Toggleable scrollback
- Scrollback compression to log files

## Installation / Usage

1term is currently used by building from source.

### Dependencies

You need GTK 4, VTE (GTK 4 build), Meson/Ninja, and a C compiler.

- Debian/Ubuntu: `sudo apt install meson gcc pkg-config libgtk-4-dev libvte-2.91-gtk4-dev libzstd-dev`
- Fedora: `sudo dnf install meson gcc pkg-config gtk4-devel vte291-gtk4-devel zstd-devel`
- Arch: `sudo pacman -S meson gcc pkg-config gtk4 vte4 zstd`

### Build and Run

```bash
meson setup build
meson compile -C build
./build/1term
```

To install system-wide (optional): `sudo meson install -C build`

### Keybindings

All keybindings use `Ctrl+Shift` as modifiers.

| Key | Action |
|-----|--------|
| `C` | Copy |
| `V` | Paste |
| `A` | Select all + copy |
| `B` | Compress scrollback to `~/.1term/logs/terminal_YYYYMMDD_HHMMSS_D.logz` |
| `T` | Toggle transparency |
| `S` | Toggle scrollback |
| `N` | New tab |
| `W` | Close tab |

Developer docs: `HACKING.md` (how to build/modify) and `DESIGN.md` (how it works internally).

## License

TBD (no `LICENSE` file is present in this repository).
