# HACKING

This is the contributor and agent guide for modifying 1term.

Architecture and internal system details live in `DESIGN.md`.

## Development Setup

### Dependencies

To build and run 1term, you need:

- GTK 4 (>= 4.14 recommended)
- VTE (>= 0.70, GTK 4 build: `vte-2.91-gtk4`)
- libzstd (`libzstd`)
- Meson + Ninja
- A C compiler (GCC or Clang)
- `clang-format` (formatting)

Example packages:

- Debian/Ubuntu: `sudo apt install meson ninja-build gcc pkg-config libgtk-4-dev libvte-2.91-gtk4-dev libzstd-dev clang-format`
- Fedora: `sudo dnf install meson ninja-build gcc pkg-config gtk4-devel vte291-gtk4-devel zstd-devel clang`
- Arch: `sudo pacman -S meson ninja gcc pkg-config gtk4 vte4 zstd clang`

## Build and Test

Configure and build:

```bash
meson setup build
meson compile -C build
```

Run:

```bash
./build/1term
```

Tests:

```bash
meson test -C build
```

Useful variants:

- Reconfigure from scratch: `meson setup build --wipe`
- Debug build: `meson setup build-debug -Dbuildtype=debug && meson compile -C build-debug`
- ASan build:
  - `meson setup build-asan -Dbuildtype=debug -Db_sanitize=address`
  - Run with suppressions: `ASAN_OPTIONS=suppressions=$(pwd)/asan.supp ./build-asan/1term`

## Coding Standards

- Language: C (project sets `c_std=c2x` in Meson).
- Formatting: run `clang-format` on changed C/H files (uses `.clang-format` in the repo root).
  - Example: `clang-format -i src/*.c src/*.h`
- Performance and rendering:
  - Avoid blocking the GTK main thread (offload heavy work to background threads).
  - Prefer native GTK 4 drawing primitives over Cairo to keep rendering GPU-backed.
  - If adding custom drawing, sync to the GTK frame clock and avoid unnecessary allocations/redraws.
- Terminal correctness:
  - Preserve expected terminal behavior (escape sequences, resizing, Unicode).
  - Be careful with styling (CSS/padding) that can degrade terminal rendering performance.

## Contribution Flow

1. Create a branch (or prepare a patch).
2. Make the change with minimal scope.
3. Run `clang-format` on touched C/H files.
4. Build and run tests: `meson compile -C build && meson test -C build`
5. Do a quick manual run and stress test (e.g., flood output in a tab, open/close tabs, toggle scrollback/transparency).
6. Submit a PR/patch with a clear description and any performance notes if applicable.
