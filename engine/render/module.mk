# SPDX-License-Identifier: MIT
ifeq ($(TR64_LEVEL_LOADER),1)
TR64_ENGINE_SOURCES += engine/render/geo.c engine/render/render.c engine/render/sprite.c engine/render/scene.c
TR64_ENGINE_SOURCES += engine/render/geo_load.c engine/render/bind_load.c engine/render/load.c
TR64_ENGINE_SOURCES += engine/render/texture.c engine/render/texture_load.c
TR64_ENGINE_SOURCES += engine/render/items.c
TR64_CODE_UNIT_FRAGMENTS += engine/render/load.def
TR64_RENDER_SCENE ?= test/scenes/m1-level1-start.scene.json
TR64_GENERATED_HEADERS += $(GEN)/render_pose.h
$(GEN)/render_pose.h: engine/render/pose.py $(TR64_RENDER_SCENE) $(TR64_RENDER_SPLIT_CAMERA) FORCE
	$(RTK) $(PYTHON) engine/render/pose.py "$(TR64_RENDER_SCENE)" $@ \
	  $(if $(TR64_RENDER_SPLIT_CAMERA),--split-camera "$(TR64_RENDER_SPLIT_CAMERA)") \
	  $(if $(filter 1,$(TR64_RENDER_HALF_VIEW)),--half-view) \
	  $(if $(filter 1,$(TR64_RENDER_MEASURE)),--measure)
endif
