# SPDX-License-Identifier: MIT
SND_MIXER_SOURCES := test/snd/mixer.c engine/snd/snd.c engine/snd/format.c engine/snd/spatial.c plat/host/audio.c engine/res/view.c engine/res/stream.c engine/res/filetable.c engine/io/io.c engine/mem/budget.c engine/mem/pool.c engine/core/log.c engine/core/fmt.c
$(BUILD_DIR)/snd-mixer-test: $(SND_MIXER_SOURCES) engine/snd/snd.h engine/snd/format.h engine/snd/spu.h $(GEN)/core_schema.h
	@rtk mkdir -p $(dir $@)
	rtk $(HOST_CC) -m32 -std=c99 -O2 $(WARN) -I. -Iinclude -I$(GEN) $(SND_MIXER_SOURCES) -o $@
.PHONY: snd-mixer-test snd-measure
snd-mixer-test: snd-test $(BUILD_DIR)/snd-mixer-test
	rtk env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_mixer.py --harness $(BUILD_DIR)/snd-mixer-test --fixtures $(BUILD_DIR)/snd-native --out $(BUILD_DIR)/snd-mixer
	rtk env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_mixer.py --pitch 115 --harness $(BUILD_DIR)/snd-mixer-test --fixtures $(BUILD_DIR)/snd-native --out $(BUILD_DIR)/snd-pitch115
	rtk env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_mixer.py --pitch 140 --harness $(BUILD_DIR)/snd-mixer-test --fixtures $(BUILD_DIR)/snd-native --out $(BUILD_DIR)/snd-pitch140
.PHONY: snd-mode-test
snd-mode-test: snd-test $(BUILD_DIR)/snd-mixer-test
	rtk env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_modes.py --harness $(BUILD_DIR)/snd-mixer-test --fixtures $(BUILD_DIR)/snd-native --out $(BUILD_DIR)/snd-modes
$(BUILD_DIR)/snd-mixer-off-test: $(SND_MIXER_SOURCES) test/snd/off.c engine/snd/snd.h engine/snd/format.h engine/snd/spu.h $(GEN)/core_schema.h
	@rtk mkdir -p $(dir $@)
	rtk $(HOST_CC) -m32 -std=c99 -O2 $(WARN) -I. -Iinclude -I$(GEN) $(SND_MIXER_SOURCES) test/snd/off.c -o $@
.PHONY: snd-off-test
snd-off-test: snd-test $(BUILD_DIR)/snd-mixer-off-test
	rtk env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_mixer.py --muted --harness $(BUILD_DIR)/snd-mixer-off-test --fixtures $(BUILD_DIR)/snd-native --out $(BUILD_DIR)/snd-mixer-off
.PHONY: snd-evidence-test
snd-evidence-test:
	rtk env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_evidence.py --out $(BUILD_DIR)/snd-evidence
snd-measure:
	rtk $(PYTHON) tools/cook/n64_snd_evidence.py --inventory --out $(BUILD_DIR)/snd-inventory.json
$(BUILD_DIR)/snd-music-hooks-test: $(SND_MIXER_SOURCES) test/snd/music-hooks.c engine/snd/snd.h $(GEN)/core_schema.h
	@rtk mkdir -p $(dir $@)
	rtk $(HOST_CC) -m32 -std=c99 -O2 $(WARN) -I. -Iinclude -I$(GEN) $(SND_MIXER_SOURCES) test/snd/music-hooks.c -o $@
.PHONY: snd-music-hooks-test
snd-music-hooks-test: snd-test $(BUILD_DIR)/snd-music-hooks-test
	rtk env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_music_hooks.py --harness $(BUILD_DIR)/snd-music-hooks-test --fixture $(BUILD_DIR)/snd-native/pc.sn64 --out $(BUILD_DIR)/snd-music-hooks
$(BUILD_DIR)/snd-spatial-test: test/snd/spatial.c engine/snd/spatial.c engine/snd/spatial.h $(GEN)/core_schema.h
	rtk $(HOST_CC) -m32 -std=c99 -O2 $(WARN) -I. -Iinclude -I$(GEN) test/snd/spatial.c engine/snd/spatial.c -o $@
.PHONY: snd-spatial-test
snd-spatial-test: $(BUILD_DIR)/snd-spatial-test
	rtk $(PYTHON) test/snd/test_spatial.py --harness $(BUILD_DIR)/snd-spatial-test
.PHONY: snd-ambient-test
snd-ambient-test: snd-test $(BUILD_DIR)/snd-mixer-test
	rtk env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_ambient.py --harness $(BUILD_DIR)/snd-mixer-test --fixtures $(BUILD_DIR)/snd-native --out $(BUILD_DIR)/snd-ambient
.PHONY: snd-stereo-test
snd-stereo-test: snd-test $(BUILD_DIR)/snd-mixer-test
	rtk env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_stereo.py --harness $(BUILD_DIR)/snd-mixer-test --fixtures $(BUILD_DIR)/snd-native --out $(BUILD_DIR)/snd-stereo
SND_QUEUE_SOURCES := test/snd/queue_capacity.c game/sound.c game/math.c game/tables.c $(filter-out test/snd/mixer.c,$(SND_MIXER_SOURCES))
$(BUILD_DIR)/snd-queue-test: $(SND_QUEUE_SOURCES) game/internal.h engine/snd/snd.h engine/snd/format.h $(GEN)/core_schema.h
	@rtk mkdir -p $(dir $@)
	rtk $(HOST_CC) -m32 -std=c99 -O2 $(WARN) -I. -Iinclude -I$(GEN) -DTR64_GAME=2 -DTR64_HOST=1 $(SND_QUEUE_SOURCES) -Wl,--wrap=game_sound_events -o $@
.PHONY: snd-queue-test
snd-queue-test: $(BUILD_DIR)/snd-queue-test
	rtk env PYTHONPATH=tools/cook $(PYTHON) test/snd/test_queue_capacity.py --harness $(BUILD_DIR)/snd-queue-test --out $(BUILD_DIR)/snd-queue
