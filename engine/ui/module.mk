# SPDX-License-Identifier: MIT
ifeq ($(TR64_LEVEL_LOADER),1)
TR64_ENGINE_SOURCES += $(wildcard engine/ui/*.c)
TR64_HOST_SOURCES += $(wildcard plat/host/ui*.c)
TR64_CART_SOURCES += $(wildcard plat/n64/ui*.c)
TR64_TR1_CODE_UNIT_FRAGMENTS += engine/ui/front.def
TR64_TR2_CODE_UNIT_FRAGMENTS += engine/ui/front.def
TR64_UI_LANGUAGE ?= engine/ui/en.json
TR64_GENERATED_HEADERS += $(GEN)/ui_string_ids.h $(GEN)/ui_string_data.h $(GEN)/ui_font_data.h
$(GEN)/ui_string_ids.h $(GEN)/ui_string_data.h $(GEN)/ui_font_data.h &: tools/cook/n64_ui_strings.py engine/ui/en.json engine/ui/font.json $(TR64_UI_LANGUAGE) FORCE
	$(RTK) $(PYTHON) tools/cook/n64_ui_strings.py --schema engine/ui/en.json --language $(TR64_UI_LANGUAGE) --font engine/ui/font.json --ids $(GEN)/ui_string_ids.h --data $(GEN)/ui_string_data.h --font-header $(GEN)/ui_font_data.h
endif
.PHONY: ui-test
$(BUILD_DIR)/ui-test: test/ui/draw-test.c engine/ui/draw.c engine/ui/strings.c engine/ui/ui.h include/tr64/game.h $(GEN)/core_schema.h $(GEN)/build_id.h $(GEN)/ui_string_ids.h $(GEN)/ui_string_data.h $(GEN)/ui_font_data.h
	$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g -Wall -Wextra -Werror -DTR64_HOST=1 -I. -Iinclude -I$(GEN) test/ui/draw-test.c engine/ui/draw.c engine/ui/strings.c -o $@
$(BUILD_DIR)/ui-backend-test: test/ui/backend-test.c plat/host/ui.c engine/ui/ui.h $(GEN)/core_schema.h $(GEN)/build_id.h
	$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g -Wall -Wextra -Werror -DTR64_HOST=1 -I. -Iinclude -I$(GEN) test/ui/backend-test.c plat/host/ui.c -o $@
$(BUILD_DIR)/picker-test: test/ui/picker-test.c engine/ui/picker.c engine/ui/picker.h engine/ui/front.h engine/save/game.c engine/save/game.h engine/save/dispatch.c engine/save/medium.c engine/save/save.h engine/save/internal.h engine/res/filetable.c include/tr64/game.h $(GEN)/core_schema.h
	$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g -Wall -Wextra -Werror -DTR64_HOST=1 -I. -Iinclude -I$(GEN) test/ui/picker-test.c engine/ui/picker.c engine/save/game.c engine/save/dispatch.c engine/save/medium.c engine/res/filetable.c -o $@
# The profiler overlay's accounting against a scripted clock (and its negative control).
$(BUILD_DIR)/perf-test: test/ui/perf-test.c engine/ui/perf.c engine/ui/perf.h engine/ui/ui.h include/tr64/render.h include/tr64/gpu.h $(GEN)/core_schema.h
	$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g -Wall -Wextra -Werror -DTR64_HOST=1 -I. -Iinclude -I$(GEN) test/ui/perf-test.c engine/ui/perf.c -o $@
.PHONY: perf-test
perf-test: $(BUILD_DIR)/perf-test
	$(RTK) $(BUILD_DIR)/perf-test
	$(RTK) $(BUILD_DIR)/perf-test --negative
$(BUILD_DIR)/ui-rumble-test: test/ui/rumble-test.c engine/ui/frontend.c engine/ui/frontend.h include/tr64/eng.h include/tr64/game.h $(GEN)/core_schema.h
	$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g -Wall -Wextra -Werror -DTR64_HOST=1 -I. -Iinclude -I$(GEN) -ffunction-sections -Wl,--gc-sections test/ui/rumble-test.c engine/ui/frontend.c -o $@
ui-test: perf-test
ui-test: $(BUILD_DIR)/ui-rumble-test
ui-test: $(BUILD_DIR)/ui-test $(BUILD_DIR)/ui-backend-test $(BUILD_DIR)/play-test $(BUILD_DIR)/ui-format-test $(BUILD_DIR)/picker-test
	$(RTK) $(BUILD_DIR)/ui-test
	$(RTK) $(BUILD_DIR)/ui-backend-test
	$(RTK) $(PYTHON) test/ui/strings-test.py --out $(BUILD_DIR)/ui-strings-test
	$(RTK) $(BUILD_DIR)/play-test --invalid-ui
	$(RTK) $(BUILD_DIR)/ui-format-test
	$(RTK) $(BUILD_DIR)/picker-test
	$(RTK) $(BUILD_DIR)/ui-rumble-test
.PHONY: ui-image-test
ui-image-test: $(BUILD_DIR)/ui-image-backend-test
	$(RTK) env PYTHONPATH=tools/cook $(PYTHON) test/ui/image-test.py --out $(BUILD_DIR)/ui-image-test
	$(RTK) $(BUILD_DIR)/ui-image-backend-test
$(BUILD_DIR)/ui-image-backend-test: test/ui/image-backend-test.c engine/ui/image.c plat/host/ui-image.c engine/ui/image.h $(GEN)/core_schema.h
	$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g -Wall -Wextra -Werror -DTR64_HOST=1 -I. -Iinclude -I$(GEN) test/ui/image-backend-test.c engine/ui/image.c plat/host/ui-image.c -o $@
$(BUILD_DIR)/ui-format-test: test/ui/format-test.c engine/ui/format.c engine/ui/format.h engine/ui/strings.c $(GEN)/core_schema.h $(GEN)/ui_string_ids.h $(GEN)/ui_string_data.h
	$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g -Wall -Wextra -Werror -DTR64_HOST=1 -I. -Iinclude -I$(GEN) test/ui/format-test.c engine/ui/format.c engine/ui/strings.c -o $@
.PHONY: ui-pose-test
ui-pose-test:
	$(RTK) $(PYTHON) test/ui/pose-test.py --out $(BUILD_DIR)/ui-pose-test
ui-test: ui-pose-test
