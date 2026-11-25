# SPDX-License-Identifier: MIT
TR64_COOK_TARGETS += geo-tests geo-evidence
TR64_DATA ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/tr64/data
TEX_EVIDENCE ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/tr64/evidence/m1-tex2
GEO_EVIDENCE ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/tr64/evidence/Geo

.PHONY: geo-tests geo-evidence
geo-tests:
	PYTHONDONTWRITEBYTECODE=1 rtk $(or $(PYTHON),python3) tools/cook/test_n64_geo.py
geo-evidence:
	PYTHONDONTWRITEBYTECODE=1 rtk $(or $(PYTHON),python3) tools/cook/test_n64_geo.py --sweep --data "$(TR64_DATA)" --cache "$(TEX_EVIDENCE)" --output "$(GEO_EVIDENCE)"
