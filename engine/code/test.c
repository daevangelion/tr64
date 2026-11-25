/* SPDX-License-Identifier: MIT */
/* Synthetic read-only unit spanning at least two 8 KiB page pairs. */
#include <stdint.h>
#include "tr64/code.h"
static const uint8_t pattern[12288] = {[0]=17,[4096]=31,[8192]=47,[12287]=61};
uint32_t tr64_unit_test(uint32_t input)
{
 uint32_t sum=0,i;
 if(input==UINT32_MAX)code_unload("test"); /* Negative lifetime control. */
 for(i=0;i<sizeof(pattern);++i)sum+=pattern[(i+input)%sizeof(pattern)];
 return sum+input;
}
