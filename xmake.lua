set_project("chrma_wm")
set_languages("c99")
add_rules("mode.debug", "mode.release")
add_defines("_GNU_SOURCE")

local raylib_source = os.getenv("RAYLIB_SRC") or "/tmp/cwm-raylib-6.0"
local imgui_source = os.getenv("IMGUI_SRC") or "/tmp/cwm-imgui"
local rlimgui_source = os.getenv("RLIMGUI_SRC") or "/tmp/cwm-rlimgui"

local function copy_assets(directory, names, scriptos)
    scriptos.mkdir(path.join(directory, "assets"))
    for _, name in ipairs(names) do
        scriptos.cp(path.join("assets", name), path.join(directory, "assets", name))
    end
end

local function build_raylib(target, backend, fileio, scriptos, abort)
    local copy = path.absolute(path.join("build", backend, "raylib-src"))
    local needs_build = not scriptos.isfile(path.join(copy, "libraylib.a"))
    if not scriptos.isfile(path.join(raylib_source, "src", "rcore.c")) then
        abort("Set RAYLIB_SRC to a raylib 6.0 source directory")
    end
    if not scriptos.isfile(path.join(copy, "rcore.c")) then
        scriptos.mkdir(path.directory(copy))
        scriptos.execv("cp", {"-a", path.join(raylib_source, "src"), copy})
        needs_build = true
    end
    local args = {"-C", copy, "RAYLIB_SRC_PATH=.", "RAYLIB_RELEASE_PATH=.",
                  "RAYLIB_MODULE_AUDIO=FALSE", "RAYLIB_MODULE_MODELS=FALSE"}
    if backend == "ai" then
        local overlay = path.join(copy, "platforms", "rcore_memory.c")
        local port = "software/ai/rcore_cwm.c"
        if fileio.readfile(overlay) ~= fileio.readfile(port) then
            scriptos.cp(port, overlay)
            needs_build = true
        end
        table.insert(args, "PLATFORM=PLATFORM_MEMORY")
        table.insert(args, "CUSTOM_CFLAGS=-O2 -I" .. path.absolute("include"))
    else
        table.insert(args, "PLATFORM=PLATFORM_DESKTOP")
        table.insert(args, "GLFW_LINUX_ENABLE_WAYLAND=TRUE")
        table.insert(args, "GLFW_LINUX_ENABLE_X11=FALSE")
        table.insert(args, "CUSTOM_CFLAGS=-O2")
    end
    if needs_build then scriptos.execv("make", args) end
    local built = path.join(copy, "libraylib.a")
    local output = target:targetfile()
    if not scriptos.isfile(output) or fileio.readfile(built) ~= fileio.readfile(output) then
        scriptos.mkdir(path.directory(output))
        scriptos.cp(built, output)
    end
end

target("raylib_cwm")
    set_kind("static")
    set_default(false)
    set_targetdir("build/ai")
    on_build(function (target) build_raylib(target, "ai", io, os, raise) end)

target("raylib_wayland")
    set_kind("static")
    set_default(false)
    set_targetdir("build/wayland")
    on_build(function (target) build_raylib(target, "wayland", io, os, raise) end)

target("ct_cwm")
    set_kind("binary")
    set_default(false)
    set_targetdir("build/ai")
    set_filename("ct")
    add_deps("raylib_cwm")
    add_linkdirs("build/ai")
    add_links("raylib_cwm")
    add_includedirs("include", "software/ct", "build/ai/raylib-src")
    add_files("software/ai/ct.c", "software/ai/ct_font.c", "software/ct/terminal.c",
              "src/cwm_client.c", "src/utils/executable_path.c")
    add_syslinks("xkbcommon", "util", "m", "pthread", "dl")
    after_build(function () copy_assets("build/ai", {"ct_font.qoi"}, os) end)

target("demo_cwm")
    set_kind("binary")
    set_default(false)
    set_targetdir("build/ai")
    set_filename("demo")
    add_deps("raylib_cwm")
    add_linkdirs("build/ai")
    add_links("raylib_cwm")
    add_includedirs("include", "build/ai/raylib-src")
    add_files("software/ai/demo.c", "software/ai/ct_font.c",
              "src/cwm_client.c", "src/utils/executable_path.c")
    add_syslinks("xkbcommon", "m", "pthread", "dl")
    after_build(function () copy_assets("build/ai", {"ct_font.qoi"}, os) end)

target("imgui_demo_cwm")
    set_kind("binary")
    set_default(false)
    set_languages("c99", "c++17")
    set_targetdir("build/ai")
    set_filename("imgui_demo")
    add_deps("raylib_cwm")
    add_linkdirs("build/ai")
    add_links("raylib_cwm")
    add_includedirs("include", "build/ai/raylib-src", imgui_source, rlimgui_source)
    add_defines("NO_FONT_AWESOME")
    add_files("software/ai/imgui_demo.cpp", "software/ai/ct_font.c",
              "src/cwm_client.c", "src/utils/executable_path.c",
              path.join(imgui_source, "imgui.cpp"),
              path.join(imgui_source, "imgui_draw.cpp"),
              path.join(imgui_source, "imgui_tables.cpp"),
              path.join(imgui_source, "imgui_widgets.cpp"),
              path.join(imgui_source, "imgui_demo.cpp"),
              "software/ai/rlimgui_cwm.cpp")
    add_syslinks("xkbcommon", "m", "pthread", "dl")
    after_build(function () copy_assets("build/ai", {"ct_font.qoi"}, os) end)

target("ct_wayland")
    set_kind("binary")
    set_default(false)
    set_targetdir("build/wayland")
    set_filename("ct")
    add_deps("raylib_wayland")
    add_linkdirs("build/wayland")
    add_links("raylib_wayland")
    add_defines("WAYLAND_RAYLIB_BACKEND")
    add_includedirs("include", "software/ct", "build/wayland/raylib-src")
    add_files("software/ai/ct.c", "software/ai/ct_font.c", "software/ct/terminal.c",
              "src/utils/executable_path.c")
    add_syslinks("util", "m", "pthread", "dl", "rt", "wayland-client",
                 "wayland-cursor", "wayland-egl", "xkbcommon", "GL")
    after_build(function () copy_assets("build/wayland", {"ct_font.qoi"}, os) end)

target("demo_wayland")
    set_kind("binary")
    set_default(false)
    set_targetdir("build/wayland")
    set_filename("demo")
    add_deps("raylib_wayland")
    add_linkdirs("build/wayland")
    add_links("raylib_wayland")
    add_defines("WAYLAND_RAYLIB_BACKEND")
    add_includedirs("include", "build/wayland/raylib-src")
    add_files("software/ai/demo.c", "software/ai/ct_font.c", "src/utils/executable_path.c")
    add_syslinks("m", "pthread", "dl", "rt", "wayland-client",
                 "wayland-cursor", "wayland-egl", "xkbcommon", "GL")
    after_build(function () copy_assets("build/wayland", {"ct_font.qoi"}, os) end)

target("ct_direct")
    set_kind("binary")
    set_default(false)
    set_targetdir("build")
    set_filename("ct")
    add_includedirs("include")
    add_files("software/ct/main.c", "software/ct/terminal.c", "src/cwm_client.c",
              "src/rendering.c", "src/utils/font.c", "src/utils/imgload.c",
              "src/utils/executable_path.c")
    add_syslinks("util", "xkbcommon")
    after_build(function () copy_assets("build", {"ct_font.qoi"}, os) end)

target("cwm")
    set_kind("binary")
    set_targetdir("build")
    set_filename("main")
    add_deps("ct_cwm")
    add_includedirs("include", "/usr/include/libdrm")
    add_files("src/main.c", "src/kms.c", "src/rendering.c", "src/input.c",
              "src/windows.c", "src/utils/font.c", "src/utils/vector.c",
              "src/utils/imgload.c", "src/utils/executable_path.c", "src/cwm_protocol.c")
    add_syslinks("xkbcommon", "drm", "input", "udev", "z")
    after_build(function ()
        copy_assets("build", {"background.qoi", "window_atlas.qoi", "ui_font.qoi"}, os)
    end)
