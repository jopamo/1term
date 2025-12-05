# Installation Guide

## Dependencies

To build the project, you need to install the following dependencies:

- `meson`
- `gcc` or `clang`
- `pkg-config`
- `libgtk-4-dev`
- `libvte-2.91-gtk4-dev`

### Debian/Ubuntu

```bash
sudo apt install meson gcc pkg-config libgtk-4-dev libvte-2.91-gtk4-dev
```

### Fedora

```bash
sudo dnf install meson gcc pkg-config gtk4-devel vte291-gtk4-devel
```

### Arch Linux

```bash
sudo pacman -S meson gcc pkg-config gtk4 vte4
```

## Building from Source

1. Clone the repository:

```bash
git clone https://github.com/jopamo/1term.git
cd 1term
```

2. Configure the build with Meson:

```bash
meson setup builddir --wipe
```

3. Compile the project:

```bash
meson compile -C builddir
```

4. Install system-wide (optional):

```bash
sudo meson install -C builddir
```

## Running

After building, you can run the terminal emulator directly from the build directory:

```bash
./builddir/1term
```

If installed system-wide, you can launch it from your application menu or run `1term` from the terminal.

## Uninstalling

To uninstall the system-wide installation:

```bash
sudo ninja -C builddir uninstall
```