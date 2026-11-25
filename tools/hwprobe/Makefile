# SPDX-License-Identifier: MIT
# Hardware probe ROM: plain libdragon from the pinned SDK (no N64_TLBCODE).
#   mkdir -p $OUT && make -C $OUT -f <repo>/tools/hwprobe/Makefile OUT=$OUT
# HWPROBE_DEFS=-DHWPROBE_NEGATIVE builds the negative control (every mapped step
# must report a fault and the verdict must be FAIL).
# Output: $(OUT)/hwprobe.z64. Never builds inside the repository.
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
N64_INST ?= $(HOME)/sdks/n64-sdk-tr64-dde6197a28aa-485fe3ad0dfc
N64_GCCPREFIX ?= $(HOME)/sdks/mips64-toolchain
PYTHON ?= python3
OUT ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/tr64-hwprobe
export N64_INST N64_GCCPREFIX
BUILD_DIR := $(OUT)
SOURCE_DIR := $(ROOT)
include $(N64_INST)/include/n64.mk
N64_ROM_TITLE = "TR64 HW PROBE"
N64_ROM_SAVETYPE =
N64_CFLAGS += -I$(ROOT) -I$(ROOT)/include -Wno-error=parentheses $(HWPROBE_DEFS)
N64_TOOLFILES += $(OUT)/page.hwprobe
.DEFAULT_GOAL := hwprobe.z64
OBJS := $(OUT)/tools/hwprobe/probe.o $(OUT)/tools/hwprobe/probe-asm.o $(OUT)/engine/core/qr.o
$(OUT)/page.hwprobe: $(ROOT)/tools/hwprobe/page.py
	@mkdir -p $(OUT)
	$(PYTHON) $< $@
$(OUT)/hwprobe.elf: $(OBJS)
hwprobe.z64: $(OUT)/hwprobe.elf $(OUT)/page.hwprobe
-include $(OBJS:.o=.d)
