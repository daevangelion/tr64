# SPDX-License-Identifier: MIT
# Ported from MIT ac3n64 04134cc; tr64 M0 layout and TLB units are new.
# Module fragments append only (paths relative to the repository):
# TR64_ENGINE_SOURCES: common .c sources; TR64_HOST_SOURCES: host-only .c.
# TR64_CART_SOURCES: cart-only .c/.S; TR64_UNIT_SOURCES: host unit-test .c.
# TR64_CART_LIBS: linker flags/archives before SDK libraries, in dependency order.
# TR64_SDK_DEPS: targets required before cart compilation/linking.
# TR64_CODE_UNIT_FRAGMENTS: units.def-format files, merged with the base list.
# TR64_TR1_CODE_UNIT_FRAGMENTS: units for the first game's cart and both hosts.
# TR64_TR2_CODE_UNIT_FRAGMENTS: units for the second game's cart and both hosts.
# Each unit NAME exports uint32_t tr64_unit_NAME(uint32_t), per engine/code.
# TR64_GENERATED_HEADERS: generated files required before either compile.
# TR64_CART_INPUTS: additional files read by cart flags (e.g. -include or -T).
# TR64_LEVEL_LOADER: 1 (default) permits module level loaders; 0 must omit
# their hook, pack generation and ROM binding; dependent game/level-render
# registrations are omitted too. All level-dependent modules honor this knob.
# TR64_SCENE_TEST_SOURCES: additive common sources for explicit scene probes;
# separate from module registrations, so probes never replace module sources.
# TR64_ROM_FILES: additive n64tool flags/files after SDK defaults (including
# --offset if needed); TR64_ROM_DEPS: file prerequisites for those arguments.
# TR64_LEVEL_PACK: optional external native level pack, consumed by a loader
# module; this is a content input, never a source/PC/PSX implementation variant.
# TR64_HOT_DATA_PHASE: bytes from the 8 KiB boundary at which the hot data
# group starts (plat/n64/hotdata.h holds the shipped value; this overrides it
# for layout experiments, docs/architecture.md section 6.2). TR64_POOL_PHASE:
# likewise for where the pools start relative to the same boundary.
# Fragments may define their own generation/SDK rules using BUILD_DIR, GEN,
# PYTHON and N64_*; they must not replace the variables/targets owned here.
.DEFAULT_GOAL := host
BUILD_DIR ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/tr64-build
BUILD_DIR := $(abspath $(BUILD_DIR))
N64_INST ?= $(HOME)/sdks/n64-sdk-tr64-dde6197a28aa-485fe3ad0dfc
N64_GCCPREFIX ?= $(HOME)/sdks/mips64-toolchain
LIBDRAGON_REV := dde6197a28aaa26b721685bfd5f2eabd32e89aa5
TINY3D_REV := 485fe3ad0dfc5458d84aeac8b41b9f2427e50c71
N64_TLBCODE := 1
# Directory capacity32; code frames, page size and TLB budget are unchanged.
export N64_INST N64_GCCPREFIX
N64_TLBCODE_UNITS := $(BUILD_DIR)/units.json
SCENE ?= test/scenes/boot.route
HOST_CC ?= cc
PYTHON ?= python3
RTK ?= $(if $(shell command -v rtk 2>/dev/null),rtk)
TR64_TEST ?= 0
TR64_PLAY ?= 0
TR64_LEVEL_LOADER ?= 1
TR64_LEVEL_PACK ?=
# D17: one game at cart build time; on host, a level selects its own rows
# at mount. The host build setting still selects the cart used for layout.
TR64_GAME ?= 1
ifneq ($(TR64_GAME),1)
ifneq ($(TR64_GAME),2)
ifneq ($(TR64_GAME),3)
$(error TR64_GAME=$(TR64_GAME): expected 1, 2 or 3)
endif
endif
endif
export TR64_GAME
ENGINE := engine/core/eng.c engine/core/log.c engine/core/rng.c engine/core/fmt.c engine/core/unwind.c engine/core/qr.c engine/mem/budget.c \
 engine/mem/pool.c engine/io/io.c engine/res/filetable.c engine/in/in.c \
 engine/test/route.c engine/test/card.c engine/code/code.c engine/code/init.c engine/code/test.c
WARN := -Wall -Wextra -Werror
COMMON := -I. -Iinclude -I$(BUILD_DIR)/generated -ffp-contract=off -fno-fast-math -DTR64_GAME=$(TR64_GAME)
ROM := $(BUILD_DIR)/tr64.z64
ELF := $(BUILD_DIR)/tr64.elf
GEN := $(BUILD_DIR)/generated
-include $(sort $(wildcard engine/*/module.mk plat/*/module.mk) $(if $(filter 3,$(TR64_GAME)),game/tr3/module.mk,game/module.mk))
TR64_HOST_CODE_UNIT_FRAGMENTS := $(TR64_CODE_UNIT_FRAGMENTS) $(if $(filter 3,$(TR64_GAME)),$(TR64_TR3_CODE_UNIT_FRAGMENTS),$(TR64_TR1_CODE_UNIT_FRAGMENTS) $(TR64_TR2_CODE_UNIT_FRAGMENTS))
ifeq ($(TR64_GAME),1)
TR64_CODE_UNIT_FRAGMENTS += $(TR64_TR1_CODE_UNIT_FRAGMENTS)
ifeq ($(TR64_PLAY)$(TR64_TEST),10)
TR64_CODE_UNIT_FRAGMENTS += $(TR64_TR1_PLAY_CODE_UNIT_FRAGMENTS)
endif
else ifeq ($(TR64_GAME),2)
TR64_CODE_UNIT_FRAGMENTS += $(TR64_TR2_CODE_UNIT_FRAGMENTS)
else
TR64_CODE_UNIT_FRAGMENTS += $(TR64_TR3_CODE_UNIT_FRAGMENTS)
endif
HOST_SOURCES := $(ENGINE) $(TR64_ENGINE_SOURCES) $(TR64_SCENE_TEST_SOURCES) plat/host/sys.c plat/host/main.c engine/test/unit.c $(TR64_HOST_SOURCES) $(TR64_UNIT_SOURCES)
CART_SOURCES := $(ENGINE) $(TR64_ENGINE_SOURCES) $(TR64_SCENE_TEST_SOURCES) plat/n64/sys.c plat/n64/main.c plat/n64/heap.c plat/n64/surfaces.c plat/n64/code.c plat/n64/fatal.c plat/n64/hotdata.c plat/n64/fmt-sdk.c plat/n64/fatal-capture.S plat/n64/start.S $(TR64_CART_SOURCES)
HOST_OBJECTS := $(addprefix $(BUILD_DIR)/host/,$(HOST_SOURCES:.c=.o))
ROM_OBJECTS := $(addprefix $(BUILD_DIR)/cart/,$(patsubst %.S,%.o,$(CART_SOURCES:.c=.o)))
.PHONY: host rom elf unit routes provcheck sdk-check FORCE cook-enabled build-id
FORCE:
build-id: $(GEN)/build_id.h
$(GEN)/build_id.h: FORCE
	@$(RTK) $(PYTHON) test/run/build-id.py --toolchain-prefix "$(N64_GCCPREFIX)" --game $(TR64_GAME) --test $(TR64_TEST) --level-loader $(TR64_LEVEL_LOADER) $(foreach source,$(TR64_SCENE_TEST_SOURCES),--scene-test-source $(source)) -o $@
$(GEN)/route_blob.h: FORCE
	@$(RTK) $(PYTHON) tools/routec/routec.py $(SCENE) --header -o $@
# tlbcode pack updates the ELF build ID in place: calibrate only after pack.
# Missing game-specific unit pages come from an ordinary link of that game.
# The selected cart remains authoritative for the image extent and common pages.
ifeq ($(TR64_LEVEL_LOADER),1)
ifeq ($(TR64_GAME),2)
host_second_game := 1
else ifeq ($(TR64_GAME),1)
ifneq ($(strip $(TR64_TR2_CODE_UNIT_FRAGMENTS)),)
host_second_game := 2
endif
endif
endif
$(GEN)/cart_layout.h: $(ROM) test/run/cart-layout.py test/run/size_gate
	@$(RTK) $(PYTHON) test/run/size_gate $(ELF) --json $(BUILD_DIR)/size.json
ifneq ($(host_second_game),)
	@$(RTK) $(MAKE) BUILD_DIR="$(BUILD_DIR)/tr$(host_second_game)-page-calibration" TR64_GAME=$(host_second_game) TR64_PLAY=0 TR64_TEST=0 \
	  TR64_LEVEL_PACK= TR64_SCENE_TEST_SOURCES= SCENE=test/scenes/boot.route \
	  TR64_RENDER_SCENE=test/scenes/m1-level1-start.scene.json TR64_RENDER_SPLIT_CAMERA= \
	  TR64_RENDER_HALF_VIEW=0 TR64_RENDER_MEASURE=0 elf
	@$(RTK) $(PYTHON) test/run/cart-layout.py $(ELF) $@ "$(BUILD_DIR)/tr$(host_second_game)-page-calibration/tr64.elf"
else
	@$(RTK) $(PYTHON) test/run/cart-layout.py $(ELF) $@
endif
$(BUILD_DIR)/host/%.o: %.c $(GEN)/build_id.h $(GEN)/host/units.h $(GEN)/cart_layout.h $(TR64_GENERATED_HEADERS)
	@$(RTK) mkdir -p $(dir $@)
	$(RTK) $(HOST_CC) -m32 -std=c99 -O2 -g $(WARN) -I$(GEN)/host $(COMMON) -D_POSIX_C_SOURCE=200809L -DTR64_HOST=1 -MMD -MP -c $< -o $@
$(BUILD_DIR)/host/tr64-host: $(HOST_OBJECTS)
	$(RTK) $(HOST_CC) -m32 -o $@ $^
host: $(BUILD_DIR)/host/tr64-host
unit: host
	$(RTK) $(BUILD_DIR)/host/tr64-host --selftest
	$(RTK) $(PYTHON) tools/routec/tests.py
	$(RTK) $(PYTHON) test/run/scene-tests.py
	$(RTK) $(PYTHON) test/run/module-tests.py
routes:
	@$(RTK) mkdir -p $(BUILD_DIR)/routes
	$(RTK) $(PYTHON) tools/routec/routec.py $(SCENE) -o $(BUILD_DIR)/routes/scene.rbc
provcheck:
	$(RTK) $(PYTHON) tools/provcheck/provcheck.py
	$(RTK) $(PYTHON) tools/provcheck/provcheck.py --selftest
-include $(sort $(wildcard tools/cook/*.mk))
cook-enabled:
	@echo '$(TR64_COOK_TARGETS)'

ifeq ($(wildcard $(N64_INST)/include/n64.mk),)
cart-identity:
	@echo 'missing cached N64 SDK: $(N64_INST)' >&2; exit 2
rom elf sdk-check:
	@echo 'missing cached N64 SDK: $(N64_INST)' >&2; exit 2
else
include $(N64_INST)/include/n64.mk
# Check before any SDK prerequisite can run. The compiler family lives in
# the pinned GCC prefix; packaging executables live in the pinned SDK.
cart_gcc_tools := N64_CC:gcc N64_CXX:g++ N64_AS:as N64_LD:ld N64_OBJCOPY:objcopy N64_OBJDUMP:objdump N64_NM:nm N64_SIZE:size N64_STRIP:strip
cart_sdk_tools := N64_TLBCODE_TOOL:tlbcode N64_SYM:n64sym N64_ELFCOMPRESS:n64elfcompress N64_TOOL:n64tool N64_ED64ROMCONFIG:ed64romconfig
ifneq ($(words $(PYTHON)),1)
$(error PYTHON: expected exactly one host interpreter word)
endif
define assert_cart_tool
ifneq ($$(words $$($(1))),1)
$$(error $(1): expected exactly one pinned tool path: $(2))
endif
ifneq ($$(strip $$($(1))),$(strip $(2)))
$$(error $(1): expected exactly one pinned tool path: $(2))
endif
endef
cart_tool_var = $(word 1,$(subst :, ,$(1)))
cart_tool_name = $(word 2,$(subst :, ,$(1)))
$(foreach tool,$(cart_gcc_tools),$(eval $(call assert_cart_tool,$(call cart_tool_var,$(tool)),$(N64_GCCPREFIX)/bin/mips64-elf-$(call cart_tool_name,$(tool)))))
$(foreach tool,$(cart_sdk_tools),$(eval $(call assert_cart_tool,$(call cart_tool_var,$(tool)),$(N64_INST)/bin/$(call cart_tool_name,$(tool)))))
# GNU make command-line variables (and -e environment overrides) can otherwise
# shadow the ELF recipe's target-specific aliases.
$(foreach tool,CC CXX,$(if $(filter command override,$(origin $(tool))),$(eval $(call assert_cart_tool,$(tool),$(N64_$(tool))))))

N64_TOOLFILES += $(TR64_ROM_FILES)
N64_LDFLAGS := $(filter-out %.a,$(TR64_CART_LIBS)) $(N64_LDFLAGS)
N64_CFLAGS += -std=c99 $(WARN) $(COMMON) -DTR64_PLAY=$(TR64_PLAY) -isystem $(N64_INCLUDEDIR) -ftrivial-auto-var-init=uninitialized
N64_CFLAGS += -Werror=unused-variable -Werror=unused-but-set-variable -Werror=unused-function -Werror=unused-parameter -Werror=unused-but-set-parameter -Werror=unused-label -Werror=unused-local-typedefs -Werror=unused-const-variable
N64_LDFLAGS += --wrap malloc --wrap calloc --wrap realloc --wrap memalign --wrap free --wrap sbrk --wrap sbrk_top
N64_LDFLAGS += --wrap _malloc_r --wrap _calloc_r --wrap _realloc_r --wrap _memalign_r --wrap _free_r --wrap _sbrk_r
N64_LDFLAGS += --wrap malloc_usable_size --wrap mallinfo --wrap valloc --wrap pvalloc --wrap realloc_uncached --wrap _valloc_r --wrap _pvalloc_r
N64_LDFLAGS += --wrap _malloc_usable_size_r --wrap _mallinfo_r
# Hot code order: plat/n64/hot.ld places the listed functions first in .text.
N64_LDFLAGS += --section-ordering-file=$(abspath plat/n64/hot.ld)
# One diagnostics path in every profile: compact fatal screen, tr64's own
# integer formatter in place of the SDK's stdio engines (no inspector, console,
# printf or decimal-float code in the image).
N64_LDFLAGS += --wrap debug_assert_func_f
N64_LDFLAGS += --wrap printf --wrap fprintf --wrap vprintf --wrap vfprintf --wrap snprintf --wrap vsnprintf --wrap sprintf --wrap vsprintf
N64_LDFLAGS += --wrap fwrite --wrap fputs --wrap puts --wrap fputc --wrap putchar --wrap setvbuf --wrap abort
N64_ROM_TITLE = "tr64"
# D6:256Kibit/32KiB SRAM (three8KiB game slots + settings/directory8KiB).
N64_ROM_SAVETYPE = $(if $(filter 1 2 3,$(TR64_GAME)),sram256k,none)
N64_ROM_EXPANSIONPAK = unused
sdk-check:
	@$(RTK) $(PYTHON) plat/n64/sdk-check.py "$(N64_INST)/MANIFEST.json" "$(LIBDRAGON_REV)" "$(TINY3D_REV)"
TR64_CART_OMIT_UNITS := $(if $(and $(filter 1,$(TR64_PLAY)),$(filter 0,$(TR64_TEST))),--omit test)
# FORCE also detects removing a fragment; the generator writes only changes.
$(GEN)/units.h $(N64_TLBCODE_UNITS) &: engine/code/units.def engine/code/units.py $(sort $(TR64_CODE_UNIT_FRAGMENTS)) FORCE
	@$(RTK) $(PYTHON) engine/code/units.py $(TR64_CART_OMIT_UNITS) engine/code/units.def $(N64_TLBCODE_UNITS) $(GEN)/units.h $(sort $(TR64_CODE_UNIT_FRAGMENTS))
$(GEN)/host/units.h: engine/code/units.def engine/code/units.py $(sort $(TR64_HOST_CODE_UNIT_FRAGMENTS)) FORCE
	@$(RTK) $(PYTHON) engine/code/units.py engine/code/units.def $(BUILD_DIR)/host-units.json $@ $(sort $(TR64_HOST_CODE_UNIT_FRAGMENTS))
# Source identity remains build-id's existing contract (including for QR).
# The configuration header also captures the effective cart settings, not the
# runner environment. New TR64_*/N64_* build options and registered generated
# headers, source lists and ROM dependencies enter automatically.
.PHONY: cart-identity
cart-identity: $(GEN)/cart_identity.h
cart_headers = $(GEN)/route_blob.h $(GEN)/units.h $(TR64_GENERATED_HEADERS)
cart_link_files = $(filter %.a,$(TR64_CART_LIBS)) plat/n64/hot.ld
cart_sdk_inputs = $(N64_INCLUDEDIR)/ktls.h $(N64_LIBDIR)/$(N64_LINKSCRIPT) \
 $(N64_LIBDIR)/libdragon.a $(N64_LIBDIR)/libdragonsys.a
cart_side_inputs = $(cart_headers) $(N64_TLBCODE_UNITS) $(cart_link_files) $(cart_sdk_inputs) \
 $(TR64_CART_INPUTS) $(TR64_ROM_DEPS)
cart_ldflags_var := $(if $(filter command override,$(origin LDFLAGS)),LDFLAGS,N64_LDFLAGS)
cart_flag_vars = N64_CFLAGS N64_ASFLAGS N64_C_AND_CXX_FLAGS $(cart_ldflags_var) N64_ED64ROMCONFIGFLAGS
define cart_newline


endef
cart_literal_vars = $(cart_flag_vars) N64_TOOLFLAGS N64_TOOLFILES N64_ROM_ELFCOMPRESS TR64_HOT_DATA_PHASE TR64_POOL_PHASE
$(foreach var,$(cart_literal_vars),$(if $(findstring $(cart_newline),$($(var))),$(error $(var): newlines are not permitted)))
cart_config_makefiles := $(MAKEFILE_LIST)
cart_config_vars = $(sort $(filter-out TR64_ARES_% TR64_TIMEOUT_SCALE TR64_SHOT_PRUNE, \
 $(filter TR64_% N64_% LIBDRAGON_% TINY3D_%,$(.VARIABLES))) $(cart_flag_vars) CART_SOURCES COMMON CCACHE D SCENE)
cart_config_inputs = $(SCENE) $(TR64_RENDER_SCENE) $(TR64_RENDER_SPLIT_CAMERA) $(TR64_UI_LANGUAGE) \
 $(cart_side_inputs) $(CART_SOURCES) $(TR64_CODE_UNIT_FRAGMENTS) $(N64_INST)/MANIFEST.json \
 $(foreach tool,$(cart_sdk_tools) $(cart_gcc_tools),$($(call cart_tool_var,$(tool))))
# The generator sees raw flags before any shell expansion. Generated packer
# members belong in TR64_ROM_DEPS.
$(GEN)/cart_identity.h: $(GEN)/build_id.h $(cart_side_inputs) FORCE | $(TR64_SDK_DEPS)
	$(file >$@.vars)$(foreach var,$(cart_config_vars),$(file >>$@.vars,$(var)=$($(var))))
	@$(RTK) $(PYTHON) test/run/cart-config.py --out "$@" --vars "$@.vars" --source-id "$(GEN)/build_id.h" \
	  --root "$(CURDIR)" --build "$(BUILD_DIR)" --sdk "$(N64_INST)" --gcc "$(N64_GCCPREFIX)" \
	  $(foreach var,$(cart_flag_vars),--check-flags $(var)) \
	  $(foreach file,$(cart_config_makefiles),--makefile $(file)) \
	  $(foreach probe,$(TR64_SCENE_TEST_SOURCES),--probe $(probe)) $(cart_config_inputs)
# Changing flags in a reused directory must rebuild the affected objects before
# main can certify them. Apply the same stamp to C and assembly.
$(ROM_OBJECTS): $(GEN)/cart_identity.h

$(BUILD_DIR)/cart/%.o: %.c $(GEN)/build_id.h $(cart_headers) | $(TR64_SDK_DEPS)
	@$(RTK) mkdir -p $(dir $@)
	$(RTK) $(N64_CC) $(N64_CFLAGS) $(if $(filter plat/n64/%,$<),-std=gnu99) -MMD -MP -c $< -o $@
$(BUILD_DIR)/cart/plat/n64/hotdata.o: private N64_CFLAGS += $(if $(TR64_HOT_DATA_PHASE),-DTR64_HOT_DATA_PHASE=$(TR64_HOT_DATA_PHASE)u)
$(BUILD_DIR)/cart/plat/n64/sys.o: private N64_CFLAGS += $(if $(TR64_POOL_PHASE),-DTR64_POOL_PHASE=$(TR64_POOL_PHASE)u)
$(BUILD_DIR)/cart/%.o: %.S $(cart_headers) | $(TR64_SDK_DEPS)
	@$(RTK) mkdir -p $(dir $@)
	$(RTK) $(N64_CC) $(N64_ASFLAGS) -c $< -o $@
$(ELF): CC=$(N64_CC)
$(ELF): CXX=$(N64_CXX)
$(ELF): private LDFLAGS=$(N64_LDFLAGS)
$(ELF): $(ROM_OBJECTS) $(cart_link_files) $(TR64_SDK_DEPS) | sdk-check
elf: $(ELF)
$(ROM): $(ELF) $(TR64_ROM_DEPS) test/run/size_gate test/run/fmt-link-gate.py test/run/hot-check.py tools/cook/pool_rows.py test/run/refs-check.py test/run/tlbcode-refs.allow test/run/tlbcode-refs-test.allow
	$(RTK) $(PYTHON) test/run/size_gate $< --json $(BUILD_DIR)/size.json
	$(RTK) $(PYTHON) test/run/hot-check.py $(BUILD_DIR)/tr64.map plat/n64/hot.ld --check-pools $(if $(TR64_POOL_PHASE),--pool-phase $(TR64_POOL_PHASE)) $(if $(filter 1,$(TR64_LEVEL_LOADER)),--elf $< --objdump $(N64_GCCPREFIX)/bin/mips64-elf-objdump --render-pose $(GEN)/render_pose.h $(if $(and $(filter 1,$(TR64_PLAY)),$(filter 0,$(TR64_TEST))),--play),--no-render-stack)
	$(RTK) $(PYTHON) test/run/fmt-link-gate.py $< --nm $(N64_GCCPREFIX)/bin/mips64-elf-nm --size-json $(BUILD_DIR)/size.json
	$(RTK) $(N64_TLBCODE_TOOL) pack $< $<.tlbcode
	$(RTK) $(PYTHON) test/run/refs-check.py $<.tlbcode.refs test/run/tlbcode-refs.allow $(if $(filter plat/n64/fatal-nested-probe.c,$(TR64_SCENE_TEST_SOURCES)),--extra-allow test/run/tlbcode-refs-test.allow) --elf $< --readelf $(N64_GCCPREFIX)/bin/mips64-elf-readelf
	$(RTK) $(N64_SYM) --all $< $<.sym
	$(RTK) cp $< $<.stripped
	$(RTK) $(N64_STRIP) -s $<.stripped
	$(RTK) $(N64_ELFCOMPRESS) -o $(dir $<) -c $(N64_ROM_ELFCOMPRESS) $<.stripped
	$(RTK) $(N64_TOOL) $(N64_TOOLFLAGS) --output $@.tmp --align 256 $<.stripped $<.sym $<.tlbcode $(N64_TOOLFILES)
	$(RTK) $(N64_ED64ROMCONFIG) $(N64_ED64ROMCONFIGFLAGS) $@.tmp
	@$(RTK) $(PYTHON) -c 'import os; assert os.path.getsize("$@.tmp") <= 64*1024*1024, "cart exceeds 64 MiB"'
	$(RTK) mv $@.tmp $@
rom: $(ROM)
endif
-include $(HOST_OBJECTS:.o=.d) $(ROM_OBJECTS:.o=.d)

.PHONY: ares-binary-tests
ares-binary-tests: $(ROM)
	$(RTK) $(PYTHON) test/run/ares-binary-tests.py --rom $(ROM)

.PHONY: fatal-tests
fatal-tests:
	$(RTK) $(PYTHON) test/run/fatal-tests.py --ares

.PHONY: qr-tests
# Needs numpy, segno, zxing-cpp, opencv-python-headless: PYTHON=<venv>/bin/python.
# Not part of `unit`; see docs/architecture.md, Diagnostics.
qr-tests:
	$(RTK) env BUILD_DIR=$(BUILD_DIR) $(PYTHON) test/run/qr-tests.py

.PHONY: refs-tests
unit: refs-tests
refs-tests:
	$(RTK) $(PYTHON) test/run/refs-check.py --selftest

.PHONY: fmt-tests
unit: fmt-tests
fmt-tests:
	$(RTK) env BUILD_DIR=$(BUILD_DIR) $(PYTHON) test/run/fmt-tests.py

.PHONY: log-tests log-burst-tests
unit: log-tests
log-tests:
	$(RTK) env BUILD_DIR=$(BUILD_DIR) $(PYTHON) test/run/log-burst-tests.py --unit-only
log-burst-tests:
	$(RTK) env BUILD_DIR=$(BUILD_DIR) $(PYTHON) test/run/log-burst-tests.py --ares

.PHONY: build-id-tests
unit: build-id-tests
build-id-tests:
	$(RTK) $(PYTHON) test/run/build-id-tests.py
