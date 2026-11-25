/* SPDX-License-Identifier: MIT */
/* Runs before main/code_init: proves the SDK default pointer resolves to the
 * compact strong handler, not merely a later registered exception callback. */
extern void tr64_fatal_test_exception(void);
__attribute__((constructor)) static void early_exception(void)
{ tr64_fatal_test_exception(); }
