#ifndef TRADE_UI_H
#define TRADE_UI_H

/* =========================================================
 * trade_ui.h  --  Marketplace manual trade screen  (Phase 4,
 *                 extended with buying)
 *
 * Same "dim background + centred panel + button rows" pattern as
 * ui.c's menu overlay (ui_menu_draw/ui_menu_hit_test), kept in its
 * own file since ui.c is already sizeable and this overlay is
 * conceptually separate (per-Marketplace trading vs. the game menu).
 *
 * One block per tradeable resource (Wood/Fish/Grain — Gold isn't
 * tradeable for itself), each with two button rows: Sell (1 / 10 /
 * All currently in stock, at SELL_PRICE) and Buy (1 / 10 / Max —
 * as much as both Gold and storage headroom allow, at BUY_PRICE,
 * resource.h — the same "pay a markup to skip gathering" rate the
 * build-confirmation popup's Gold-payment option uses). A Close
 * button dismisses the screen; a click anywhere else outside the
 * panel also closes it, matching the menu overlay's convention.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "resource.h"

#define TRADE_W          480
#define TRADE_TITLE_H     34
#define TRADE_BLOCK_H     92    /* label + sell row + buy row */
#define TRADE_BLOCK_PAD   10
#define TRADE_MARGIN      20
#define TRADE_BTN_W       72
#define TRADE_BTN_H       26
#define TRADE_BTN_GAP     10
#define TRADE_ROW_GAP      6    /* gap between a block's sell/buy rows */
#define TRADE_CLOSE_H     36

/* Tradeable resources: RES_WOOD, RES_FISH, RES_GRAIN. RES_GOLD is
 * excluded — you can't trade currency for itself — and conveniently
 * occupies the last ResourceType slot, so this count also doubles
 * as a valid ResourceType range [0, TRADE_SELLABLE_COUNT). */
#define TRADE_SELLABLE_COUNT 3

#define TRADE_H (TRADE_TITLE_H + TRADE_SELLABLE_COUNT * (TRADE_BLOCK_H + TRADE_BLOCK_PAD) \
                 + TRADE_CLOSE_H + TRADE_MARGIN * 2)

typedef enum {
    TRADE_HIT_NONE  = 0,   /* click outside the panel — close        */
    TRADE_HIT_CLOSE = 1,   /* explicit Close button                  */
    TRADE_HIT_SELL  = 2,   /* a sell button — out_res/out_qty are set */
    TRADE_HIT_BUY   = 3    /* a buy button  — out_res/out_qty are set */
} TradeHit;

/* Draw the trade overlay. `s` is the live stockpile (read-only
 * display of current amounts/prices, and used to grey out an
 * unaffordable/full buy button); mouse_x/y highlight the hovered
 * button. */
void trade_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const Stockpile *s, int mouse_x, int mouse_y);

/* Hit-test a click against the trade overlay. On TRADE_HIT_SELL or
 * TRADE_HIT_BUY, *out_res is the resource and *out_qty is how many
 * units (-1 means "all"/"max" — the caller resolves that against
 * the live stockpile, since this function doesn't see it). */
TradeHit trade_ui_hit_test(int screen_w, int screen_h,
                           int mouse_x, int mouse_y,
                           ResourceType *out_res, int *out_qty);

#endif /* TRADE_UI_H */
