# raylib software renderer on chrmaWM

This is a chrmaWM adaptation of raylib 6.0's `PLATFORM_MEMORY` backend. The raylib source stays external; the build copies it into the ignored `build/ai/` directory and overlays `rcore_cwm.c`. The backend connects through `cwm_client`, reads raylib's software framebuffer directly into CWM's BGRA shared buffer, commits each frame, and translates keyboard, mouse, close, and resize events.

Get the raylib 6.0 source and build the demo:

```sh
git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git /tmp/cwm-raylib-6.0
make -C software/ai RAYLIB_SRC=/tmp/cwm-raylib-6.0
./build/ai/ct
```

Run `./build/main` on the VT first. CWM's Super shortcut launches `build/ai/ct`, so build the raylib port before using that shortcut. `./build/ai/demo` is also available as a small raylib drawing sample. For another application, use `build/ai/raylib-src/raylib.h` and `build/ai/raylib-src/libraylib.a`, and link `src/cwm_client.c`, `-lxkbcommon -lm -lpthread -ldl`.

Both programs load ct's QOI font strip from `build/ai/assets/ct_font.qoi`. `LoadCtFont()` converts the magenta-separated fallback and ASCII glyphs into a raylib `Font`; call `UnloadFont()` when done. `build/ai/ct` is the raylib terminal. It shares the cell grid and VT parser in `software/ct/terminal.c` with the direct CWM terminal at `build/ct`, while raylib owns its drawing and keyboard input. The raylib terminal keeps Esc as a shell key; close its window to exit.

The raylib terminal starts `$SHELL` interactively, falling back to `/bin/sh`, and advertises `xterm-256color`. Its scrollback keeps 1000 screen lines. Use the mouse wheel or Shift+PageUp/PageDown to browse, and Shift+End to return to the live screen. When an application requests SGR mouse reporting, clicks and wheel events go to that application instead. Resizing clears scrollback because the terminal does not reflow saved lines yet.

The shared terminal core handles UTF-8 cell widths, 256-color and RGB SGR colors, character editing, and a separate screen for full-screen applications. The custom font still contains ASCII only; other characters occupy the correct number of cells but draw as simple ASCII fallbacks.

CWM also supports Sticky Keys for Shift, Ctrl, and Alt in every client. Tap a modifier to apply it to the next key; tap it twice to lock it, then tap it again to unlock. Holding a modifier during a chord still works normally. Super keeps its CWM shortcut behavior.

The CWM protocol currently supplies pointer movement, the left mouse button, and wheel scrolling; other buttons, clipboard, gamepads, and audio are outside this port. `SetWindowSize()` and other window management calls inherited from raylib's memory backend are stubs.
