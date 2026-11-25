# SPDX-License-Identifier: MIT
# Standalone or included by the root Makefile. No engine dependencies.
TR64_COOK_TARGETS += tex-tests tex-evidence
TEX_EVIDENCE ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/tr64/evidence/m1-tex
.PHONY: tex-tests tex-evidence
tex-tests:
	PYTHONDONTWRITEBYTECODE=1 rtk $(or $(PYTHON),python3) tools/cook/test_n64_tex.py
tex-evidence:
	PYTHONDONTWRITEBYTECODE=1 rtk $(or $(PYTHON),python3) tools/cook/n64_tex_evidence.py --data "$(TR64_DATA)" --output "$(TEX_EVIDENCE)"
