/* SPDX-License-Identifier: MIT
 *
 * Hot code order for the cart link. Passed to the linker with
 * --section-ordering-file (Makefile), so each input section named here is
 * placed at the very start of the SDK script's .text (0xC0001000), in this
 * order, ahead of everything else; nothing outside this file moves it, and
 * test/run/hot-check.py fails the ROM build when the map differs. Evidence,
 * numbers and the rules for editing: docs/architecture.md section 6.1.
 *
 * The VR4300 I-cache is 16 KiB, direct mapped by virtual address (32-byte
 * lines): code more than 16 KiB apart competes for the same lines. The hot
 * set does not fit, so the layout decides who competes with whom:
 *   - the render window (R, last four groups) is what runs in a loop inside
 *     the measured frame; it is placed so that its first 16 KiB are alias-free;
 *   - everything that runs when the CPU is otherwise idle or once per frame
 *     (mixer, music, I/O pump, wait loops) is placed first, so the tail of the
 *     render window falls on top of it instead of on the render loop.
 * Every function is its own section (-ffunction-sections); one statement per
 * entry keeps the order exact; object patterns disambiguate static names.
 * Not listed on purpose: the game tick and pose producer (measured worse when
 * appended after the window, section 6.1) and cold code.
 *
 * Same syntax as a linker script's SECTIONS body, without SECTIONS.
 */
.text : {
  /* Idle-time code: sound pump and mixer, music decode, I/O pump. */
  *engine/snd/snd.o(.text.tr64_snd_mix)
  *engine/snd/snd.o(.text.prefetch)
  *engine/snd/snd.o(.text.tr64_snd_pump)
  *plat/n64/audio.o(.text.tr64_audio_pump)
  *libdragon.a:audio.o(.text.audio_can_write)
  *libdragon.a:audio.o(.text.audio_write_begin)
  *libdragon.a:audio.o(.text.audio_write_end)
  *engine/io/io.o(.text.tr64_io_pump)
  /* Music decode (called from the mixer). */
  *engine/snd/music.o(.text.tr64_music_pump)
  *engine/snd/music.o(.text.tr64_music_mono_pcm)
  *engine/snd/music.o(.text.prefetch)
  /* Frame-wait loop: display, RSP assert poll, VI wait. */
  *libdragon.a:display.o(.text.display_try_get)
  *libdragon.a:rsp.o(.text.__rsp_check_assert)
  *libdragon.a:kirq.o(.text.kirq_wait)
  /* Sync-point waits and the deferred RSPQ poll. */
  *libdragon.a:rspq.o(.text.rspq_syncpoint_check)
  *libdragon.a:rspq.o(.text.rspq_syncpoint_new)
  *libdragon.a:rspq.o(.text.rspq_syncpoint_wait)
  *libdragon.a:rspq.o(.text.__rspq_deferred_poll)
  /* @window Render window. 1. Submit loop, once per batch run, heaviest first. */
  *engine/render/render.o(.text.tr64_render_draw_object)
  *engine/render/render.o(.text.batch_visible_room)
  *engine/render/render.o(.text.batch_visible_mesh)
  *plat/n64/gpu.o(.text.tr64_gpu_batch)
  *plat/n64/gpu.o(.text.recolour_mesh_verts)
  *plat/n64/gpu.o(.text.tr64_gpu_material)
  *plat/n64/gpu.o(.text.tr64_gpu_matrix)
  *plat/n64/gpu.o(.text.matrix_encode)
  *plat/n64/gpu.o(.text.record)
  *plat/n64/gpu.o(.text.execute)
  /* 2. What the submit loop calls: RSPQ writers, T3D. */
  *libdragon.a:rspq.o(.text.rspq_next_buffer)
  *libt3d.a(.text.t3d_vert_load)
  *libt3d.a(.text.t3d_matrix_set)
  *libt3d.a(.text.t3d_state_set_drawflags)
  /* 3. Per-room drawable walk. */
  *engine/render/items.o(.text.tr64_render_draw_items)
  *engine/render/items.o(.text.prepare_item)
  /* 4. Interrupt entry and the SP/DP/MI handlers that fire during submission. */
  *libdragon.a:inthandler.o(.text)
  *libdragon.a:interrupt.o(.text.__MI_handler)
  *libdragon.a:rspq.o(.text.rspq_sp_interrupt)
  *libdragon.a:rdpq.o(.text.__rdpq_interrupt)
  *libdragon.a:interrupt.o(.text.disable_interrupts)
  *libdragon.a:interrupt.o(.text.enable_interrupts)
}
/* Hot data group: one contiguous run from a boundary of the 8 KiB data cache
 * (plat/n64/hotdata.c): the start anchor, then all of .sbss, then the statics
 * the frame loop touches at the start of .bss. Same rules as the code list:
 * one statement per entry, object patterns, nothing but placements.
 * docs/architecture.md section 6.2. */
.sbss : {
  *plat/n64/hotdata.o(.sbss.tr64_hot_data_start)
}
.bss : {
  *plat/n64/gpu.o(.bss.gpu)
  *engine/res/load.o(.bss.current)
  *engine/render/scene.o(.bss.renderer)
  *plat/n64/audio.o(.bss.profile)
  *libdragon.a:rspq.o(.bss.lowpri)
  *libdragon.a:rspq.o(.bss.highpri)
  *libdragon.a:rspq.o(.bss.rspq_block_chain)
  *libdragon.a:rdpq.o(.bss.rdpq_block_state)
  *libdragon.a:rdpq_tex.o(.bss.last_tload)
  *libdragon.a:display.o(.bss.display_queue)
  *plat/n64/hotdata.o(.bss.tr64_hot_data_end)
}
