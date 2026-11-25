/* SPDX-License-Identifier: MIT */
#ifndef TR64_UI_H
#define TR64_UI_H
#include <stdint.h>
#include "tr64/gpu.h"

/* GPU adapters use this interface even without the generated level schema. */
struct tr64_level;
/* Drawing consumes borrowed, validated level textures. No heap or state-pool
 * allocation. Bind after level publication, unbind before level release. */
enum { TR64_UI_LEFT=0, TR64_UI_CENTRE=1, TR64_UI_RIGHT=2 };
enum { TR64_UI_HEALTH=0, TR64_UI_AIR=1, TR64_UI_DASH=2, TR64_UI_OPTION=3 };
typedef struct {
 const uint8_t *image,*palette;
 uint16_t image_width,image_height,stride;
 uint16_t source_x,source_y,width,height;
 int16_t left,top,right,bottom;
} tr64_ui_sprite_t;
int tr64_ui_bind(const struct tr64_level *level);
void tr64_ui_unbind(void);
void tr64_ui_set_active(int active);
/* Engine-owned frame hook; called at the 3D/2D boundary before presentation. */
void tr64_ui_gpu_draw(void);
/* CPU enqueue interval only; frame GPU completion is measured by gpu.h.
 * Zero when hidden. Host wall timings are not cart performance evidence. */
uint32_t tr64_ui_last_submit_us(void);
/* Text coordinates follow fixed drawText: y is the glyph baseline; centre
 * and right alignment include the full 320-pixel screen width. '$' escapes
 * one raw glyph index. Unsupported/unterminated escapes fail, never read past
 * a string's terminator or index beyond the level glyph sequence. */
int tr64_ui_text_width(const char *text);
int tr64_ui_text(int x,int y,const char *text,unsigned align);
/* Text laid out once, drawn many times (the profiler overlay): the rectangle origin of each glyph as
 * tr64_ui_text would place it (x+left, y+top of the glyph's sprite) and its glyph index. Returns the number
 * of glyphs written (0 for a blank string); -1 for an invalid string, no font, or when `capacity` would be
 * exceeded. */
typedef struct { int16_t x,y;uint8_t glyph; } tr64_ui_glyph_t;
int tr64_ui_text_glyphs(int x,int y,const char *text,unsigned align,tr64_ui_glyph_t *out,unsigned capacity);
/* The sprite of one font glyph (0..109); fatal on a malformed descriptor. */
tr64_ui_sprite_t tr64_ui_font_sprite(unsigned glyph);
/* Absolute TX64 descriptor for a sprite-only pickup notification. */
void tr64_ui_pickup_sprite(unsigned descriptor,int x,int y);
/* One raw source glyph. Alignment uses its own advance, not '$' string
 * measurement; typed menu arrows/brackets already carry their source position. */
int tr64_ui_glyph(int x,int y,unsigned glyph,unsigned align);
/* Numeric IDs are generated into ui_string_ids.h from the stable English
 * schema; all language tables share the same IDs. Invalid IDs return NULL. */
const char *tr64_ui_string(unsigned id);
uint32_t tr64_ui_string_bytes(void);
/* Immutable source-StringID widths, in pixels, from the same font data used
 * by drawing. Game uses this borrowed table only to place its text view. */
const uint16_t *tr64_ui_string_widths(uint32_t *count);
int tr64_ui_text_id(int x,int y,unsigned id,unsigned align);
/* Standalone boundary screen. Returns zero before a level font is bound.
 * Acquires, attaches, completes and presents exactly one surface internally. */
int tr64_ui_message(const char *title,const char *message);
void tr64_ui_bar(int x,int y,unsigned width,int value,unsigned type);
uint16_t tr64_ui_colour(unsigned palette_index);
/* Fixed-game shaded fill: rows 25/27 are common TITLE-derived RGB gains,
 * not palette indices. Darkens existing framebuffer pixels in place. */
void tr64_ui_shade(int x,int y,int width,int height,unsigned shade);
/* Platform adapters only: called within an attached production frame. Rects
 * are half-open, colours RGBA5551; sprite texels and palettes stay big-endian. */
void tr64_ui_backend_begin(void);
void tr64_ui_backend_screen_begin(void);
void tr64_ui_backend_screen_end(void);
void tr64_ui_backend_end(void);
void tr64_ui_backend_rect(int x,int y,int width,int height,uint16_t colour);
void tr64_ui_backend_shade(int x,int y,int width,int height,unsigned retain5);
void tr64_ui_backend_sprite(int x,int y,int width,int height,
                            const tr64_ui_sprite_t *sprite);
/* A list of glyph rectangles, sorted by glyph so that equal glyphs are adjacent, drawn within an attached
 * frame after tr64_ui_backend_begin: one image and palette load per distinct glyph, then one rectangle for
 * each occurrence. `font` gives a glyph's sprite (tr64_ui_font_sprite). */
void tr64_ui_backend_glyphs(const tr64_ui_glyph_t *glyphs,unsigned count,
                            tr64_ui_sprite_t (*font)(unsigned glyph));
#endif
