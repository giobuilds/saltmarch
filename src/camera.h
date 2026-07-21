#ifndef CAMERA_H
#define CAMERA_H

/* =========================================================
 * camera.h  --  Camera / viewport
 *
 * The camera tracks how far we have scrolled the world.
 * offset_x / offset_y are pixel offsets applied before
 * drawing every tile.  When both are 0 the map origin
 * (tile 0,0) sits at the top-centre of the screen.
 *
 * Pan speed is measured in pixels per frame.
 * ========================================================= */

/* Logical render resolution. Defined here rather than in game.h
 * because camera_init() is the primary consumer and island.h (which
 * game.h includes, so it can't include game.h back) needs them to
 * centre a newly generated island's camera. Everything that
 * previously got these from game.h still does — game.h includes
 * camera.h. */
#define SCREEN_W 1920
#define SCREEN_H 1080

#define CAMERA_PAN_SPEED 400.0f  /* pixels moved per second when key held */

#define ZOOM_DEFAULT  1.0f   /* starting zoom level                    */
#define ZOOM_MIN      0.8f   /* CHANGED: max 20%% zoom out              */
#define ZOOM_MAX      1.3f   /* CHANGED: max 30%% zoom in               */
#define ZOOM_STEP     0.05f  /* CHANGED: finer step (5%% per notch)     */

typedef struct {
    float offset_x;   /* horizontal scroll in pixels */
    float offset_y;   /* vertical  scroll in pixels  */
    float zoom;       /* scale factor: 1.0 = normal, 2.0 = 2x in  */
} Camera;

/* Reset camera to a sensible starting position that centres the
 * map on a FullHD screen. */
void camera_init(Camera *cam, int screen_w, int screen_h,
                 int map_cols, int map_rows);

#endif /* CAMERA_H */
