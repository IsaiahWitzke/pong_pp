CLIENT_SRC := src/client/main.cpp
CLIENT_OUT := web/pong.wasm

# --target=wasm32        : output WebAssembly
# -nostdlib              : no libc / libc++ (we are freestanding)
# -fno-exceptions/-rtti  : drop C++ runtime features that need stdlib
# -O2                    : release optimization
# --no-entry             : there is no main(); the module is library-style
# --export-dynamic       : honor our __attribute__((export_name(...)))
# --allow-undefined      : let JS provide imports at instantiation time
CLIENT_CXXFLAGS := --target=wasm32 \
                   -nostdlib \
                   -fno-exceptions \
                   -fno-rtti \
                   -O2 \
                   -Wl,--no-entry \
                   -Wl,--export-dynamic \
                   -Wl,--allow-undefined

client: $(CLIENT_OUT)

$(CLIENT_OUT): $(CLIENT_SRC)
	@mkdir -p web
	clang++ $(CLIENT_CXXFLAGS) -o $@ $<
