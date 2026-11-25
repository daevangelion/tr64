# SPDX-License-Identifier: MIT
ifeq ($(TR64_LEVEL_LOADER),1)
TR64_ENGINE_SOURCES += engine/res/load.c engine/res/view.c engine/res/pack.c engine/res/level.c engine/res/validate.c engine/res/tex.c engine/res/load-unit.c
TR64_ENGINE_SOURCES += engine/res/stream.c engine/res/query.c engine/res/sound.c engine/res/image.c
TR64_CODE_UNIT_FRAGMENTS += engine/res/load.def
TR64_GENERATED_HEADERS += $(GEN)/core_schema.h $(GEN)/level_pack.h
$(GEN)/core_schema.h: tools/cook/n64_core.py tools/cook/ir.py tools/cook/n64_validate.py
	$(RTK) $(PYTHON) tools/cook/n64_core.py schema --out $@
$(GEN)/level_pack.h: tools/cook/n64_bind.py FORCE
	$(RTK) $(PYTHON) tools/cook/n64_bind.py --pack "$(TR64_LEVEL_PACK)" --out $@
ifneq ($(strip $(TR64_LEVEL_PACK)),)
TR64_ROM_DEPS += $(TR64_LEVEL_PACK)
TR64_ROM_FILES += --offset 1048576 "$(TR64_LEVEL_PACK)"
endif
endif
