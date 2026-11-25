/* SPDX-License-Identifier: MIT */
/* Explicit scene-test provider, never a shipping save/media fallback. No weak
 * eng.h declarations: these definitions intentionally override weak providers.
 * The 8KiB blob and metadata are ordinary, image-budget-charged static storage. */
#include "tr64/game.h"
#include <stdint.h>
#include <stddef.h>
extern void sys_error(const char *,...) __attribute__((noreturn,format(printf,1,2)));
static uint8_t saved_blob[8192];
static uint32_t saved_bytes;
static int32_t saved_level;
static tr64_game_settings_t saved_settings;
static uint32_t settings_writes,vibration_requests;

int eng_game_has_save(void)
{
    return saved_bytes!=0;
}
int eng_game_save(void)
{
    if(game_save_blocked())return 2; /* TR64_ENG_SAVE_BLOCKED; preserve strong test providers. */
    uint32_t required=game_save(NULL,0),written;
    int32_t level=game_level_id();
    if(!required || required>sizeof(saved_blob) || level<0)
        return 0;
    written=game_save(saved_blob,sizeof(saved_blob));
    if(written!=required)
        return 0;
    saved_bytes=written;
    saved_level=level;
    return 1;
}
void eng_game_save_settings(void)
{
    const tr64_game_settings_t *settings=game_settings_view();
    if(!settings)
        sys_error("save-io: settings save without a loaded game");
    saved_settings=*settings;
    ++settings_writes;
}
void eng_game_vibrate(void)
{
    /* A counted fixture acknowledgement, not a hardware haptic claim. */
    ++vibration_requests;
}
int tr64_game_test_save(void)
{
    return eng_game_save()==1;
}
int tr64_game_test_load(void)
{
    return saved_bytes && game_load(saved_blob,saved_bytes);
}
int32_t tr64_game_test_saved_level(void)
{
    return saved_bytes?saved_level:-1;
}
uint32_t tr64_game_test_save_bytes(void)
{
    return saved_bytes;
}
const uint8_t *tr64_game_test_saved_data(uint32_t *count)
{
    *count=saved_bytes;
    return saved_blob;
}
uint32_t tr64_game_test_settings_writes(void)
{
    return settings_writes;
}
const tr64_game_settings_t *tr64_game_test_saved_settings(void)
{
    return settings_writes?&saved_settings:NULL;
}
uint32_t tr64_game_test_vibrations(void)
{
    return vibration_requests;
}
