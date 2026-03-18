NGINX_DIR  := nginx
MODULE_DIR := $(CURDIR)
NGINX_BIN  := $(NGINX_DIR)/objs/nginx
OUT_BIN    := out/sbin/nginx
NPROCS     := $(shell nproc)

SRCS := $(wildcard $(MODULE_DIR)/*.c)
HDRS := $(wildcard $(MODULE_DIR)/*.h)

.PHONY: all build test start configure clean

# Default: configure (if needed), build, and run tests
all: test

# Build the nginx binary with the httplite module
build: $(OUT_BIN)

# Auto-configure if objs/Makefile doesn't exist yet
$(NGINX_DIR)/objs/Makefile: config
	cd $(NGINX_DIR) && auto/configure \
		--prefix="$(MODULE_DIR)/$(NGINX_DIR)/out" \
		--without-http_rewrite_module \
		--add-module="$(MODULE_DIR)" \
		--with-debug

# Compile nginx (nginx's generated Makefile tracks .c/.h deps via ADDON_DEPS)
$(NGINX_BIN): $(NGINX_DIR)/objs/Makefile $(SRCS) $(HDRS)
	$(MAKE) -C $(NGINX_DIR)

# Copy the binary to out/sbin for tests and start
$(OUT_BIN): $(NGINX_BIN)
	@mkdir -p $(dir $@)
	cp -f $< $@

# Run the test suite
test: $(OUT_BIN)
	cd test && TEST_HTTPLITE_BINARY=../$(OUT_BIN) prove -j $(NPROCS) .

# Build and start nginx with nginx.conf (the old 'make' behavior)
start: $(OUT_BIN)
	-@pkill -f 'nginx.*httplite' 2>/dev/null; sleep 0.5
	$(OUT_BIN) -c $(MODULE_DIR)/nginx.conf

# Force reconfigure
configure:
	cd $(NGINX_DIR) && auto/configure \
		--prefix="$(MODULE_DIR)/$(NGINX_DIR)/out" \
		--without-http_rewrite_module \
		--add-module="$(MODULE_DIR)" \
		--with-debug

clean:
	-$(MAKE) -C $(NGINX_DIR) clean
	rm -rf out
