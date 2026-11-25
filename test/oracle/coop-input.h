/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; the fixed input/actor protocol uses types from src/fixed/common.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_COOP_INPUT_H
#define TR64_COOP_INPUT_H
/* Host and oracle framed reader. Legacy mask/setup parsing is unchanged. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { COOP_COMMAND_CAPACITY=128 };
enum { COOP_JOIN=1, COOP_JOIN_REFUSED, COOP_LEAVE, COOP_LEAVE_RESULT, COOP_SAVE_BLOCKED,
       COOP_HEAVY, COOP_FLIP, COOP_PIT, COOP_WARP, COOP_WARP_RESULT, COOP_PLACE, COOP_HEALTH, COOP_HIT };
typedef struct {
    uint32_t kind,tick,slot;
    int32_t value[5];
} CoopCommand;

/* 0: not a command, -1: malformed, 1: one command. */
static int coop_read_command(FILE *in,const char *name,CoopCommand *cmd,
    uint32_t (*decimal)(const char *),int32_t (*signed_decimal)(const char *))
{
    char token[128];
    unsigned fields=0;
    cmd->kind=0;
    if (!strcmp(name,"join")) cmd->kind=COOP_JOIN;
    else if (!strcmp(name,"join-refused")) cmd->kind=COOP_JOIN_REFUSED;
    else if (!strcmp(name,"leave")) cmd->kind=COOP_LEAVE;
    else if (!strcmp(name,"leave-result")) cmd->kind=COOP_LEAVE_RESULT,fields=1;
    else if (!strcmp(name,"heavy")) cmd->kind=COOP_HEAVY,fields=4;
    else if (!strcmp(name,"flip")) cmd->kind=COOP_FLIP;
    else if (!strcmp(name,"pit")) cmd->kind=COOP_PIT,fields=4;
    else if (!strcmp(name,"warp")) cmd->kind=COOP_WARP;
    else if (!strcmp(name,"warp-result")) cmd->kind=COOP_WARP_RESULT,fields=1;
    else if (!strcmp(name,"place")) cmd->kind=COOP_PLACE,fields=5;
    else if (!strcmp(name,"health")) cmd->kind=COOP_HEALTH,fields=1;
    else if (!strcmp(name,"hit")) cmd->kind=COOP_HIT,fields=2;
    else if (!strcmp(name,"save-blocked")) cmd->kind=COOP_SAVE_BLOCKED,fields=1;
    else return 0;
    if (fscanf(in,"%127s",token)!=1) return -1;
    cmd->tick=decimal(token);
    if (cmd->kind>=COOP_WARP) {
        if (fscanf(in,"%127s",token)!=1) return -1;
        cmd->slot=decimal(token);
        if (cmd->slot>1) return -1;
    }
    for (unsigned i=0;i<fields;++i) {
        if (fscanf(in,"%127s",token)!=1) return -1;
        cmd->value[i]=signed_decimal(token);
    }
    if (((cmd->kind==COOP_HEAVY || cmd->kind==COOP_PIT) && cmd->value[0]<0) ||
        (cmd->kind==COOP_PLACE && (cmd->value[0]<0 || cmd->value[4]<INT16_MIN || cmd->value[4]>INT16_MAX)) ||
        (cmd->kind==COOP_HEALTH && (cmd->value[0]<INT16_MIN || cmd->value[0]>INT16_MAX)) ||
        (cmd->kind==COOP_HIT && (cmd->value[0]<0 || cmd->value[1]<1 || cmd->value[1]>1000))) return -1;
    return 1;
}
#endif
