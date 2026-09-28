#include "raylib.h"
#include "ct_font.h"
#include "utils/executable_path.h"

#include <stdio.h>

int main(void)
{
    InitWindow(480, 300, "raylib on chrmaWM");
    if (!IsWindowReady()) return 1;

    char font_path[4096];
    if (executable_path(font_path, sizeof(font_path), "assets/ct_font.qoi") < 0) {
        CloseWindow();
        return 1;
    }
    Font font = LoadCtFont(font_path);
    if (!IsFontValid(font)) {
        fprintf(stderr, "could not load ct font: %s\n", font_path);
        CloseWindow();
        return 1;
    }

    SetTargetFPS(60);
    while (!WindowShouldClose())
    {
        Vector2 mouse = GetMousePosition();

        BeginDrawing();
        ClearBackground((Color){ 25, 22, 40, 255 });
        DrawRectangle(16, 16, GetScreenWidth() - 32, GetScreenHeight() - 32,
                      (Color){ 51, 46, 76, 255 });
        DrawCircleV(mouse, IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 24 : 12,
                    IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? ORANGE : SKYBLUE);
        DrawTextEx(font, TextFormat("fps: %d", GetFPS()), (Vector2){0, 0}, 12, 0, GREEN);
        DrawTextEx(font, "raylib demo", (Vector2){32, 35}, 18, 0, WHITE);
        DrawTextEx(font, "Move/click the mouse; resize or press Esc", (Vector2){32, 65}, 12, 0, WHITE);
        DrawTextEx(font, TextFormat("%d x %d  |  mouse %.0f, %.0f",
                            GetScreenWidth(), GetScreenHeight(), mouse.x, mouse.y),
                   (Vector2){32, 90}, 12, 0, YELLOW);
        EndDrawing();
    }
    UnloadFont(font);
    CloseWindow();
    return 0;
}
