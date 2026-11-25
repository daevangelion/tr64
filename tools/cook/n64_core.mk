# SPDX-License-Identifier: MIT
CORE_TEST_SOURCES := tools/cook/core_test.c engine/res/load.c engine/res/view.c engine/res/pack.c engine/res/level.c engine/res/validate.c engine/res/tex.c engine/res/load-unit.c engine/res/filetable.c engine/io/io.c engine/mem/budget.c engine/mem/pool.c engine/core/log.c engine/code/code.c engine/code/init.c engine/code/test.c plat/host/sys.c
CORE_TEST_SOURCES += engine/res/stream.c engine/res/query.c engine/res/sound.c engine/res/image.c
CORE_TEST_SOURCES += engine/snd/format.c engine/snd/snd.c plat/host/audio.c
CORE_TEST_SOURCES += tools/cook/catalog_test.c
CORE_TEST_SOURCES += engine/core/fmt.c
CORE_TEST_SOURCES += engine/save/dispatch.c engine/save/identity.c
.PHONY: n64-catalog-tests
n64-catalog-tests: $(BUILD_DIR)/core-test
	rtk $(PYTHON) tools/cook/test_n64_catalog.py --harness $(BUILD_DIR)/core-test --out $(BUILD_DIR)/catalog-tests --cache $(BUILD_DIR)/compressor
	rtk $(PYTHON) tools/cook/test_pack_level_music.py --cache $(BUILD_DIR)/compressor
# The standalone harness links no Game/renderer modules. Its dispatch registry
# must describe those linked objects, not the production module-fragment list.
CORE_TEST_GEN := $(BUILD_DIR)/core-test-generated
$(CORE_TEST_GEN)/units.json $(CORE_TEST_GEN)/units.h &: engine/code/units.def engine/code/units.py engine/res/load.def
	rtk $(PYTHON) engine/code/units.py engine/code/units.def $(CORE_TEST_GEN)/units.json $(CORE_TEST_GEN)/units.h engine/res/load.def
$(BUILD_DIR)/core-test: $(CORE_TEST_SOURCES) $(GEN)/build_id.h $(GEN)/core_schema.h $(GEN)/level_pack.h $(CORE_TEST_GEN)/units.h $(GEN)/cart_layout.h
	rtk $(HOST_CC) -m32 -std=c99 -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Wl,--gc-sections,--wrap=tr64_level_pack_bind_streams -D_POSIX_C_SOURCE=200809L -DTR64_HOST=1 -I$(CORE_TEST_GEN) $(COMMON) $(CORE_TEST_SOURCES) -o $@
.PHONY: n64-core-tests n64-core-measure n64-pack-tests
n64-pack-tests:
	rtk $(PYTHON) tools/cook/test_n64_pack.py --cache $(BUILD_DIR)/compressor
.PHONY: n64-image-tests
n64-image-tests: $(BUILD_DIR)/core-test
	rtk $(PYTHON) tools/cook/test_n64_image.py --harness $(BUILD_DIR)/core-test --out $(BUILD_DIR)/image-tests --cache $(BUILD_DIR)/compressor
n64-core-tests: $(BUILD_DIR)/core-test n64-pack-tests tr2-material-test
	rtk $(PYTHON) tools/cook/n64_core_tests.py --harness $(BUILD_DIR)/core-test --out $(BUILD_DIR)/core-tests --cache $(BUILD_DIR)/compressor

.PHONY: tr2-material-test
tr2-material-test: $(GEN)/core_schema.h
	rtk $(HOST_CC) -std=c99 -O2 $(WARN) -I. -Iinclude -I$(GEN) test/run/tr2-material.c \
	  engine/render/bind_load.c engine/render/geo_load.c engine/render/texture_load.c engine/res/view.c -o $(BUILD_DIR)/tr2-material-test
	rtk $(BUILD_DIR)/tr2-material-test
n64-core-measure: $(BUILD_DIR)/core-test
	rtk $(PYTHON) tools/cook/n64_core.py measure --out $(BUILD_DIR)/core-measure --cache $(BUILD_DIR)/compressor
	rtk $(PYTHON) tools/cook/n64_core_tests.py --harness $(BUILD_DIR)/core-test --real $(BUILD_DIR)/core-measure --out $(BUILD_DIR)/core-tests --cache $(BUILD_DIR)/compressor
