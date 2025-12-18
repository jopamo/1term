# 1term - Hacking Guide

1term is a GPU-accelerated VTE terminal widget embedded in a GTK 4 application.

## Overview

This project embeds a VTE terminal widget inside a GTK 4 application, leveraging GPU-accelerated rendering (via GTK 4’s rendering back-end) to improve rendering performance, scroll/render latency, reduce CPU load, and make the terminal responsive even under heavy output or high-refresh displays.

GTK 4 + VTE (GTK4-flavor) is supported: the library provides `libvte-2.91-gtk4` / `libvte-2.91` for GTK-4 builds.

## Development Environment

### Dependencies

To build and run 1term, you need:

- **GTK 4**: Version 4.14 or later is recommended for the stable "new NGL renderer".
- **VTE**: Version ≥0.70 with GTK 4 support (`vte-2.91-gtk4`).
- **Build System**: `meson` and `ninja`.
- **Compiler**: A C compiler (GCC or Clang).

### Building and Running

1.  **Configure the build:**
    ```bash
    meson setup build
    ```

2.  **Build the project:**
    ```bash
    meson compile -C build
    ```

3.  **Run the application:**
    ```bash
    ./build/src/1term
    ```

4.  **Run tests:**
    ```bash
    meson test -C build
    ```

## Coding Guidelines

### Language and Style

- **Language**: The project is written in **C**.
- **Formatting**: We use `clang-format`. Please run the formatter before submitting changes to ensure consistent style.

### Hacking & Architecture

- **GPU Acceleration**: Prefer native GTK 4 drawing primitives over Cairo where possible to maximize GPU acceleration.
- **Frame Clock**: Sync custom drawing operations to the GTK 4 frame clock.
- **Performance**:
    - Avoid blocking the main thread.
    - Batch redraws for high-throughput output.
    - Use double-buffering / frame-clock aware redraws.
    - Minimize unnecessary allocations and rendering of invisible regions.
- **Design**:
    - Respect standard terminal behavior (escape sequences, resizing, Unicode).
    - Ensure styling (CSS/padding) does not degrade GPU-backed rendering performance.

## Contributing

### Proposing Changes

1.  Fork the repository and create a feature branch.
2.  Make your changes, ensuring they follow the coding guidelines.
3.  Test your changes under both light and heavy load (e.g., flood stdout).
4.  Run the test suite to ensure no regressions.
5.  Submit a pull request (or patch) with a clear description of the changes.

### Testing

Before submitting, please run the full test suite:

```bash
meson test -C build
```

Additionally, validate performance using a profiler (e.g., `perf`) if modifying rendering paths, and test on different backends (X11, Wayland) if possible.