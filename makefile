CC = gcc
BUILD = build
PKG_CFLAGS := $(shell pkg-config --cflags xkbcommon libdrm libinput libudev)
PKG_LIBS   := $(shell pkg-config --libs xkbcommon libdrm libinput libudev)

CFLAGS = -std=c99 -Wall -Wextra -D_GNU_SOURCE -Iinclude $(PKG_CFLAGS) -MMD -MP
LDLIBS = $(PKG_LIBS)

SRCS = src/main.c src/kms.c src/rendering.c src/input.c src/windows.c \
	src/utils/font.c src/utils/vector.c src/utils/imgload.c src/cwm_protocol.c
OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

CLIENT_SRCS = src/test_client.c src/cwm_client.c
CLIENT_OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(CLIENT_SRCS))
CLIENT_DEPS = $(CLIENT_OBJS:.o=.d)

CT_SRCS = src/ct.c src/cwm_client.c src/rendering.c src/utils/font.c src/utils/imgload.c
CT_OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(CT_SRCS))
CT_DEPS = $(CT_OBJS:.o=.d)

.PHONY: all clean compile_commands
.DELETE_ON_ERROR:

all: $(BUILD)/main $(BUILD)/ct

$(BUILD)/main: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS) -lz

$(BUILD)/test_client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJS)

$(BUILD)/ct: $(CT_OBJS)
	$(CC) $(CFLAGS) -o $@ $(CT_OBJS) -lutil -lxkbcommon

$(BUILD)/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@.tmp $<
	mv -f $@.tmp $@

clean:
	rm -rf $(BUILD) src/*.o main

compile_commands:
	python3 -c "import json,os; root=os.getcwd(); files='''$(SRCS) $(CLIENT_SRCS) $(CT_SRCS)'''.split(); flags='$(CC) $(CFLAGS)'.split(); print(json.dumps([{'directory':root,'file':f,'arguments':flags+['-c',f]} for f in files], indent=2))" > compile_commands.json

-include $(DEPS) $(CLIENT_DEPS) $(CT_DEPS)
