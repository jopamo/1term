# HACKING.md — guidelines for contributing / understanding

## 📦 Overview  

This project embeds a VTE terminal widget inside a GTK 4 application, potentially leveraging GPU-accelerated rendering (via GTK 4’s rendering back-end) to improve rendering performance, scroll/render latency, reduce CPU load, and make the terminal responsive even under heavy output or high-refresh displays  

GTK 4 + VTE (GTK4-flavor) is supported: the library provides `libvte-2.91-gtk4` / `libvte-2.91` for GTK-4 builds.

Recent upstream work on VTE focused on performance: patches have cut wall-clock time for common operations by ≈ 40%, and future work aims to replace Cairo-based draw-paths with native GTK4 drawing primitives.  

The GTK 4 renderer (e.g. via its GL/Native back-end) allows delegating much of the drawing work to GPU rather than CPU/software — this underpins GPU-acceleration for the terminal UI.

## 🛠 Build / Dependencies  

- Depend on `vte-2.91-gtk4` (or whichever version your distro provides) rather than legacy GTK3-based VTE, if you want GTK4 integration
- Ensure your GTK 4 version is recent enough: while GTK-4.12 might compile, GTK 4.14 (or later) is recommended/stable for the “new NGL renderer” to avoid rendering failures/bugs seen in earlier GL back-ends.
- Build using meson/ninja (or your distro’s build system), enabling `gtk4 = true` in VTE’s configuration if building from source.

## 📄 Hacking & Code Style Guidelines  

- Prefer to use native GTK4 drawing primitives rather than Cairo where possible (once VTE’s GTK4 native drawing is mature) — this reduces draw latency and maximizes benefit from GPU acceleration.
- If you add custom drawing (e.g. overlays, decorations, custom glyph rendering), make it compatible with the GTK 4 frame clock (i.e. rendering operations should be synced to the frame clock rather than arbitrary timeouts). This matches upstream’s direction to drive draw timing from the frame clock.
- Handle high-throughput output (e.g. flood of stdout, large scrollback, rapid redraw) carefully — since terminal emulators are often used in heavy-output scenarios, ensure your code avoids blocking, batches redraws, and doesn’t force full redraws when only partial updates suffice  
- Respect font scaling / DPI / HiDPI: using GTK + VTE gives you automatic support for DPI changes (font scaling, window scaling) so users don’t need to restart or manually reconfigure on display DPI changes.

## 🚀 GPU-Acceleration & Performance Best Practices  

Because GTK4 + VTE now supports GPU offloaded rendering / frame-clock-driven drawing, you should:  

- Ensure rendering path uses the GTK 4 GL / native renderer rather than falling back to software (Cairo) when possible (i.e. detect if GL-renderer is available and enabled)  
- Use double-buffering / frame-clock aware redraws to avoid tearing or flicker  
- Batch small updates (e.g. cursor blink, prompt redraw) instead of forcing full widget redraws every tiny change  
- For scrollback buffer and history management, ensure buffer compression / memory management is efficient: recently VTE changed its scrollback compression from zlib to a more performant algorithm.
- Minimize unnecessary allocations, recomputations, or rendering of invisible/off-screen regions — only draw what the user sees  

## 🧪 Running / Debugging / Profiling  

- Use a profiler (e.g. `perf`, or tools equivalent on your platform) to measure rendering paths and identify bottlenecks (draw latency, blocking I/O, buffer flushes) — this is especially useful as GTK 4 / VTE transitions to native drawing paths. This method was used in upstream VTE performance tuning.
- Test under heavy output: long logs, rapid continuous scroll, mixed Unicode / wide glyphs, high-refresh redraws — ensure none of these regress performance or rendering correctness  
- Validate under different GPUs / drivers / display back-ends (X11, Wayland) — GPU-acceleration depends on proper GL/native renderer support  
- Monitor resource usage (CPU load, GPU load, memory for scrollback) to detect regressions  

## ✅ Design / UX / Features Guidance  

- Keep terminal behavior predictable: respect escape sequences, width/height, resizing, scrollback, scrollbars or scroll-back via history, copy/paste, Unicode/UTF-8, wide glyphs, combining characters, etc.  
- Allow theming / font customization / padding / margins / custom CSS or style settings — but ensure these do not break performance or force excessive redraws. For example some pre-GTK4 terminals used CSS for padding; for GTK4, ensure styling is efficient and doesn’t degrade GPU-backed rendering. (see how other GTK-4 terminals handled padding/styling)
- Provide sane defaults but allow advanced users to configure performance- vs aesthetics tradeoffs (e.g. enabling/disabling GPU rendering, scrollback buffer size, async draw batching, fallback to software rendering for compatibility)  

## 🧑‍💻 Developer Workflow / Contribution Guidelines  

- When you write patches/changes: test under *both* small load (simple shell, few lines) and heavy load (flood stdout, log dumps)  
- Document any fallback paths (e.g. if GL-renderer fails, fallback to software) — making sure to note limitations (performance degradation, rendering artifacts)  
- Write clear code and avoid duplication: reuse VTE API where possible instead of reimplementing features already provided (pty spawning, scrollback, escape-parsing) — VTE is well-tested and covers a broad set of terminal features.
- Add tests where appropriate: if you add drawing or performance-sensitive code, test Unicode rendering, wide-glyphs, resizing, buffer overflow, stress tests  
