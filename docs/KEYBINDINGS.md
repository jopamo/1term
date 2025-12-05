# Keybindings Reference

All keybindings use `Ctrl+Shift` as modifiers.

## Clipboard Operations

- `Ctrl+Shift+C`: Copy selected text to clipboard.
- `Ctrl+Shift+V`: Paste clipboard content.
- `Ctrl+Shift+A`: Select all text and copy to clipboard.

## Scrollback Compression

- `Ctrl+Shift+B`: Compress the entire scrollback buffer to a compressed log file (`~/.1term/logs/terminal_YYYYMMDD_HHMMSS_D.logz`).

## UI Toggles

- `Ctrl+Shift+T`: Toggle window transparency (enabled by default).
- `Ctrl+Shift+S`: Toggle scrollback buffer (enabled by default; when disabled, scrollback lines set to 0).

## Tab Management

- `Ctrl+Shift+N`: Create a new tab in the current window.
- `Ctrl+Shift+W`: Close the current tab.

## Notes

- Keybindings are handled per-terminal instance.
- The scrollback compression runs asynchronously in a thread pool.
- Transparency and scrollback toggles affect all terminals in the current window.
- When the last tab is closed, the window automatically closes.