NGINX_VERSION := 1.22.1
NGINX_DIR     := nginx-$(NGINX_VERSION)
MODULE_DIR    := $(CURDIR)
NGINX_BIN     := $(NGINX_DIR)/objs/nginx
OUT_BIN       := out/sbin/nginx
NPROCS        := $(shell nproc)
NGINX_URL     := https://nginx.org/download/nginx-$(NGINX_VERSION).tar.gz

SRCS := $(wildcard $(MODULE_DIR)/*.c)
HDRS := $(wildcard $(MODULE_DIR)/*.h)

.PHONY: all build test start configure clean real-clean

# Default: configure (if needed), build, and run tests
all: test

# Build the nginx binary with the httplite module
build: $(OUT_BIN)

# Download and unpack nginx source if NGINX_DIR is missing
$(NGINX_DIR)/configure:
	curl -fsSL $(NGINX_URL) -o nginx-$(NGINX_VERSION).tar.gz
	tar xzf nginx-$(NGINX_VERSION).tar.gz
	rm nginx-$(NGINX_VERSION).tar.gz
	mv nginx-$(NGINX_VERSION) $(NGINX_DIR)

# Auto-configure if objs/Makefile doesn't exist yet
$(NGINX_DIR)/objs/Makefile: config $(NGINX_DIR)/configure
	cd $(NGINX_DIR) && ./configure \
		--prefix="$(MODULE_DIR)/$(NGINX_DIR)/out" \
		--without-http_rewrite_module \
		--add-module="$(MODULE_DIR)" \
		--with-debug

# Compile nginx (nginx's generated Makefile tracks .c/.h deps via ADDON_DEPS)
$(NGINX_BIN): $(NGINX_DIR)/objs/Makefile $(SRCS) $(HDRS)
	$(MAKE) -j $(NPROCS) -C $(NGINX_DIR)

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
configure: $(NGINX_DIR)/configure
	cd $(NGINX_DIR) && ./configure \
		--prefix="$(MODULE_DIR)/$(NGINX_DIR)/out" \
		--without-http_rewrite_module \
		--add-module="$(MODULE_DIR)" \
		--with-debug

clean:
	-$(MAKE) -C $(NGINX_DIR) clean
	rm -r out

real-clean: clean
	rm -r $(NGINX_DIR)
