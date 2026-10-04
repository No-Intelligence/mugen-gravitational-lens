#ifndef MUGEN_GRAVITATIONAL_LENS_H
#define MUGEN_GRAVITATIONAL_LENS_H
#include <stdint.h>
#ifdef _WIN32
#define GL_CALL __cdecl
#else
#define GL_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define GL_ABI_VERSION 1u
#define GL_WIDTH 640u
#define GL_HEIGHT 480u
/* Diagnostic final-composite pass. Engine sprite/stage layers are NOT this ID. */
#define GL_LAYER_COMPOSITE 2147483647
/* -128..128: insert before first sorted sprite whose native sort key >= layer.
   Stage BG0, previously drawn shadows and HUD layerno=0 are behind this
   boundary; BG1, ontop Explods and HUD layerno=1/2 follow it. A native key incorporates
   animation-element priorities; it is not always character sprpriority alone. */
enum GlResult { GL_OK=0, GL_INVALID_ARGUMENT=-1, GL_OUT_OF_MEMORY=-2,
    GL_UNSUPPORTED_LAYER=-3, GL_NOT_READY=-4 };
/* Opaque contexts are owned and freed by mugen-lens-math.dll only.
   One context per rendering thread. Calls on the same context are serialized
   by the caller. No exceptions or allocator ownership cross the ABI. */
typedef struct GlContext GlContext;
typedef struct GlLensV1 {
    uint32_t struct_size;
    int32_t center_x, center_y;       /* integer pixels in 640 x 480 */
    uint32_t radius;                 /* influence radius: 1 .. 512 */
    uint32_t einstein_radius;        /* lens strength: 0 .. 512 */
    uint32_t core_radius;            /* softened center: 1 .. 512 */
} GlLensV1;
typedef struct GlImageV1 {
    uint32_t struct_size;
    void *pixels;
    uint32_t width, height;
    uint32_t pitch;                  /* positive bytes per row */
    uint32_t bytes_per_pixel;        /* 1, 2, 3, 4; packed, unchanged */
} GlImageV1;
uint32_t GL_CALL GlMath_GetAbiVersion(void);
int32_t GL_CALL GlMath_Create(GlContext **context);
void GL_CALL GlMath_Destroy(GlContext *context);
int32_t GL_CALL GlMath_SetLens(GlContext *context, const GlLensV1 *lens);
/* Input/output may alias: source pixels are snapshotted before writing.
   Output bytes are selected from input; no color conversion/interpolation.
   Source positions outside the frame clamp to the nearest edge pixel. */
int32_t GL_CALL GlMath_Warp(GlContext *context, const GlImageV1 *image);
/* Returns the source pixel used by Warp for one output pixel. */
int32_t GL_CALL GlMath_SourcePixel(const GlContext *context, int32_t x,
    int32_t y, int32_t *source_x, int32_t *source_y);

/* Native mod API. Render-layer support must be queried, never guessed.
   These exports belong to mods/gravitational-lens/mod.dll. */
typedef struct GlCommandV1 {
    uint32_t struct_size;
    uint32_t owner_id;
    int32_t layer;
    GlLensV1 lens;
    uint32_t enabled;
} GlCommandV1;
uint32_t GL_CALL GlMod_GetAbiVersion(void);
int32_t GL_CALL GlMod_Set(const GlCommandV1 *command);
int32_t GL_CALL GlMod_Disable(uint32_t owner_id);
int32_t GL_CALL GlMod_IsLayerSupported(int32_t layer);
#ifdef __cplusplus
}
#endif
#endif
