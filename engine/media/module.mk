# SPDX-License-Identifier: MIT
ifeq ($(TR64_LEVEL_LOADER),1)
TR64_ENGINE_SOURCES += engine/snd/music.c engine/snd/music_decode.c engine/snd/music_format.c
TR64_ENGINE_SOURCES += engine/media/cinema.c engine/media/transition.c engine/media/film.c engine/media/film_audio.c
TR64_CART_SOURCES += plat/n64/film.c
TR64_HOST_SOURCES += plat/host/film.c
TR64_TR1_CODE_UNIT_FRAGMENTS += engine/media/video.def
endif
.PHONY: media-source-test media-transport-test media-transition-test media-arena-test media-cook-test
media-source-test:
	$(RTK) env PYTHONPATH=tools/cook $(PYTHON) test/media/test_source.py
media-transport-test:
	$(RTK) env PYTHONPATH=tools/cook $(PYTHON) test/media/test_transport.py --out $(BUILD_DIR)/media-transport
media-transition-test:
	@mkdir -p $(BUILD_DIR)/media-transition
	$(RTK) $(HOST_CC) -std=c99 -O2 -g $(WARN) -I. -Iinclude -D_POSIX_C_SOURCE=200809L test/media/transition.c engine/media/transition.c -o $(BUILD_DIR)/media-transition/transition
	$(RTK) $(BUILD_DIR)/media-transition/transition engine/in/play.c
media-arena-test:
	@mkdir -p $(BUILD_DIR)/media-arena
	$(RTK) $(HOST_CC) -std=c99 -O2 -g $(WARN) -I. -Iinclude -D_POSIX_C_SOURCE=200809L test/media/arena.c -o $(BUILD_DIR)/media-arena/arena
	$(RTK) $(BUILD_DIR)/media-arena/arena
media-cook-test:
	$(RTK) env PYTHONPATH=tools/cook $(PYTHON) test/media/test_video_cook.py
