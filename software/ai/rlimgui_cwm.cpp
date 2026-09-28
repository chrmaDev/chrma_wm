#include "raylib.h"
#include "rlgl.h"

// rlImGui binds textures after rlBegin(). CWM's software renderer uses the
// OpenGL 1.1 path, where changing textures inside glBegin/glEnd is invalid.
static int pending_mode = 0;

static void cwm_rl_begin(int mode)
{
    pending_mode = mode;
}

static void cwm_rl_set_texture(unsigned int id)
{
    rlSetTexture(id);
    if (pending_mode != 0)
    {
        rlBegin(pending_mode);
        pending_mode = 0;
    }
}

// rlImGui supplies OpenGL's bottom-left Y, but rlsw's swScissor uses top-left Y.
static void cwm_rl_scissor(int x, int y, int width, int height)
{
    rlScissor(x, GetScreenHeight() - y - height, width, height);
}

#define rlBegin cwm_rl_begin
#define rlSetTexture cwm_rl_set_texture
#define rlScissor cwm_rl_scissor
#include "rlImGui.cpp"
