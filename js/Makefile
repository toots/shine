# Emscriptem compilation

EMCC:=emcc
EXPORTED_FUNCTIONS:='["_malloc", "_free", "_shine_check_config", "_shine_js_init",\
        "_shine_js_int16_len", "_shine_js_ptr_len", \
	"_shine_samples_per_pass", "_shine_encode_buffer",\
        "_shine_flush", "_shine_close"]'
CFLAGS:=-O3
LINKFLAGS:=-s EXPORTED_FUNCTIONS=$(EXPORTED_FUNCTIONS) --memory-init-file 0 $(CFLAGS)

C_FILES:=$(wildcard ../src/lib/*.c) src/wrapper.c
HEADER_FILES:=$(wildcard ../src/lib/*.h)
SOURCES:=$(C_FILES) $(HEADER_FILES)
OBJECTS:=$(C_FILES:%.c=%.o)

all: dist/libshine.js

dist/libshine.js: $(SOURCES) $(OBJECTS) src/pre.js src/post.js
	$(EMCC) $(LINKFLAGS) --pre-js src/pre.js --post-js src/post.js $(OBJECTS) -o $@

%.o: %.c
	$(EMCC) $(CFLAGS) -I../src/lib -c $< -o $@

clean:
	rm -f *.o ../src/lib/*.o
