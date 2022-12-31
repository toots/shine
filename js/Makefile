# Emscriptem compilation

EMCC:=emcc
EXPORTED_FUNCTIONS:='["_malloc", "_free", "_shine_check_config", "_shine_js_init",\
        "_shine_js_int16_len", "_shine_js_ptr_len", \
	"_shine_samples_per_pass", "_shine_encode_buffer",\
        "_shine_flush", "_shine_close"]'
CFLAGS:=-O3
LINKFLAGS:=-s EXPORTED_FUNCTIONS=$(EXPORTED_FUNCTIONS) -s SINGLE_FILE=1 -s EXPORTED_RUNTIME_METHODS=setValue,getValue --memory-init-file 0 $(CFLAGS)
WEB_LINKFLAGS:=$(LINKFLAGS) -s ENVIRONMENT='web' -s EXPORT_NAME='createModule' -s USE_ES6_IMPORT_META=0

C_FILES:=$(wildcard ../src/lib/*.c) src/wrapper.c
HEADER_FILES:=$(wildcard ../src/lib/*.h)
SOURCES:=$(C_FILES) $(HEADER_FILES)
OBJECTS:=$(C_FILES:%.c=%.o)

all: dist/libshine_node.js dist/libshine_browser.js

src/post-node.js: src/libshine_class.js src/libshine_node_wrapper.js
	rm -f src/post-node.js
	cat src/libshine_class.js src/libshine_node_wrapper.js > src/post-node.js

dist/libshine_node.js: $(SOURCES) $(OBJECTS) src/post-node.js
	$(EMCC) $(LINKFLAGS) --post-js src/post-node.js $(OBJECTS) -o dist/libshine_node.js

dist/libshine_browser_stubs.js: $(SOURCES) $(OBJECTS)
	$(EMCC) $(WEB_LINKFLAGS) $(OBJECTS) -o dist/libshine_browser_stubs.mjs
	mv dist/libshine_browser_stubs.mjs dist/libshine_browser_stubs.js

dist/libshine_browser.js: src/libshine_class.js src/libshine_browser_wrapper.js dist/libshine_browser_stubs.js
	cat src/libshine_class.js src/libshine_browser_wrapper.js > dist/libshine_browser.js

%.o: %.c
	$(EMCC) $(CFLAGS) -I../src/lib -c $< -o $@

clean:
	rm -rf *.o ../src/lib/*.o dist/libshine*js
