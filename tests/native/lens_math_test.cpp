#include "../../include/gravitational_lens.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>
static void check(bool value,const char *message) {
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main() {
    GlContext *c=nullptr; check(GlMath_Create(&c)==GL_OK,"create");
    check(GlMath_GetAbiVersion()==GL_ABI_VERSION,"ABI");
    GlLensV1 l{sizeof(l),320,240,180,70,18};
    check(GlMath_SetLens(c,&l)==GL_OK,"set");
    unsigned long long compared=0;
    for(unsigned b=1;b<=4;++b) {
        const unsigned pitch=640*b+13;
        std::vector<uint8_t> original(pitch*480+32,0xCD), data;
        for(unsigned y=0;y<480;++y) for(unsigned x=0;x<640;++x)
            for(unsigned k=0;k<b;++k) original[y*pitch+x*b+k]=static_cast<uint8_t>((x*17+y*31+k*71)&255);
        data=original; GlImageV1 im{sizeof(im),data.data(),640,480,pitch,b};
        check(GlMath_Warp(c,&im)==GL_OK,"warp");
        for(int y=0;y<480;++y) for(int x=0;x<640;++x) {
            const double dx=x-l.center_x,dy=y-l.center_y,q=dx*dx+dy*dy;
            const double r2=static_cast<double>(l.radius)*l.radius;
            double scale=1;
            if(q<r2) { double w=1-q/r2; scale-=l.einstein_radius*l.einstein_radius*w*w/(q+l.core_radius*l.core_radius); }
            const int sx=std::clamp(l.center_x+static_cast<int>(std::lround(dx*scale)),0,639);
            const int sy=std::clamp(l.center_y+static_cast<int>(std::lround(dy*scale)),0,479);
            check(std::memcmp(data.data()+y*pitch+x*b,original.data()+sy*pitch+sx*b,b)==0,"independent formula and exact pixel bytes");
            ++compared;
        }
        for(unsigned y=0;y<480;++y)
            check(std::memcmp(data.data()+y*pitch+640*b,original.data()+y*pitch+640*b,13)==0,"padding unchanged");
        check(std::memcmp(data.data()+pitch*480,original.data()+pitch*480,32)==0,"tail guard");
        l.einstein_radius=0; check(GlMath_SetLens(c,&l)==GL_OK,"identity set");
        data=original; check(GlMath_Warp(c,&im)==GL_OK,"identity warp");
        check(data==original,"zero strength exact identity");
        l.einstein_radius=70;check(GlMath_SetLens(c,&l)==GL_OK,"restore");
        im.pitch=1;check(GlMath_Warp(c,&im)==GL_INVALID_ARGUMENT,"reject pitch");
    }
    GlLensV1 invalid=l; invalid.radius=513;
    check(GlMath_SetLens(c,&invalid)==GL_INVALID_ARGUMENT,"reject radius");
    // A moving center must update coordinates without rebuilding the kernel.
    for(int y : {0,239,479}) for(int x : {0,319,639}) {
        l.center_x=x;l.center_y=y;check(GlMath_SetLens(c,&l)==GL_OK,"moving center");
        int sx=-1,sy=-1;check(GlMath_SourcePixel(c,x,y,&sx,&sy)==GL_OK && sx==x && sy==y,"center finite and unchanged");
    }
    l.center_x=320;l.center_y=240;l.radius=512;
    check(GlMath_SetLens(c,&l)==GL_OK,"maximum support");
    std::vector<uint8_t> frame(640*480*4,0x91);
    GlImageV1 im{sizeof(im),frame.data(),640,480,640*4,4};
    LARGE_INTEGER frequency,start,end;QueryPerformanceFrequency(&frequency);
    std::vector<double> times;
    for(int i=0;i<650;++i) {
        QueryPerformanceCounter(&start);
        l.center_x=100+i%400;l.center_y=100+i%280;
        check(GlMath_SetLens(c,&l)==GL_OK,"bench move");
        check(GlMath_Warp(c,&im)==GL_OK,"bench warp");
        QueryPerformanceCounter(&end);
        if(i>=50) times.push_back(1000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart);
    }
    std::sort(times.begin(),times.end());
    double total=0; for(auto v:times)total+=v;
    std::printf("{\"result\":\"PASS\",\"compared_pixels\":%llu,\"samples\":%zu,\"width\":640,\"height\":480,\"bytes_per_pixel\":4,\"radius\":512,\"moving_center\":true,\"warp_mean_ms\":%.6f,\"warp_p99_ms\":%.6f,\"warp_max_ms\":%.6f,\"engine_fps_verified\":false}\n",
        compared,times.size(),total/times.size(),times[static_cast<size_t>(times.size()*0.99)],times.back());
    GlMath_Destroy(c);
}
