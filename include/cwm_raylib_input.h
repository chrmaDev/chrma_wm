#ifndef CWM_RAYLIB_INPUT_H
#define CWM_RAYLIB_INPUT_H

#define CWM_KEY_SHIFT 1
#define CWM_KEY_CONTROL 2
#define CWM_KEY_ALT 4

typedef struct {
    int key;
    int codepoint;
    unsigned int modifiers;
} cwm_raylib_key_event;

int cwm_raylib_next_key_event(cwm_raylib_key_event *event);

#endif
