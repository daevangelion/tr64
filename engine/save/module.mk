# SPDX-License-Identifier: MIT
ifeq ($(TR64_LEVEL_LOADER),1)
TR64_ENGINE_SOURCES += $(wildcard engine/save/*.c)
TR64_HOST_SOURCES += $(wildcard plat/host/save*.c)
TR64_CART_SOURCES += $(wildcard plat/n64/save*.c)
TR64_CODE_UNIT_FRAGMENTS += engine/save/medium.def
ifneq ($(filter test/ui/save-write.c test/ui/save-read.c,$(TR64_SCENE_TEST_SOURCES)),)
TR64_CART_SOURCES += test/ui/save-native.c
TR64_CART_LIBS += --wrap main
endif
endif

.PHONY: save-test
$(BUILD_DIR)/save-test: test/ui/save-test.c engine/save/medium.c engine/save/dispatch.c engine/save/save.h engine/save/internal.h engine/res/filetable.c
	$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g -Wall -Wextra -Werror -DTR64_HOST=1 -I. -Iinclude test/ui/save-test.c engine/save/medium.c engine/save/dispatch.c engine/res/filetable.c -o $@
$(BUILD_DIR)/save-host-test: test/ui/save-host.c engine/save/medium.c engine/save/dispatch.c plat/host/save.c engine/save/save.h engine/save/internal.h engine/res/filetable.c
	$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -DTR64_HOST=1 -I. -Iinclude test/ui/save-host.c engine/save/medium.c engine/save/dispatch.c plat/host/save.c engine/res/filetable.c -o $@
save-test: $(BUILD_DIR)/save-test $(BUILD_DIR)/save-host-test
	$(RTK) $(BUILD_DIR)/save-test
	$(RTK) $(PYTHON) test/ui/save-host-test.py --binary $(abspath $(BUILD_DIR)/save-host-test) --out $(BUILD_DIR)/save-host-test-data
