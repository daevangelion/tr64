# SPDX-License-Identifier: MIT
# Included by the engine Makefile; also usable with make -f tools/cook/cook.mk.
TR64_COOK_TARGETS := 1

.PHONY: cook-tests cook-m0
cook-tests:
	PYTHONDONTWRITEBYTECODE=1 rtk $(or $(PYTHON),python3) tools/cook/tests.py

cook-m0:
	@test -n "$(TR64_DATA)" || { echo 'cook-m0 requires TR64_DATA' >&2; exit 1; }
	PYTHONDONTWRITEBYTECODE=1 rtk $(or $(PYTHON),python3) tools/cook/m0.py --data "$(TR64_DATA)" --output "$(BUILD_DIR)/cook-m0"
