include make/tpl.mk

test: unittest
	./test_runner

uidev: ui/layout.html ui/dist/index.css ui/dist/index.js
	@mkdir -p ui/dist
	$(call compose,ui/layout.html,make/html.map,ui/dist/index.html)
	@echo "Built test version → ui/dist/index.html"

hwdev: ui/layout.html ui/dist/index.css ui/dist/index.js $(wildcard src/*.c) $(wildcard src/*.h)
	@mkdir -p ui/dist
	sed -i 's/^const testing = true;/const testing = false;/' ui/dist/index.js
	$(call compose,ui/layout.html,make/html.map,ui/dist/index.html)
	gzip -k -9 -f ui/dist/index.html
	xxd -i ui/dist/index.html.gz > src/ui_dist_index_html_gz.h
	cd build && cmake -DPICO_BOARD=pico2_w .. && $(MAKE)
	@echo "Built test version → ui/dist/index.html"

build: ui/layout.html ui/dist/index.css ui/dist/index.js $(wildcard src/*.c) $(wildcard src/*.h)
	@mkdir -p ui/dist
	sed -i '/\/\/testing$$/d' ui/dist/index.js
	$(call compose,ui/layout.html,make/html.map,ui/dist/index.html)
	gzip -k -9 -f ui/dist/index.html
	xxd -i ui/dist/index.html.gz > src/ui_dist_index_html_gz.h
	cd build && cmake -DPICO_BOARD=pico2_w .. && $(MAKE)
	@echo "Built test version → ui/dist/index.html"

unittest: src/config/config_iterator.c tests/test_config_iterator.c tests/unity.c
	gcc -std=c11 -Wall -Wextra -Werror -DUNIT_TEST -Isrc -Itests $^ -o test_runner

ui/dist/index.css: ui/layout.css ui/content/network.css ui/content/command.css ui/content/graph.css ui/content/settings.css
	@mkdir -p ui/dist
	$(call compose,ui/layout.css,make/css.map,ui/dist/index.css)
	@echo "Built test version → ui/dist/index.css"

ui/dist/index.js: ui/layout.js ui/content/network.js ui/content/command.js ui/content/graph.js ui/content/components.js ui/content/settings.js
	@mkdir -p ui/dist
	$(call compose,ui/layout.js,make/js.map,ui/dist/index.js)
# 	sed -i '/\/\/testing$$/d' ui/dist/index.js
	@echo "Built test version → ui/dist/index.js"

ui/content/components.js: $(wildcard ui/content/components/*.js)
	$(call compose,ui/content/components/components.js,make/js-comp.map,ui/content/components.js)
	@echo "Built test version → ui/content/components.js"

flash:
	cp build/twitcher.uf2 /media/anro/RP2350/twitcher.uf2 

clean:
	rm -rf ui/dist
	rm ui/content/components.js
	rm -f test_runner
