include make/tpl.mk

# Build targets. One UI source, three builds, selected by comment markers:
#
#   //online-start … //online-end, //online    talks to the controller (WebSocket)
#   //offline-start … //offline-end, //offline  stands in for the controller (canned data)
#   //debug                                      console logging
#
#   uidev  browser-only. Online code stripped; offline stand-ins and logging kept.
#          This is the GitHub Pages demo.
#   uihw   the page the release firmware serves, without building the firmware.
#          Offline stand-ins and logging stripped.
#   hwdev  firmware with the debug UI. Offline stand-ins stripped; logging kept.
#   build  release firmware, serving the uihw page.

# Compose the UI into ui/dist/. Always recomposed: every target strips markers
# from ui/dist/index.js in place, so a file-timestamp rule would let one build
# reuse another's stripped copy (e.g. `make uidev` after `make build` would ship
# the hardware UI).
define compose_ui
	@mkdir -p ui/dist
	$(call compose,ui/layout.css,make/css.map,ui/dist/index.css)
	$(call compose,ui/layout.js,make/js.map,ui/dist/index.js)
endef

# Strip a //NAME-start … //NAME-end block and every //NAME line from the JS.
strip_markers = @sed -i -e '/\/\/$(1)-start$$/,/\/\/$(1)-end$$/d' -e '/\/\/$(1)$$/d' ui/dist/index.js

# Gzip the composed page into a C header and build the firmware.
define firmware
	gzip -k -9 -f ui/dist/index.html
	xxd -i ui/dist/index.html.gz > src/ui_dist_index_html_gz.h
	cd build && cmake -DPICO_BOARD=pico2_w .. && $(MAKE)
endef

.PHONY: test ui-test protocol-check uidev uihw hwdev build flash clean

# Unit tests, plus the checks that keep PROTOCOL.md and the C/JS constants honest.
test: protocol-check test_runner test_protocol test_ws_frame
	./test_runner
	./test_protocol
	./test_ws_frame

protocol-check:
	@awk -f make/check-constants.awk src/protocol.h ui/content/protocol.js

uidev: ui/content/components.js
	$(compose_ui)
	$(call strip_markers,online)
	$(call compose,ui/layout.html,make/html.map,ui/dist/index.html)
	@echo "Built browser-only UI → ui/dist/index.html"

uihw: ui/content/components.js
	$(compose_ui)
	$(call strip_markers,offline)
	$(call strip_markers,debug)
	$(call compose,ui/layout.html,make/html.map,ui/dist/index.html)
	@echo "Built hardware UI → ui/dist/index.html"

hwdev: ui/content/components.js
	$(compose_ui)
	$(call strip_markers,offline)
	$(call compose,ui/layout.html,make/html.map,ui/dist/index.html)
	$(firmware)
	@echo "Built debug firmware → build/"

build: uihw
	$(firmware)
	@echo "Built release firmware → build/"

# Drive the hardware UI in a headless browser against a fake device, then check
# every packet it sent with the firmware's own parsing code. Needs Node and
# Playwright (see .github/workflows/test.yml).
ui-test: uihw check_packets
	node tests/ui_packets.cjs ui/dist/index.html ui/dist/packets.txt
	./check_packets ui/dist/packets.txt

TEST_CFLAGS = -std=c11 -Wall -Wextra -Werror -DUNIT_TEST -Isrc -Itests

test_runner: src/config/config_iterator.c tests/test_config_iterator.c tests/unity.c
	gcc $(TEST_CFLAGS) $^ -o $@

test_protocol: src/config/config_iterator.c src/program.c tests/test_protocol.c tests/unity.c tests/protocol_examples.h src/protocol.h src/program.h
	gcc $(TEST_CFLAGS) $(filter %.c,$^) -o $@

check_packets: src/config/config_iterator.c src/program.c tests/check_packets.c src/protocol.h src/program.h
	gcc $(TEST_CFLAGS) $(filter %.c,$^) -o $@

test_ws_frame: src/networking/ws_frame.c tests/test_ws_frame.c tests/unity.c src/networking/ws_frame.h
	gcc $(TEST_CFLAGS) $(filter %.c,$^) -o $@

# The `hex example=` / `fifo example=` blocks in PROTOCOL.md, as C arrays.
tests/protocol_examples.h: PROTOCOL.md make/examples.awk
	awk -f make/examples.awk PROTOCOL.md > $@.tmp && mv $@.tmp $@

ui/content/components.js: $(wildcard ui/content/components/*.js)
	$(call compose,ui/content/components/components.js,make/js-comp.map,ui/content/components.js)

flash:
	cp build/twitcher.uf2 /media/anro/RP2350/twitcher.uf2

clean:
	rm -rf ui/dist
	rm -f ui/content/components.js
	rm -f test_runner test_protocol test_ws_frame check_packets tests/protocol_examples.h
