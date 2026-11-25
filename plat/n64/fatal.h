/* SPDX-License-Identifier: MIT */
#ifndef TR64_NATIVE_FATAL_H
#define TR64_NATIVE_FATAL_H
#include <stdint.h>
#include <exception.h>
void sys_fatal_exception(exception_t *) __attribute__((noreturn));
void sys_fatal_message(const char *,uint32_t) __attribute__((noreturn));
void sys_fatal_install(void);
#endif
