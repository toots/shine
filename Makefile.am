EXTRA_DIST = ChangeLog README.md README.old libshine.sym js tests/golden.txt \
             tests/wasm.mjs tools/generate_tables.c tools/generate_psy_tables.c \
             tools/check-no-float.sh

noinst_HEADERS = src/bin/main.h src/bin/wave.h \
                 src/lib/bitstream.h src/lib/huffman.h src/lib/l3bitstream.h \
                 src/lib/l3loop.h src/lib/l3mdct.h src/lib/l3subband.h \
                 src/lib/fixed_point.h src/lib/reservoir.h src/lib/tables.h \
                 src/lib/types.h src/lib/psy.h src/lib/psy_tables.h

AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS  = -I m4

# Optimization comes from CFLAGS, which configure defaults to -g -O2.
AM_CFLAGS = -Wall -Wextra -funroll-loops -fsigned-char

lib_LTLIBRARIES      = libshine.la
# Everything but the model, which tests/psy_model.c includes.
CORE_SOURCES         = src/lib/bitstream.c src/lib/huffman.c \
                       src/lib/l3bitstream.c src/lib/l3loop.c src/lib/l3mdct.c \
                       src/lib/l3subband.c src/lib/layer3.c src/lib/reservoir.c \
                       src/lib/tables.c src/lib/precomputed_tables.c
libshine_la_SOURCES  = $(CORE_SOURCES) src/lib/psy.c

libshine_la_LDFLAGS  = -no-undefined -version-info 4:0:1 -export-symbols $(srcdir)/libshine.sym
libshine_ladir       = ${prefix}/include/shine
libshine_la_HEADERS  = src/lib/layer3.h

bin_PROGRAMS     = shineenc
shineenc_SOURCES = src/bin/main.c src/bin/wave.c
shineenc_LDADD   = libshine.la

# One program per concern. A new test needs its program here and its
# _SOURCES line below.
TESTS          = tests/framing tests/flush tests/wave tests/cli tests/golden \
                 tests/level tests/psy_model tools/psy_oracle
check_PROGRAMS = $(TESTS)
TEST_HELPERS   = tests/helpers.c tests/helpers.h
LDADD          = libshine.la
AM_CPPFLAGS    = -I$(srcdir)/src/lib

tests_framing_SOURCES = tests/framing.c $(TEST_HELPERS)
tests_flush_SOURCES   = tests/flush.c $(TEST_HELPERS)
tests_wave_SOURCES    = tests/wave.c $(TEST_HELPERS)
tests_cli_SOURCES     = tests/cli.c $(TEST_HELPERS)
tests_golden_SOURCES  = tests/golden.c $(TEST_HELPERS)
tests_level_SOURCES   = tests/level.c $(TEST_HELPERS)

# Built from the library's sources, to reach the model's internals. The
# oracle (doc/psy/tests.md §3) is deleted once phase 1 lands.
tests_psy_model_SOURCES = tests/psy_model.c $(CORE_SOURCES)
tests_psy_model_CFLAGS  = $(AM_CFLAGS)
tests_psy_model_LDADD   = -lm
tools_psy_oracle_SOURCES = tools/psy_oracle.c $(libshine_la_SOURCES)
tools_psy_oracle_CFLAGS  = $(AM_CFLAGS)
tools_psy_oracle_LDADD   = -lm

AM_TESTS_ENVIRONMENT = SHINEENC='$(abs_top_builddir)/shineenc'; export SHINEENC; \
                       SHINE_GOLDEN='$(abs_top_srcdir)/tests/golden.txt'; export SHINE_GOLDEN;

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = shine.pc

# WebAssembly build of the JavaScript package: `make wasm`, tested by
# `make check-wasm`. Separate from all and check, and only available when
# configure found a WebAssembly toolchain.
JS_DIST     = js/dist
WASM_SOURCES = $(libshine_la_SOURCES) js/src/wrapper.c
WASM_EXPORTS = malloc free shine_check_config shine_js_init \
               shine_samples_per_pass shine_encode_buffer shine_flush \
               shine_close

if HAVE_WASM
wasm: $(JS_DIST)/shine.wasm $(JS_DIST)/shine_wasm.mjs $(JS_DIST)/shine.mjs

$(JS_DIST)/shine.wasm: $(WASM_SOURCES) $(noinst_HEADERS) src/lib/layer3.h
	@$(MKDIR_P) $(JS_DIST)
	sources=; for source in $(WASM_SOURCES); do \
	  sources="$$sources $(srcdir)/$$source"; done; \
	exports=; for name in $(WASM_EXPORTS); do \
	  exports="$$exports -Wl,--export=$$name"; done; \
	$(WASM_CC) $(WASM_FLAGS) -O3 -I$(srcdir)/src/lib $$exports $$sources \
	  -lc -o $@

# The module embedded as base64, so that the package is usable as is by
# bundlers and browsers.
$(JS_DIST)/shine_wasm.mjs: $(JS_DIST)/shine.wasm
	{ printf 'export default "'; base64 < $(JS_DIST)/shine.wasm | tr -d '\n'; \
	  printf '";\n'; } > $@

$(JS_DIST)/shine.mjs: js/src/shine.mjs
	@$(MKDIR_P) $(JS_DIST)
	cp $(srcdir)/js/src/shine.mjs $@

check-wasm: wasm shineenc$(EXEEXT)
	@test -n "$(NODE)" || { echo "check-wasm needs node: rerun configure with NODE set" >&2; exit 1; }
	SHINEENC='$(abs_top_builddir)/shineenc' SHINE_JS='$(abs_top_builddir)/$(JS_DIST)/shine.mjs' \
	  $(NODE) $(srcdir)/tests/wasm.mjs
else
wasm check-wasm:
	@echo "No WebAssembly toolchain was found: rerun configure with WASM_CC set to a clang with wasm-ld in its PATH and WASI_SYSROOT to a wasi-libc sysroot." >&2; exit 1
endif

.PHONY: wasm check-wasm

dist-hook:
	rm -rf $(distdir)/$(JS_DIST) $(distdir)/js/node_modules

clean-local:
	-rm -rf $(JS_DIST)
