/*  camera.c  --  Camera implementation  */

#include "camera.h"
#include "map.h"   /* for TILE_W, TILE_H */

/* ---- camera_init ---------------------------------------
 * We want tile (0,0) to appear at the top-centre of the
 * screen.  In isometric projection the top of the diamond
 * grid is the point where row=0 and col=0 meet.
 *
 * The isometric x of tile (r,c) is:
 *   iso_x = (c - r) * (TILE_W / 2)
 *
 * For (0,0) that is 0.  We shift by half the screen width
 * so that x=0 maps to the horizontal centre.
 *
 * Add a small top margin (3 tile-heights) so the first row
 * is not clipped.
 * 
 * CHANGED Phase 6: offset_y start increased because TILE_H
 *  is now 128 (was 32), so 3 tile-heights of top margin = 384px.
 * -------------------------------------------------------- */
void camera_init(Camera *cam, int screen_w, int screen_h,
                 int map_cols, int map_rows)
{
    /* Suppress unused-parameter warnings until we use them */
    (void)screen_h;
    (void)map_cols;
    (void)map_rows;

    cam->offset_x = (float)(screen_w / 2);
    cam->offset_y = (float)(TILE_H * 3);
}
