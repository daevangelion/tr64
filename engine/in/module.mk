# SPDX-License-Identifier: MIT
.PHONY: input-test play-test pace-test
$(BUILD_DIR)/input-test: engine/in/in.c engine/in/test-input.c engine/in/input.h include/tr64/in.h include/tr64/game.h
	@$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -std=c99 -O2 $(WARN) -I. -Iinclude engine/in/in.c engine/in/test-input.c -o $@
input-test: $(BUILD_DIR)/input-test
	$(RTK) $<
# The play loop's accumulator against a scripted clock, with the legacy one-tick-per-frame loop as the negative control.
$(BUILD_DIR)/pace-test: engine/in/pace.h engine/in/test-pace.c engine/core/frame.h include/tr64/limits.h
	@$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -std=c99 -O2 $(WARN) -I. -Iinclude engine/in/test-pace.c -o $@
pace-test: $(BUILD_DIR)/pace-test
	$(RTK) $<
# The play loop runs against synthetic game/platform services but the real slot picker, save glue and
# medium (over a RAM device); 32-bit like the other host medium tests (code-unit calls pass a 32-bit address).
PLAY_TEST_REAL := engine/ui/picker.c engine/save/game.c engine/save/dispatch.c engine/save/medium.c engine/res/filetable.c
$(BUILD_DIR)/play-test: engine/in/play.c engine/in/in.c engine/in/test-play.c engine/in/input.h engine/in/pace.h engine/in/play.h engine/snd/snd.h include/tr64/game.h include/tr64/render.h include/tr64/pack.h $(PLAY_TEST_REAL) engine/ui/picker.h engine/save/game.h engine/save/save.h engine/save/internal.h $(GEN)/build_id.h $(GEN)/core_schema.h
	@$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g $(WARN) -DTR64_HOST=1 -I. -Iinclude -I$(GEN) engine/in/play.c engine/in/in.c engine/in/test-play.c $(PLAY_TEST_REAL) -o $@
play-test: $(BUILD_DIR)/play-test
	$(RTK) $<
	$(RTK) $< --invalid-carry
	$(RTK) $< --preview
	$(RTK) $< --coop
	$(RTK) $< --source-load
	$(RTK) $< --load-unusable
	$(RTK) $< --load-refused
	$(RTK) $< --load-unconfirmed
	$(RTK) $< --end-from-menu
	$(RTK) $< --pace
	$(RTK) $< --slow 50
	$(RTK) $< --slow 100
	$(RTK) $< --slow 130
	$(RTK) $< --slow 50 --source-load
ifeq ($(TR64_LEVEL_LOADER),1)
ifneq ($(TR64_GAME),3)
TR64_ENGINE_SOURCES += engine/in/wall.c
endif
endif
# Explicit opt-in keeps pack-free shipping boot/negative-control builds intact.
ifeq ($(TR64_PLAY),1)
ifeq ($(TR64_TEST),1)
ifeq ($(filter test/play/coop.c,$(TR64_SCENE_TEST_SOURCES)),)
$(error TR64_PLAY with TR64_TEST requires the explicit co-op play probe)
endif
endif
ifneq ($(filter test/play/coop.c,$(TR64_SCENE_TEST_SOURCES)),)
TR64_CART_INPUTS += $(GEN)/gate_place.h
endif
ifneq ($(TR64_LEVEL_LOADER),1)
$(error TR64_PLAY requires TR64_LEVEL_LOADER=1)
endif
ifeq ($(strip $(TR64_LEVEL_PACK)),)
$(error TR64_PLAY requires TR64_LEVEL_PACK)
endif
ifeq ($(TR64_GAME),2)
TR64_CART_SOURCES += engine/in/play-tr2.c
else ifeq ($(TR64_GAME),3)
TR64_CART_SOURCES += engine/in/play-tr3.c
else
TR64_CART_SOURCES += engine/in/play.c
endif
TR64_CART_SOURCES += plat/n64/play-message.c
endif
