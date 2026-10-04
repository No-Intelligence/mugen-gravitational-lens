#include "../../include/gravitational_lens.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

struct GlContext {
    GlLensV1 lens{};
    bool ready=false;
    int diameter=0;
    std::vector<int16_t> offsets;
    std::vector<uint8_t> snapshot;
};
extern "C" uint32_t GL_CALL GlMath_GetAbiVersion() { return GL_ABI_VERSION; }
extern "C" int32_t GL_CALL GlMath_Create(GlContext **out) {
    if (!out) return GL_INVALID_ARGUMENT;
    *out=nullptr;
    try {
        auto *c=new GlContext;
        try { c->snapshot.resize(GL_WIDTH*GL_HEIGHT*4); }
        catch (...) { delete c; throw; }
        *out=c; return GL_OK;
    } catch (...) { return GL_OUT_OF_MEMORY; }
}
extern "C" void GL_CALL GlMath_Destroy(GlContext *c) { delete c; }
extern "C" int32_t GL_CALL GlMath_SetLens(GlContext *c,const GlLensV1 *l) {
    if (!c || !l || l->struct_size!=sizeof(*l) || l->radius<1 || l->radius>512 ||
        l->einstein_radius>512 || l->core_radius<1 || l->core_radius>512 ||
        l->center_x<0 || l->center_x>=640 || l->center_y<0 || l->center_y>=480)
        return GL_INVALID_ARGUMENT;
    if (c->ready && c->lens.radius==l->radius &&
        c->lens.einstein_radius==l->einstein_radius && c->lens.core_radius==l->core_radius) {
        c->lens=*l; return GL_OK; // Moving the center never rebuilds the kernel.
    }
    try {
        const int r=static_cast<int>(l->radius), d=2*r+1;
        std::vector<int16_t> offsets(static_cast<size_t>(d)*d*2);
        const double r2=static_cast<double>(r)*r;
        const double e2=static_cast<double>(l->einstein_radius)*l->einstein_radius;
        const double core2=static_cast<double>(l->core_radius)*l->core_radius;
        for(int y=-r;y<=r;++y) for(int x=-r;x<=r;++x) {
            const double q=static_cast<double>(x)*x+static_cast<double>(y)*y;
            double scale=1;
            if(q<r2) {
                const double taper=1-q/r2;
                scale-=e2*taper*taper/(q+core2);
            }
            // Bound the displacement before narrowing; extreme user inputs
            // may produce deflections well outside the visible image.
            const auto bounded=[](double v) { return static_cast<int16_t>(
                std::lround(std::clamp(v,-2048.0,2048.0))); };
            const size_t i=(static_cast<size_t>(y+r)*d+x+r)*2;
            offsets[i]=bounded(x*scale); offsets[i+1]=bounded(y*scale);
        }
        c->offsets.swap(offsets); c->diameter=d; c->lens=*l; c->ready=true;
        return GL_OK;
    } catch (...) { return GL_OUT_OF_MEMORY; }
}
static void source(const GlContext *c,int x,int y,int &sx,int &sy) {
    const int dx=x-c->lens.center_x,dy=y-c->lens.center_y;
    const int r=static_cast<int>(c->lens.radius);
    sx=x;sy=y;
    if(dx>=-r && dx<=r && dy>=-r && dy<=r) {
        const size_t i=(static_cast<size_t>(dy+r)*c->diameter+dx+r)*2;
        sx=std::clamp(c->lens.center_x+static_cast<int>(c->offsets[i]),0,639);
        sy=std::clamp(c->lens.center_y+static_cast<int>(c->offsets[i+1]),0,479);
    }
}
extern "C" int32_t GL_CALL GlMath_SourcePixel(const GlContext *c,int32_t x,
    int32_t y,int32_t *sx,int32_t *sy) {
    if(!c || !c->ready || !sx || !sy || x<0 || x>=640 || y<0 || y>=480)
        return GL_INVALID_ARGUMENT;
    source(c,x,y,*sx,*sy); return GL_OK;
}
template<unsigned B> static void warp(GlContext *c,const GlImageV1 *im) {
    auto *pixels=static_cast<uint8_t*>(im->pixels);
    constexpr size_t row=GL_WIDTH*B;
    for(unsigned y=0;y<GL_HEIGHT;++y)
        std::memcpy(c->snapshot.data()+y*row,pixels+static_cast<size_t>(y)*im->pitch,row);
    const int r=static_cast<int>(c->lens.radius);
    const int x0=std::max(0,c->lens.center_x-r),x1=std::min(639,c->lens.center_x+r);
    const int y0=std::max(0,c->lens.center_y-r),y1=std::min(479,c->lens.center_y+r);
    for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
        int sx,sy;source(c,x,y,sx,sy);
        std::memcpy(pixels+static_cast<size_t>(y)*im->pitch+x*B,
            c->snapshot.data()+static_cast<size_t>(sy)*row+sx*B,B);
    }
}
extern "C" int32_t GL_CALL GlMath_Warp(GlContext *c,const GlImageV1 *im) {
    if(!c || !c->ready) return GL_NOT_READY;
    if(!im || im->struct_size!=sizeof(*im) || !im->pixels || im->width!=GL_WIDTH ||
       im->height!=GL_HEIGHT || im->bytes_per_pixel<1 || im->bytes_per_pixel>4 ||
       im->pitch<GL_WIDTH*im->bytes_per_pixel || im->pitch>0x7fffffffu/GL_HEIGHT)
        return GL_INVALID_ARGUMENT;
    if(!c->lens.einstein_radius) return GL_OK;
    switch(im->bytes_per_pixel) {
        case 1:warp<1>(c,im);break;case 2:warp<2>(c,im);break;
        case 3:warp<3>(c,im);break;case 4:warp<4>(c,im);break;
    }
    return GL_OK;
}
