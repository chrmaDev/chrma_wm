# raylib software renderer on chrmaWM

This is a chrmaWM adaptation of raylib 6.0's `PLATFORM_MEMORY` backend. The raylib source stays external; the build copies it into the ignored `build/ai/` directory and overlays `rcore_cwm.c`. The backend connects through `cwm_client`, reads raylib's software framebuffer directly into CWM's BGRA shared buffer, commits each frame, and translates keyboard, mouse, close, and resize events.

Get the raylib 6.0 source and build the demo:

```sh
git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git /tmp/cwm-raylib-6.0
make -C software/ai RAYLIB_SRC=/tmp/cwm-raylib-6.0
./build/ai/demo
```

Run `./build/main` on the VT first. The demo is a normal raylib application linked to the CWM software backend. For another application, use `build/ai/raylib-src/raylib.h` and `build/ai/raylib-src/libraylib.a`, and link `src/cwm_client.c`, `-lxkbcommon -lm -lpthread -ldl`.

The demo loads ct's QOI font strip from `build/ai/assets/ct_font.qoi`. `LoadCtFont()` converts the magenta-separated fallback and ASCII glyphs into a raylib `Font` for `DrawTextEx()`; call `UnloadFont()` when done.

The CWM protocol currently supplies only the left mouse button and pointer movement; wheel, other buttons, clipboard, gamepads, and audio are outside this port. `SetWindowSize()` and other window management calls inherited from raylib's memory backend are stubs.
