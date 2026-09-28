#include "raylib.h"
extern "C" {
#include "ct_font.h"
#include "utils/executable_path.h"
}

#include "imgui.h"
#include "rlImGui.h"

#include <cstdio>

static bool load_imgui_font()
{
    char path[4096];
    if (executable_path(path, sizeof(path), "assets/ct_font.qoi") < 0) return false;

    Font source_font = LoadCtFont(path);
    Image source_image = LoadImage(path);
    Color *source_pixels = source_image.data ? LoadImageColors(source_image) : nullptr;
    if (!IsFontValid(source_font) || !source_pixels || source_font.glyphCount > 128)
    {
        if (source_pixels) UnloadImageColors(source_pixels);
        if (source_image.data) UnloadImage(source_image);
        if (IsFontValid(source_font)) UnloadFont(source_font);
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig config;
    config.SizePixels = (float)source_font.baseSize;
    config.PixelSnapH = true;
    ImFont *font = io.Fonts->AddFontDefaultBitmap(&config);
    io.FontDefault = font;

    struct GlyphRect { ImFontAtlasRectId id; Rectangle source; };
    GlyphRect glyph_rects[128];
    int rect_count = 0;
    for (int i = 1; i < source_font.glyphCount; ++i)
    {
        Rectangle rec = source_font.recs[i];
        int codepoint = source_font.glyphs[i].value;
        ImFontAtlasRectId id = io.Fonts->AddCustomRectFontGlyph(
            font, codepoint, (int)rec.width, (int)rec.height,
            (float)source_font.glyphs[i].advanceX);
        if (id == ImFontAtlasRectId_Invalid)
        {
            UnloadImageColors(source_pixels);
            UnloadImage(source_image);
            UnloadFont(source_font);
            return false;
        }
        font->GetFontBaked(config.SizePixels)->FindGlyph(codepoint)->Colored = false;
        glyph_rects[rect_count++] = {id, rec};
    }
    font->Flags |= ImFontFlags_LockBakedSizes;

    unsigned char *atlas_pixels = nullptr;
    int atlas_width = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_width, nullptr);
    Color separator = source_pixels[0];
    for (int i = 0; i < rect_count; ++i)
    {
        ImFontAtlasRect rect;
        if (!io.Fonts->GetCustomRect(glyph_rects[i].id, &rect)) continue;
        Rectangle source = glyph_rects[i].source;
        for (int y = 0; y < rect.h; ++y)
        {
            for (int x = 0; x < rect.w; ++x)
            {
                Color pixel = source_pixels[((int)source.y + y) * source_image.width +
                                            (int)source.x + x];
                unsigned char alpha = pixel.r == separator.r && pixel.g == separator.g &&
                                      pixel.b == separator.b && pixel.a == separator.a ? 0 : pixel.a;
                unsigned char *dst = atlas_pixels + ((rect.y + y) * atlas_width + rect.x + x) * 4;
                dst[0] = dst[1] = dst[2] = 255;
                dst[3] = alpha;
            }
        }
    }

    UnloadImageColors(source_pixels);
    UnloadImage(source_image);
    UnloadFont(source_font);
    return true;
}

int main()
{
    InitWindow(400, 300, "Dear ImGui on chrmaWM");
    if (!IsWindowReady()) return 1;

    SetTargetFPS(60);
    rlImGuiSetup(true);
    if (!load_imgui_font())
    {
        std::fprintf(stderr, "could not load assets/ct_font.qoi for Dear ImGui\n");
        rlImGuiShutdown();
        CloseWindow();
        return 1;
    }

    bool show_demo = true;
    float value = 0.5f;
    int clicks = 0;

    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(DARKGRAY);

        rlImGuiBegin();
        ImGui::Begin("chrmaWM + raylib + Dear ImGui");
        ImGui::Text("Move and resize this window to test input and clipping.");
        ImGui::SliderFloat("Value", &value, 0.0f, 1.0f);
        if (ImGui::Button("Click me")) ++clicks;
        ImGui::SameLine();
        ImGui::Text("Clicks: %d", clicks);
        ImGui::Checkbox("Show Dear ImGui demo", &show_demo);
        ImGui::Text("Frame rate: %.1f fps", ImGui::GetIO().Framerate);
        ImGui::End();
        if (show_demo) ImGui::ShowDemoWindow(&show_demo);
        rlImGuiEnd();

        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
