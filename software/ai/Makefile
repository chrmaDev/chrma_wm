ROOT := ../..
OUT := $(ROOT)/build/ai
RAYLIB_SRC ?= /tmp/cwm-raylib-6.0
RAYLIB_COPY := $(OUT)/raylib-src
RAYLIB_LIB := $(RAYLIB_COPY)/libraylib.a

.PHONY: all clean
all: $(OUT)/demo $(OUT)/ct

$(OUT)/assets/ct_font.qoi: $(ROOT)/assets/ct_font.qoi
	mkdir -p $(OUT)/assets
	cp $< $@

$(RAYLIB_COPY)/.copied:
	@test -f "$(RAYLIB_SRC)/src/rcore.c" || { echo "Set RAYLIB_SRC to raylib 6.0 source"; exit 1; }
	mkdir -p $(OUT)
	cp -a "$(RAYLIB_SRC)/src" $(RAYLIB_COPY)
	touch $@

$(RAYLIB_LIB): rcore_cwm.c $(ROOT)/include/cwm_raylib_input.h $(RAYLIB_COPY)/.copied
	cp rcore_cwm.c $(RAYLIB_COPY)/platforms/rcore_memory.c
	$(MAKE) -C $(RAYLIB_COPY) PLATFORM=PLATFORM_MEMORY RAYLIB_MODULE_AUDIO=FALSE RAYLIB_MODULE_MODELS=FALSE RAYLIB_SRC_PATH=. RAYLIB_RELEASE_PATH=. CUSTOM_CFLAGS="-O2 -I$(abspath $(ROOT)/include)"

$(OUT)/demo: demo.c ct_font.c ct_font.h $(RAYLIB_LIB) $(ROOT)/src/cwm_client.c $(ROOT)/src/utils/executable_path.c | $(OUT)/assets/ct_font.qoi
	cc -O2 -Wall -Wextra -I$(RAYLIB_COPY) -I$(ROOT)/include demo.c ct_font.c $(ROOT)/src/cwm_client.c $(ROOT)/src/utils/executable_path.c $(RAYLIB_LIB) -o $@ -lxkbcommon -lm -lpthread -ldl

$(OUT)/ct: ct.c ct_font.c ct_font.h $(ROOT)/include/cwm_raylib_input.h $(ROOT)/software/ct/terminal.c $(ROOT)/software/ct/terminal.h $(RAYLIB_LIB) $(ROOT)/src/cwm_client.c $(ROOT)/src/utils/executable_path.c | $(OUT)/assets/ct_font.qoi
	cc -O2 -Wall -Wextra -D_GNU_SOURCE -I$(RAYLIB_COPY) -I$(ROOT)/include -I$(ROOT)/software/ct ct.c ct_font.c $(ROOT)/software/ct/terminal.c $(ROOT)/src/cwm_client.c $(ROOT)/src/utils/executable_path.c $(RAYLIB_LIB) -o $@ -lxkbcommon -lutil -lm -lpthread -ldl

clean:
	$(RM) -r $(OUT)
