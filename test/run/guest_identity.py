# SPDX-License-Identifier: MIT
"""Independently expected and guest-observed identities of a cart boot."""
import re


SOURCE = r"[0-9A-F]{12}"
CONFIG = r"[0-9a-f]{64}"
SHA256 = CONFIG


def expected_identity(build, header=None, *, option="--identity-header"):
    path = header if header is not None else build / "generated/cart_identity.h"
    try:
        text = path.read_text()
        build_ids = re.findall(rf'^#define TR64_BUILD_ID "({SOURCE})"\n', text, re.M)
        configs = re.findall(rf'^#define TR64_CONFIG_ID "({CONFIG})"\n', text, re.M)
        if len(build_ids) == len(configs) == 1:
            return build_ids[0], configs[0]
        reason = "expected exactly one source id and one configuration digest"
    except OSError as error:
        reason = str(error)
    raise ValueError(f"cannot read expected cart identity from {path}: {reason}; "
                     f"use {option} to supply an independent cart_identity.h")


def records(text, prefix):
    """Count all line-start candidates before validating their exact syntax."""
    return re.findall(rf"^{re.escape(prefix)}[^\n]*(?:\n|\Z)", text, re.M)


def has_exit_record(text, status):
    """The guest exit must be one complete ares record, not the launcher status."""
    candidates = records(text, "TESTHARNESS exit ")
    return len(candidates) == 1 and re.fullmatch(
        rf"TESTHARNESS exit {status}(?: \(arg1=0x[0-9a-f]{{8}} arg2=0x[0-9a-f]{{8}}\))?\n",
        candidates[0]) is not None


def build_identity(text, expected, *, play=False):
    """Return ((observed source, observed configuration), failure) for a single boot."""
    prefix = "play: build_id=" if play else "cart: build_id="
    candidates = records(text, prefix)
    absent = (None, None)
    if not candidates:
        return absent, f"missing guest build id (expected {expected[0]})"
    if len(candidates) != 1:
        return absent, f"multiple guest build ids (expected one {expected[0]})"
    mode = " mode=live" if play else ""
    match = re.fullmatch(rf"{prefix}({SOURCE}) config_id=({CONFIG}){mode}\n", candidates[0])
    if not match:
        return absent, "malformed or incomplete guest build identity record"
    actual = match.groups()
    if actual[0] != expected[0]:
        return actual, f"guest build id mismatch: actual={actual[0]} expected={expected[0]}"
    if actual[1] != expected[1]:
        return actual, f"guest configuration digest mismatch: actual={actual[1]} expected={expected[1]}"
    return actual, None


def require_identity(text, expected, *, play=False):
    actual, failure = build_identity(text, expected, play=play)
    if failure:
        raise ValueError(failure)
    return actual


def rom_receipt(text):
    """The hash verified by the launcher, not the guest or the verdict runner."""
    matches = re.findall(rf"\brom_sha256=({SHA256})\n", text)
    if len(matches) != 1 or text.count("rom_sha256=") != 1:
        raise ValueError("missing or malformed launcher ROM SHA-256 receipt")
    return matches[0]
