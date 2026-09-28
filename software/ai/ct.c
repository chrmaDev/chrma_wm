#include "raylib.h"
#include "cwm_raylib_input.h"
#include "ct_font.h"
#include "terminal.h"
#include "utils/executable_path.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <pty.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

static int master_fd = -1;

static int send_bytes(const char *data, size_t size)
{
    if (master_fd >= 0 && size > 0)
        return write(master_fd, data, size) > 0;
    return 0;
}

static void send_reply(const char *data, size_t size)
{
    send_bytes(data, size);
}

static int send_key(int key, unsigned int modifiers)
{
    const char *sequence = NULL;

    switch (key) {
    case KEY_ENTER: case KEY_KP_ENTER: sequence = "\r"; break;
    case KEY_BACKSPACE: sequence = "\x7f"; break;
    case KEY_TAB: sequence = "\t"; break;
    case KEY_ESCAPE: sequence = "\033"; break;
    case KEY_UP: sequence = "\033[A"; break;
    case KEY_DOWN: sequence = "\033[B"; break;
    case KEY_RIGHT: sequence = "\033[C"; break;
    case KEY_LEFT: sequence = "\033[D"; break;
    case KEY_HOME: sequence = "\033[1~"; break;
    case KEY_END: sequence = "\033[4~"; break;
    case KEY_DELETE: sequence = "\033[3~"; break;
    case KEY_PAGE_UP: sequence = "\033[5~"; break;
    case KEY_PAGE_DOWN: sequence = "\033[6~"; break;
    default: break;
    }
    if (sequence) {
        if (modifiers & CWM_KEY_ALT) send_bytes("\033", 1);
        send_bytes(sequence, strlen(sequence));
        return 1;
    }
    return 0;
}

static int send_mouse(int button, int down, int cell_width, int cell_height)
{
    char sequence[40];
    int x = GetMouseX() / cell_width + 1;
    int y = GetMouseY() / cell_height + 1;
    int len;

    if (!term.mouse_mode || !term.mouse_sgr || x < 1 || y < 1 ||
        x > term.cols || y > term.rows) return 0;
    len = snprintf(sequence, sizeof(sequence), "\033[<%d;%d;%d%c",
                   button, x, y, down ? 'M' : 'm');
    return len > 0 && len < (int)sizeof(sequence) && send_bytes(sequence, (size_t)len);
}

static int send_input(int cell_width, int cell_height)
{
#ifndef WAYLAND_RAYLIB_BACKEND
    cwm_raylib_key_event event;
#endif
    int changed = 0;
    int sent = 0;
    float wheel = GetMouseWheelMove();

    if (term.mouse_mode && term.mouse_sgr) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            send_mouse(0, 1, cell_width, cell_height);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            send_mouse(0, 0, cell_width, cell_height);
        if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE))
            send_mouse(1, 1, cell_width, cell_height);
        if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE))
            send_mouse(1, 0, cell_width, cell_height);
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
            send_mouse(2, 1, cell_width, cell_height);
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
            send_mouse(2, 0, cell_width, cell_height);
        for (int i = 0; i < (int)wheel; i++)
            send_mouse(64, 1, cell_width, cell_height);
        for (int i = 0; i > (int)wheel; i--)
            send_mouse(65, 1, cell_width, cell_height);
    } else if (wheel != 0) changed |= terminal_scroll((int)(wheel * 3));

#ifdef WAYLAND_RAYLIB_BACKEND
    unsigned int modifiers = 0;
    int key;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) modifiers |= CWM_KEY_SHIFT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) modifiers |= CWM_KEY_CONTROL;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) modifiers |= CWM_KEY_ALT;
    while ((key = GetKeyPressed()) != 0) {
        if ((modifiers & CWM_KEY_SHIFT) && key == KEY_PAGE_UP)
            changed |= terminal_scroll(term.rows - 1);
        else if ((modifiers & CWM_KEY_SHIFT) && key == KEY_PAGE_DOWN)
            changed |= terminal_scroll(1 - term.rows);
        else if ((modifiers & CWM_KEY_SHIFT) && key == KEY_END)
            changed |= terminal_scroll_bottom();
        else if (send_key(key, modifiers))
            sent = 1;
        else if ((modifiers & CWM_KEY_CONTROL) && key >= KEY_A && key <= KEY_Z) {
            char control = (char)(key - KEY_A + 1);
            if (modifiers & CWM_KEY_ALT) send_bytes("\033", 1);
            sent |= send_bytes(&control, 1);
        }
    }
    for (int codepoint; (codepoint = GetCharPressed()) != 0;) {
        int length;
        if (codepoint < 32 || codepoint == 127 || (modifiers & CWM_KEY_CONTROL))
            continue;
        const char *utf8 = CodepointToUTF8(codepoint, &length);
        if (modifiers & CWM_KEY_ALT) send_bytes("\033", 1);
        sent |= send_bytes(utf8, (size_t)length);
    }
#else
    while (cwm_raylib_next_key_event(&event)) {
        if ((event.modifiers & CWM_KEY_SHIFT) && event.key == KEY_PAGE_UP)
            changed |= terminal_scroll(term.rows - 1);
        else if ((event.modifiers & CWM_KEY_SHIFT) && event.key == KEY_PAGE_DOWN)
            changed |= terminal_scroll(1 - term.rows);
        else if ((event.modifiers & CWM_KEY_SHIFT) && event.key == KEY_END)
            changed |= terminal_scroll_bottom();
        else if (send_key(event.key, event.modifiers))
            sent = 1;
        else if (event.codepoint) {
            int length;
            const char *utf8 = CodepointToUTF8(event.codepoint, &length);

            if (event.modifiers & CWM_KEY_ALT) send_bytes("\033", 1);
            sent |= send_bytes(utf8, (size_t)length);
        }
    }
#endif
    if (sent) changed |= terminal_scroll_bottom();
    return changed;
}

static Color ray_color(color c)
{
    return (Color){c.r, c.g, c.b, c.a};
}

static void draw_terminal(Font font, int cell_width, int cell_height)
{
    Color default_bg = ray_color(terminal_palette[0]);

    ClearBackground(default_bg);
    for (int row = 0; row < term.rows; row++) {
        const vt_cell *line = terminal_row(row);
        for (int col = 0; col < term.cols; col++) {
            const vt_cell *cell = &line[col];
            int cursor = term.history_offset == 0 && term.cursor_visible
                      && row == term.cursor_row && col == term.cursor_col;
            Color fg = ray_color(cursor ? cell->background : cell->foreground);
            Color bg = ray_color(cursor ? cell->foreground : cell->background);
            int x = col * cell_width;
            int y = row * cell_height;

            if (cursor || bg.r != default_bg.r || bg.g != default_bg.g ||
                bg.b != default_bg.b || bg.a != default_bg.a)
                DrawRectangle(x, y, cell_width, cell_height, bg);
            if (!cell->continuation && cell->glyph != ' ')
                DrawTextCodepoint(font, terminal_display_glyph(cell->glyph),
                                  (Vector2){(float)x, (float)y}, cell_height, fg);
        }
    }
}

static int resize_terminal(int cell_width, int cell_height)
{
    struct winsize ws = {0};
    int cols = GetScreenWidth() / cell_width;
    int rows = GetScreenHeight() / cell_height;

    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (terminal_resize(cols, rows) < 0) return -1;
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    ioctl(master_fd, TIOCSWINSZ, &ws);
    return 0;
}

int main(void)
{
    char font_path[4096];
    Font font;
    pid_t child;
    int cell_width;
    int cell_height;
    int dirty = 1;

    setlocale(LC_CTYPE, "");

#ifdef WAYLAND_RAYLIB_BACKEND
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(CT_COLS * 12, CT_ROWS * 18, "ChrmaTerm raylib");
#else
    InitWindow(CT_COLS * 4, CT_ROWS * 6, "ChrmaTerm raylib");
#endif
    if (!IsWindowReady()) return 1;
    SetExitKey(KEY_NULL);
    if (executable_path(font_path, sizeof(font_path), "assets/ct_font.qoi") < 0) {
        CloseWindow();
        return 1;
    }
    font = LoadCtFont(font_path);
    if (!IsFontValid(font)) {
        CloseWindow();
        return 1;
    }
    cell_width = (int)GetGlyphAtlasRec(font, ' ').width;
    cell_height = font.baseSize;
    if (cell_width < 1 || cell_height < 1) {
        UnloadFont(font);
        CloseWindow();
        return 1;
    }
#ifdef WAYLAND_RAYLIB_BACKEND
    int monitor = GetCurrentMonitor();
    int available_width = GetMonitorWidth(monitor) * 4 / 5;
    int available_height = GetMonitorHeight(monitor) * 4 / 5;
    int scale = 1;

    if (available_width > 0 && available_height > 0) {
        while (scale < 8 && CT_COLS * cell_width * (scale + 1) <= available_width &&
               CT_ROWS * cell_height * (scale + 1) <= available_height)
            scale++;
    } else scale = 3;
    cell_width *= scale;
    cell_height *= scale;
    SetWindowSize(CT_COLS * cell_width, CT_ROWS * cell_height);
#endif

    child = forkpty(&master_fd, NULL, NULL, NULL);
    if (child < 0) {
        UnloadFont(font);
        CloseWindow();
        return 1;
    }
    if (child == 0) {
        const char *home = getenv("HOME");
        const char *shell = getenv("SHELL");
        if (home) chdir(home);
        setenv("TERM", "xterm-256color", 1);
        if (shell && shell[0]) execl(shell, shell, "-i", (char *)NULL);
        execl("/bin/sh", "sh", "-i", (char *)NULL);
        _exit(1);
    }
    fcntl(master_fd, F_SETFL, fcntl(master_fd, F_GETFL) | O_NONBLOCK);
    terminal_set_reply(send_reply);
    if (resize_terminal(cell_width, cell_height) < 0) goto done;
    if (terminal_enable_history(1000) < 0) goto done;

    SetTargetFPS(30);
    while (!WindowShouldClose()) {
        uint8_t buf[512];

        if (IsWindowResized()) {
            if (resize_terminal(cell_width, cell_height) < 0) break;
            dirty = 1;
        }
        dirty |= send_input(cell_width, cell_height);
        for (int reads = 0; reads < 64; reads++) {
            ssize_t n = read(master_fd, buf, sizeof(buf));
            if (n > 0) {
                terminal_feed(buf, (size_t)n);
                dirty = 1;
                continue;
            }
            if (n == 0) goto done;
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) goto done;
            break;
        }

        BeginDrawing();
#ifdef WAYLAND_RAYLIB_BACKEND
        draw_terminal(font, cell_width, cell_height);
#else
        if (dirty) draw_terminal(font, cell_width, cell_height);
#endif
        EndDrawing();
        dirty = 0;
    }

done:
    close(master_fd);
    waitpid(child, NULL, WNOHANG);
    terminal_free();
    UnloadFont(font);
    CloseWindow();
    return 0;
}
