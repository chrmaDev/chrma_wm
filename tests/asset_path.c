#include "utils/executable_path.h"
#include "utils/imgload.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    const char *assets[] = {
        "assets/background.qoi",
        "assets/window_atlas.qoi",
        "assets/ui_font.qoi",
        "assets/ct_font.qoi",
    };
    char path[4096];

    for (size_t i = 0; i < sizeof(assets) / sizeof(assets[0]); i++) {
        pixmap image = {0};

        assert(executable_path(path, sizeof(path), assets[i]) == 0);
        assert(load_qoi(path, &image));
        assert(image.width > 0 && image.height > 0);
        free(image.data);
    }
    assert(executable_path(path, sizeof(path), "ct") == 0);
    assert(access(path, X_OK) == 0);
    puts("asset path checks passed");
    return 0;
}
