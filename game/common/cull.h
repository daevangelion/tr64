/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/draw.h and common.cpp at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#ifndef TR64_GAME_CULL_H
#define TR64_GAME_CULL_H
/* Private producer helper; include internal.h before this header.
 * The query supplies ceil(max vertex distance from the mesh JOINT ORIGIN) for
 * the actual resident render geometry. Core collision radii are NOT sufficient.
 * A zero query result means unknown; the item must remain drawable. */
typedef int (*GameCullMeshRadius)(void *context, uint32 mesh_slot, int32 *radius);
/* True supplies an item-origin world sphere enclosing all emitted visual
 * vertices, including Lara mesh swaps and attached hand flashes, without bone
 * posing. False leaves outputs untouched and MUST bypass culling: sprites,
 * separately attached flashes, failed queries and unprovable arithmetic.
 * No allocation or game-state change. */
bool game_item_cull_sphere(const ItemObj *item, GameCullMeshRadius query, void *context,
                           vec3i *center, int32 *radius);
#endif
