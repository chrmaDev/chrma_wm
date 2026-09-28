# Raylib on CWM and Wayland

Both builds use raylib 6.0 and the same application sources in this directory. The CWM build copies raylib's source into `build/ai/raylib-src`, replaces its memory backend with `rcore_cwm.c`, and draws into a CWM shared buffer. The Wayland build uses a separate source copy in `build/wayland/raylib-src` and raylib's desktop backend. Raylib's own Makefile builds those two libraries; xmake builds CWM and the applications.

Put raylib 6.0 source at `/tmp/cwm-raylib-6.0`, or set `RAYLIB_SRC` to its directory. The Wayland build also needs Wayland, EGL, OpenGL, xkbcommon, and `wayland-scanner` development packages.

```sh
git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git /tmp/cwm-raylib-6.0
xmake build cwm
xmake build demo_cwm
xmake build ct_wayland
xmake build demo_wayland
```

`xmake build cwm` creates `build/main` and `build/ai/ct`. Run CWM on a VT; its Super shortcut opens the CWM terminal. The demo binaries are `build/ai/demo` and `build/wayland/demo`. The Wayland terminal is `build/wayland/ct`. Each binary loads `assets/ct_font.qoi` beside its output directory.

`ct.c` shares its parser and drawing code across backends. Its CWM build reads the CWM key event queue; its Wayland build reads raylib's desktop key and text queues. The direct CWM renderer is still available as `xmake build ct_direct` and `build/ct`.

The CWM and Wayland binaries can run at the same time in separate processes when both display sessions are available. Raylib chooses its platform backend at build time, so these are separate executables.

## Dear ImGui demo on CWM

The Dear ImGui demo uses rlImGui's Raylib 6.0 release and Dear ImGui 1.92.7. Put their source trees at `/tmp/cwm-rlimgui` and `/tmp/cwm-imgui`, or set `RLIMGUI_SRC` and `IMGUI_SRC` to their directories.

```sh
git clone --depth 1 --branch Raylib_6_0 https://github.com/raylib-extras/rlImGui.git /tmp/cwm-rlimgui
git clone --depth 1 --branch v1.92.7 https://github.com/ocornut/imgui.git /tmp/cwm-imgui
xmake build imgui_demo_cwm
./build/ai/imgui_demo
```

Run the last command inside chrmaWM. The demo opens a small control panel and Dear ImGui's full demo window. Its text uses the `assets/ct_font.qoi` bitmap glyphs at their native 6-pixel height, with ImGui text colors. It uses the CWM raylib software renderer and requires no OpenGL context. CWM does not currently change the mouse cursor shape for ImGui controls.

CWM provides left, right, and middle mouse buttons, wheel scrolling, focus changes, and basic window size, title, minimize, maximize, and fullscreen requests. The text clipboard is shared between CWM clients and accepts up to 1023 bytes. Gamepads, audio, and advanced window modes are outside this port.
