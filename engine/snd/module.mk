# SPDX-License-Identifier: MIT
ifeq ($(TR64_LEVEL_LOADER),1)
# Pinned SDK queue-depth observability uses its explicitly preview audio API.
LIBDRAGON_PREVIEW := 2
TR64_ENGINE_SOURCES += engine/snd/snd.c engine/snd/format.c engine/snd/spatial.c
TR64_HOST_SOURCES += plat/host/audio.c
TR64_CART_SOURCES += plat/n64/audio.c
endif
.PHONY: snd-test snd-source-test
snd-source-test:
	$(RTK) env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_source.py --out $(BUILD_DIR)/snd-source
snd-test: snd-source-test
	$(RTK) env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_native.py --out $(BUILD_DIR)/snd-native
