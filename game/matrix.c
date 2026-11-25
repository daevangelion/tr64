/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/common.cpp at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/common.cpp at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"

void matrixPush_c(void)
{
    if (gs->gMatrixPtr - gs->gMatrixStack >= 7)
        sys_error("game matrix stack overflow");
    (gs->gMatrixPtr[1] = gs->gMatrixPtr[0]);
    gs->gMatrixPtr++;
}

void matrixSetIdentity_c(void)
{
    Matrix *m = gs->gMatrixPtr;
    m->e00 = 0x4000;
    m->e01 = 0;
    m->e02 = 0;
    m->e03 = 0;
    m->e10 = 0;
    m->e11 = 0x4000;
    m->e12 = 0;
    m->e13 = 0;
    m->e20 = 0;
    m->e21 = 0;
    m->e22 = 0x4000;
    m->e23 = 0;
}

void matrixTranslateRel_c(int32 x, int32 y, int32 z)
{
    Matrix *m = gs->gMatrixPtr;
    if (x) {
        m->e03 = i32_add(m->e03, i32_mul(m->e00, x));
        m->e13 = i32_add(m->e13, i32_mul(m->e10, x));
        m->e23 = i32_add(m->e23, i32_mul(m->e20, x));
    }
    if (y) {
        m->e03 = i32_add(m->e03, i32_mul(m->e01, y));
        m->e13 = i32_add(m->e13, i32_mul(m->e11, y));
        m->e23 = i32_add(m->e23, i32_mul(m->e21, y));
    }
    if (z) {
        m->e03 = i32_add(m->e03, i32_mul(m->e02, z));
        m->e13 = i32_add(m->e13, i32_mul(m->e12, z));
        m->e23 = i32_add(m->e23, i32_mul(m->e22, z));
    }
}

void matrixRotateX_c(int32 angle)
{
    int32 s;
    int32 c;
    sincos(angle, s, c);
    Matrix *m = gs->gMatrixPtr;
    {
        int32 _x = ((i32_sub(i32_mul(m->e02, c), i32_mul(m->e01, s))) >> 14);
        int32 _y = ((i32_add(i32_mul(m->e01, c), i32_mul(m->e02, s))) >> 14);
        m->e02 = _x;
        m->e01 = _y;
    };
    {
        int32 _x = ((i32_sub(i32_mul(m->e12, c), i32_mul(m->e11, s))) >> 14);
        int32 _y = ((i32_add(i32_mul(m->e11, c), i32_mul(m->e12, s))) >> 14);
        m->e12 = _x;
        m->e11 = _y;
    };
    {
        int32 _x = ((i32_sub(i32_mul(m->e22, c), i32_mul(m->e21, s))) >> 14);
        int32 _y = ((i32_add(i32_mul(m->e21, c), i32_mul(m->e22, s))) >> 14);
        m->e22 = _x;
        m->e21 = _y;
    };
}

void matrixRotateY_c(int32 angle)
{
    int32 s;
    int32 c;
    sincos(angle, s, c);
    Matrix *m = gs->gMatrixPtr;
    {
        int32 _x = ((i32_sub(i32_mul(m->e00, c), i32_mul(m->e02, s))) >> 14);
        int32 _y = ((i32_add(i32_mul(m->e02, c), i32_mul(m->e00, s))) >> 14);
        m->e00 = _x;
        m->e02 = _y;
    };
    {
        int32 _x = ((i32_sub(i32_mul(m->e10, c), i32_mul(m->e12, s))) >> 14);
        int32 _y = ((i32_add(i32_mul(m->e12, c), i32_mul(m->e10, s))) >> 14);
        m->e10 = _x;
        m->e12 = _y;
    };
    {
        int32 _x = ((i32_sub(i32_mul(m->e20, c), i32_mul(m->e22, s))) >> 14);
        int32 _y = ((i32_add(i32_mul(m->e22, c), i32_mul(m->e20, s))) >> 14);
        m->e20 = _x;
        m->e22 = _y;
    };
}

void matrixRotateZ_c(int32 angle)
{
    int32 s;
    int32 c;
    sincos(angle, s, c);
    Matrix *m = gs->gMatrixPtr;
    {
        int32 _x = ((i32_sub(i32_mul(m->e01, c), i32_mul(m->e00, s))) >> 14);
        int32 _y = ((i32_add(i32_mul(m->e00, c), i32_mul(m->e01, s))) >> 14);
        m->e01 = _x;
        m->e00 = _y;
    };
    {
        int32 _x = ((i32_sub(i32_mul(m->e11, c), i32_mul(m->e10, s))) >> 14);
        int32 _y = ((i32_add(i32_mul(m->e10, c), i32_mul(m->e11, s))) >> 14);
        m->e11 = _x;
        m->e10 = _y;
    };
    {
        int32 _x = ((i32_sub(i32_mul(m->e21, c), i32_mul(m->e20, s))) >> 14);
        int32 _y = ((i32_add(i32_mul(m->e20, c), i32_mul(m->e21, s))) >> 14);
        m->e21 = _x;
        m->e20 = _y;
    };
}

void matrixRotateYXZ_c(int32 angleX, int32 angleY, int32 angleZ)
{
    if (angleY)
        matrixRotateY_c(angleY);
    if (angleX)
        matrixRotateX_c(angleX);
    if (angleZ)
        matrixRotateZ_c(angleZ);
}

void matrixLerp_c(const Matrix *n, int32 pmul, int32 pdiv)
{
    Matrix *m = gs->gMatrixPtr;
    /* All callers lerp after rotation-only updates (translation e*3 unchanged). */
    if ((pdiv == 2) || ((pdiv == 4) && (pmul == 2)))
    {
        m->e00 = i32_add(n->e00, m->e00) >> 1;
        m->e01 = i32_add(n->e01, m->e01) >> 1;
        m->e02 = i32_add(n->e02, m->e02) >> 1;
        m->e10 = i32_add(n->e10, m->e10) >> 1;
        m->e11 = i32_add(n->e11, m->e11) >> 1;
        m->e12 = i32_add(n->e12, m->e12) >> 1;
        m->e20 = i32_add(n->e20, m->e20) >> 1;
        m->e21 = i32_add(n->e21, m->e21) >> 1;
        m->e22 = i32_add(n->e22, m->e22) >> 1;
    }
    else if (pdiv == 4)
    {
        if (pmul == 1)
        {
            m->e00 = i32_add(m->e00, i32_sub(n->e00, m->e00) >> 2);
            m->e01 = i32_add(m->e01, i32_sub(n->e01, m->e01) >> 2);
            m->e02 = i32_add(m->e02, i32_sub(n->e02, m->e02) >> 2);
            m->e10 = i32_add(m->e10, i32_sub(n->e10, m->e10) >> 2);
            m->e11 = i32_add(m->e11, i32_sub(n->e11, m->e11) >> 2);
            m->e12 = i32_add(m->e12, i32_sub(n->e12, m->e12) >> 2);
            m->e20 = i32_add(m->e20, i32_sub(n->e20, m->e20) >> 2);
            m->e21 = i32_add(m->e21, i32_sub(n->e21, m->e21) >> 2);
            m->e22 = i32_add(m->e22, i32_sub(n->e22, m->e22) >> 2);
        }
        else
        {
            m->e00 = i32_sub(n->e00, i32_sub(n->e00, m->e00) >> 2);
            m->e01 = i32_sub(n->e01, i32_sub(n->e01, m->e01) >> 2);
            m->e02 = i32_sub(n->e02, i32_sub(n->e02, m->e02) >> 2);
            m->e10 = i32_sub(n->e10, i32_sub(n->e10, m->e10) >> 2);
            m->e11 = i32_sub(n->e11, i32_sub(n->e11, m->e11) >> 2);
            m->e12 = i32_sub(n->e12, i32_sub(n->e12, m->e12) >> 2);
            m->e20 = i32_sub(n->e20, i32_sub(n->e20, m->e20) >> 2);
            m->e21 = i32_sub(n->e21, i32_sub(n->e21, m->e21) >> 2);
            m->e22 = i32_sub(n->e22, i32_sub(n->e22, m->e22) >> 2);
        }
    }
    else
    {
        int32 t = i32_mul(pmul, divTable[pdiv]) >> 8;
        m->e00 = i32_add(m->e00, i32_mul(i32_sub(n->e00, m->e00), t) >> 8);
        m->e01 = i32_add(m->e01, i32_mul(i32_sub(n->e01, m->e01), t) >> 8);
        m->e02 = i32_add(m->e02, i32_mul(i32_sub(n->e02, m->e02), t) >> 8);
        m->e10 = i32_add(m->e10, i32_mul(i32_sub(n->e10, m->e10), t) >> 8);
        m->e11 = i32_add(m->e11, i32_mul(i32_sub(n->e11, m->e11), t) >> 8);
        m->e12 = i32_add(m->e12, i32_mul(i32_sub(n->e12, m->e12), t) >> 8);
        m->e20 = i32_add(m->e20, i32_mul(i32_sub(n->e20, m->e20), t) >> 8);
        m->e21 = i32_add(m->e21, i32_mul(i32_sub(n->e21, m->e21), t) >> 8);
        m->e22 = i32_add(m->e22, i32_mul(i32_sub(n->e22, m->e22), t) >> 8);
    }
}
