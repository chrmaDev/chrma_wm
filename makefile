CC = gcc
BUILD = build
PKG_CFLAGS := $(shell pkg-config --cflags xkbcommon libdrm libinput libudev)
PKG_LIBS   := $(shell pkg-config --libs xkbcommon libdrm libinput libudev)

CFLAGS = -std=c99 -Wall -Wextra -D_GNU_SOURCE -Iinclude $(PKG_CFLAGS) -MMD -MP
LDLIBS = $(PKG_LIBS)

SRCS = src/main.c src/kms.c src/rendering.c src/input.c src/windows.c \
	src/utils/font.c src/utils/vector.c src/utils/imgload.c \
	src/utils/executable_path.c src/cwm_protocol.c
OBJS = $(patsubst %.c,$(BUILD)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

CT_SRCS = software/ct/main.c src/cwm_client.c src/rendering.c \
	src/utils/font.c src/utils/imgload.c src/utils/executable_path.c
CT_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(CT_SRCS))
CT_DEPS = $(CT_OBJS:.o=.d)
WM_ASSETS = $(addprefix $(BUILD)/assets/,background.qoi window_atlas.qoi ui_font.qoi)
CT_ASSETS = $(BUILD)/assets/ct_font.qoi

.PHONY: all check clean compile_commands
.DELETE_ON_ERROR:

all: $(BUILD)/main $(BUILD)/ct

$(BUILD)/main: $(OBJS) | $(WM_ASSETS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS) -lz

$(BUILD)/ct: $(CT_OBJS) | $(CT_ASSETS)
	$(CC) $(CFLAGS) -o $@ $(CT_OBJS) -lutil -lxkbcommon

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@.tmp $<
	mv -f $@.tmp $@

$(BUILD)/assets/%: assets/%
	mkdir -p $(dir $@)
	cp $< $@

$(BUILD)/protocol_test: tests/protocol.c src/cwm_protocol.c src/windows.c src/utils/vector.c
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -o $@ \
		tests/protocol.c src/windows.c src/utils/vector.c -Wl,--gc-sections

$(BUILD)/asset_path_test: tests/asset_path.c src/utils/executable_path.c src/utils/imgload.c | $(BUILD)/ct $(WM_ASSETS)
	$(CC) $(CFLAGS) -o $@ $^

check: $(BUILD)/protocol_test $(BUILD)/asset_path_test
	$(BUILD)/protocol_test
	cd /tmp && $(abspath $(BUILD))/asset_path_test

clean:
	rm -rf $(BUILD) src/*.o main

compile_commands:
	python3 -c "import json,os; root=os.getcwd(); files='''$(SRCS) $(CT_SRCS)'''.split(); flags='$(CC) $(CFLAGS)'.split(); print(json.dumps([{'directory':root,'file':f,'arguments':flags+['-c',f]} for f in files], indent=2))" > compile_commands.json

-include $(DEPS) $(CT_DEPS)
