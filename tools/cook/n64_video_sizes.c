/* SPDX-License-Identifier: MIT */
/* Compile-only size audit: include the pinned SDK translation unit, do not
 * copy it or link this probe into an engine/ROM. Read the emitted u32 array
 * with the target compiler's assembly output (ABI, not host sizeof guesses). */
#include "h264.c"
const unsigned tr64_video_sizes[] = {
    sizeof(h264_t),sizeof(storage_t),sizeof(mbStorage_t),
    sizeof(macroblockLayer_t),sizeof(seqParamSet_t),sizeof(picParamSet_t),
    sizeof(dpbPicture_t),sizeof(dpbOutPicture_t),sizeof(dpbStorage_t)
};
