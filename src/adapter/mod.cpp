// Adapter for the supplied WinMUGEN Plus .text only.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <mmsystem.h>
#include "../../include/mugen_mod.h"
#include "../../include/gravitational_lens.h"
#include <algorithm>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

struct Bitmap {
    int w,h,clip,cl,cr,ct,cb;
    int *vtable;
    void *write_bank,*read_bank,*dat;
    unsigned long id;
    void *extra;
    int x_ofs,y_ofs,seg;
    unsigned char *line[1];
};
static_assert(offsetof(Bitmap,line)==64,"Allegro x86 bitmap layout");
struct EngineImage { int w,h; uint8_t reserved[20];int type; void *unknown;Bitmap *bitmap; };
static_assert(offsetof(EngineImage,bitmap)==36,"verified engine image layout");
using GetImageFn=EngineImage* (__cdecl*)(void*,int);
using PresentFn=void (__cdecl*)(void*,int);
using OffsetPresentFn=void (__cdecl*)(void*,int,int,int);
using FormatFn=int (__cdecl*)(char*,const char*,va_list);
using QueueFn=void (__cdecl*)(void*);
using NodeFn=void (__cdecl*)(void*);
static const MugenModHostV1 *host=nullptr;
static GlContext *math_context=nullptr;
static DWORD main_thread=0;
static GlCommandV1 current{};
static bool active=false,shape_set=false;
static bool timer_requested=false;
static unsigned frame_number=0,last_command=0;
static uint8_t *base=nullptr;
static FILE *metrics=nullptr;
static std::vector<uint8_t> before,warped;
static LARGE_INTEGER frequency{},previous{};
static unsigned warped_count=0,command_count=0;
static bool captured=false;
static bool final_captured=false;
static GetImageFn get_image=nullptr;
static PresentFn original_scaled=nullptr;
static OffsetPresentFn original_offset=nullptr;
static FormatFn original_format=nullptr;
static QueueFn original_queue=nullptr;
static NodeFn original_node=nullptr;
static bool in_fight_queue=false,boundary_done=false;
static bool qa_enabled=false,qa_complete=false,qa_frame=false;
static std::vector<uint8_t> qa_baseline,qa_live,qa_prior;
static FILE *qa_trace=nullptr;
static unsigned qa_index=0;
static Bitmap *draw_bitmap() {
    auto *im=get_image(*reinterpret_cast<void**>(base+0xb6038),*reinterpret_cast<int*>(base+0xb603c));
    if(!im || im->type!=1 || im->w!=640 || im->h!=480)return nullptr;
    return im->bitmap;
}
static size_t bitmap_bytes(Bitmap *b) {
    if(!b || !b->vtable || b->w!=640 || b->h!=480 || b->vtable[0]!=16)return 0;
    return 640*480*2;
}
static void read_bitmap(Bitmap *b,std::vector<uint8_t> &v) {
    for(unsigned y=0;y<480;++y)std::memcpy(v.data()+y*1280,reinterpret_cast<unsigned char**>(reinterpret_cast<uint8_t*>(b)+64)[y],1280);
}
static void write_bitmap(Bitmap *b,const std::vector<uint8_t> &v) {
    for(unsigned y=0;y<480;++y)std::memcpy(reinterpret_cast<unsigned char**>(reinterpret_cast<uint8_t*>(b)+64)[y],v.data()+y*1280,1280);
}
static void log(const char *s) { if(host && host->log)host->log(host->mod_id,s); }
static void set_active(bool value) {
    active=value;
    if(value && !timer_requested)timer_requested=timeBeginPeriod(1)==TIMERR_NOERROR;
    else if(!value && timer_requested) {timeEndPeriod(1);timer_requested=false;}
}

extern "C" uint32_t __cdecl GlMod_GetAbiVersion() { return GL_ABI_VERSION; }
extern "C" int32_t __cdecl GlMod_IsLayerSupported(int32_t layer) {
    // Signed sort keys are actual queue boundaries, not stage BG layer numbers.
    return ((layer>=-128 && layer<=128) || layer==GL_LAYER_COMPOSITE) ? 1:0;
}
extern "C" int32_t __cdecl GlMod_Set(const GlCommandV1 *c) {
    if(!math_context) return GL_NOT_READY;
    if(GetCurrentThreadId()!=main_thread || !c || c->struct_size!=sizeof(*c) || c->enabled>1)
        return GL_INVALID_ARGUMENT;
    if(!GlMod_IsLayerSupported(c->layer)) return GL_UNSUPPORTED_LAYER;
    // One active owner in this prototype: never silently replace a different character.
    if(active && current.owner_id!=c->owner_id) return GL_NOT_READY;
    const auto result=GlMath_SetLens(math_context,&c->lens);
    if(result!=GL_OK)return result;
    current=*c;set_active(c->enabled!=0);last_command=frame_number;
    return GL_OK;
}
extern "C" int32_t __cdecl GlMod_Disable(uint32_t owner) {
    if(!math_context)return GL_NOT_READY;
    if(GetCurrentThreadId()!=main_thread)return GL_INVALID_ARGUMENT;
    if(current.owner_id!=owner)return GL_INVALID_ARGUMENT;
    set_active(false);return GL_OK;
}

static int __cdecl format_bridge(char *out,const char *format,va_list args) {
    // Read only our exact %d-only protocol; ordinary clipboard output retains
    // the engine's original formatter and behavior. No %n/native pointer API.
    if(GetCurrentThreadId()==main_thread && format) {
        va_list copy;va_copy(copy,args);
        if(std::strcmp(format,"GL1:shape:%d:%d:%d")==0) {
            const int owner=va_arg(copy,int),r=va_arg(copy,int),core=va_arg(copy,int);
            if(owner>=0 && r>=1 && r<=512 && core>=1 && core<=512 &&
               (!active || current.owner_id==static_cast<uint32_t>(owner))) {
                current.owner_id=static_cast<uint32_t>(owner);
                current.lens.radius=static_cast<uint32_t>(r);
                current.lens.core_radius=static_cast<uint32_t>(core);shape_set=true;
            }
        } else if(std::strcmp(format,"GL1:set:%d:%d:%d:%d:%d")==0) {
            const int owner=va_arg(copy,int),x=va_arg(copy,int),y=va_arg(copy,int);
            const int layer=va_arg(copy,int),strength=va_arg(copy,int);
            if(shape_set && owner>=0 && current.owner_id==static_cast<uint32_t>(owner) && strength>=0) {
                GlCommandV1 c=current;c.struct_size=sizeof(c);c.layer=layer;c.enabled=1;
                c.lens.struct_size=sizeof(c.lens);c.lens.center_x=x;c.lens.center_y=y;
                c.lens.einstein_radius=static_cast<uint32_t>(strength);
                const int result=GlMod_Set(&c);
                if(result==GL_OK)++command_count;
                else if(command_count==0)log("GL1:set rejected; invalid command or unsupported layer.");
            }
        } else if(std::strcmp(format,"GL1:off:%d")==0) {
            const int owner=va_arg(copy,int);
            if(owner>=0)GlMod_Disable(static_cast<uint32_t>(owner));
        }
        va_end(copy);
    }
    return original_format(out,format,args);
}
static void save_raw(const wchar_t *name,const std::vector<uint8_t> &bytes) {
    wchar_t path[MAX_PATH];
    if(swprintf_s(path,L"%s\\%s",host->mod_directory,name)<0)return;
    FILE *f=nullptr;if(_wfopen_s(&f,path,L"wb")==0 && f) {
        fwrite(bytes.data(),1,bytes.size(),f);fclose(f);
    }
}
static Bitmap *prepare(void *handle,int index) {
    if(!active || GetCurrentThreadId()!=main_thread)return nullptr;
    if(frame_number-last_command>3) {set_active(false);return nullptr;}
    EngineImage *engine_image=get_image(handle,index);
    if(!engine_image || engine_image->type!=1 || engine_image->w!=640 || engine_image->h!=480)return nullptr;
    Bitmap *b=engine_image->bitmap;
    if(!b || b->w!=640 || b->h!=480 || !b->vtable || (b->id&0xD0000000u))return nullptr;
    const int depth=b->vtable[0];
    const unsigned bytes=depth==15 ? 2u:static_cast<unsigned>(depth/8);
    if((depth!=8 && depth!=15 && depth!=16 && depth!=24 && depth!=32) || !bytes)return nullptr;
    const size_t row=640*bytes,total=row*480;
    if(total>before.size() || total>warped.size())return nullptr;
    LARGE_INTEGER start,end;QueryPerformanceCounter(&start);
    auto **lines=reinterpret_cast<unsigned char**>(reinterpret_cast<uint8_t*>(b)+64);
    for(unsigned y=0;y<480;++y)std::memcpy(before.data()+y*row,lines[y],row);
    std::memcpy(warped.data(),before.data(),total);
    GlImageV1 im{sizeof(im),warped.data(),640,480,static_cast<uint32_t>(row),bytes};
    if(GlMath_Warp(math_context,&im)!=GL_OK)return nullptr;
    for(unsigned y=0;y<480;++y)std::memcpy(lines[y],warped.data()+y*row,row);
    QueryPerformanceCounter(&end);
    ++warped_count;
    if(metrics) {
        const double interval=previous.QuadPart ? 1000.0*(start.QuadPart-previous.QuadPart)/frequency.QuadPart:0;
        const double cost=1000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart;
        std::fprintf(metrics,"%u,%u,%u,%d,%.6f,%.6f\n",warped_count,frame_number,command_count,depth,interval,cost);
        if(warped_count%300==0)std::fflush(metrics);
    }
    previous=start;
    if(!captured) {
        // Persist exact packed pixels; verification compares the same frame.
        std::vector<uint8_t> exact_before(before.begin(),before.begin()+total);
        std::vector<uint8_t> exact_after(warped.begin(),warped.begin()+total);
        save_raw(L"frame-before.raw",exact_before);save_raw(L"frame-after.raw",exact_after);
        wchar_t path[MAX_PATH];FILE *f=nullptr;
        if(swprintf_s(path,L"%s\\frame.json",host->mod_directory)>=0 && _wfopen_s(&f,path,L"wb")==0 && f) {
            std::fprintf(f,"{\"width\":640,\"height\":480,\"depth\":%d,\"bytes_per_pixel\":%u,\"center_x\":%d,\"center_y\":%d,\"radius\":%u,\"einstein_radius\":%u,\"core_radius\":%u,\"layer\":%d,\"owner_id\":%u}\n",
                depth,bytes,current.lens.center_x,current.lens.center_y,current.lens.radius,
                current.lens.einstein_radius,current.lens.core_radius,current.layer,current.owner_id);
            fclose(f);
        }
        captured=true;log("First 640x480 boundary frame warped; raw evidence saved.");
    }
    return b;
}
static void restore(Bitmap *b) {
    if(!b)return;
    const int depth=b->vtable[0];const size_t row=640*(depth==15 ? 2:depth/8);
    auto **lines=reinterpret_cast<unsigned char**>(reinterpret_cast<uint8_t*>(b)+64);
    for(unsigned y=0;y<480;++y)std::memcpy(lines[y],before.data()+y*row,row);
}
static void capture_final(void *handle,int index) {
    if(!captured || final_captured || !active || current.layer==GL_LAYER_COMPOSITE)return;
    EngineImage *im=get_image(handle,index);
    if(!im || im->type!=1 || !bitmap_bytes(im->bitmap))return;
    read_bitmap(im->bitmap,qa_live);
    save_raw(L"final-present.raw",qa_live);final_captured=true;
}
static void __cdecl scaled_bridge(void *handle,int index) {
    ++frame_number;
    if(active && frame_number-last_command>3)set_active(false);
    Bitmap *b=current.layer==GL_LAYER_COMPOSITE ? prepare(handle,index):nullptr;
    if(qa_enabled)capture_final(handle,index);
    original_scaled(handle,index);restore(b);
}
static void __cdecl offset_bridge(void *handle,int index,int x,int y) {
    ++frame_number;
    if(active && frame_number-last_command>3)set_active(false);
    Bitmap *b=current.layer==GL_LAYER_COMPOSITE ? prepare(handle,index):nullptr;
    if(qa_enabled)capture_final(handle,index);
    original_offset(handle,index,x,y);restore(b);
}
static void apply_boundary() {
    if(boundary_done)return;
    boundary_done=true;
    void *handle=*reinterpret_cast<void**>(base+0xb6038);
    const int index=*reinterpret_cast<int*>(base+0xb603c);
    prepare(handle,index);
}
static void __cdecl node_bridge(void *node) {
    uint8_t original_bytes[56];
    if(qa_frame)std::memcpy(original_bytes,node,56);
    if(in_fight_queue && active && !boundary_done &&
       *static_cast<int32_t*>(node)>=current.layer)apply_boundary();
    // Optional first-frame QA: run identical raster calls against an unwarped
    // control buffer, then restore the live pixels. No simulation is replayed.
    Bitmap *b=qa_frame ? draw_bitmap():nullptr;
    if(b && bitmap_bytes(b)) {
        read_bitmap(b,qa_live);write_bitmap(b,qa_baseline);
        std::memcpy(qa_prior.data(),qa_baseline.data(),qa_baseline.size());
        original_node(node);read_bitmap(b,qa_baseline);write_bitmap(b,qa_live);
    }
    original_node(node);
    if(qa_frame && qa_trace) {
        unsigned changed=0,mismatch=0;
        if(b && bitmap_bytes(b) && boundary_done && *static_cast<int32_t*>(node)>=current.layer) {
            read_bitmap(b,qa_live);
            for(size_t i=0;i<qa_live.size();i+=2)
                if(std::memcmp(qa_prior.data()+i,qa_baseline.data()+i,2)!=0) {
                    ++changed;
                    if(std::memcmp(qa_live.data()+i,qa_baseline.data()+i,2)!=0)++mismatch;
                }
        }
        std::fprintf(qa_trace,"%u,%d,%d,%u,%u,%u\n",qa_index++,*static_cast<int32_t*>(node),
            static_cast<int32_t*>(node)[6],std::memcmp(original_bytes,node,56)==0 ? 1u:0u,changed,mismatch);
    }
}
static void __cdecl queue_bridge(void *queue) {
    in_fight_queue=active && current.layer!=GL_LAYER_COMPOSITE;
    boundary_done=false;
    qa_frame=qa_enabled && !qa_complete && in_fight_queue;
    if(qa_frame) {
        Bitmap *b=draw_bitmap();
        if(!bitmap_bytes(b))qa_frame=false;
        else {
            read_bitmap(b,qa_baseline);
            wchar_t path[MAX_PATH];
            swprintf_s(path,L"%s\\order.csv",host->mod_directory);
            _wfopen_s(&qa_trace,path,L"wb");
            if(qa_trace)std::fprintf(qa_trace,"index,key,type,node_unchanged,foreground_changed,foreground_mismatch\n");
            // Snapshot the exact order chosen by the engine, before any hook.
            auto **q=static_cast<void**>(queue);auto *collection=static_cast<uint8_t*>(q[0]);
            const int count=*reinterpret_cast<int*>(collection+8);
            const int stride=*reinterpret_cast<int*>(collection+4);
            auto *nodes=*reinterpret_cast<uint8_t**>(collection+20);
            auto *indices=static_cast<int*>(q[1]);FILE *f=nullptr;
            swprintf_s(path,L"%s\\expected-order.csv",host->mod_directory);
            _wfopen_s(&f,path,L"wb");
            if(f) {
                std::fprintf(f,"index,key,type\n");
                for(int i=0;i<count;++i) {
                    auto *n=reinterpret_cast<int32_t*>(nodes+stride*indices[i]);
                    std::fprintf(f,"%d,%d,%d\n",i,n[0],n[6]);
                }
                fclose(f);
            }
        }
    }
    original_queue(queue);
    if(in_fight_queue && active && !boundary_done)apply_boundary();
    if(qa_frame) {
        Bitmap *b=draw_bitmap();read_bitmap(b,qa_live);
        save_raw(L"queue-control.raw",qa_baseline);save_raw(L"queue-lensed.raw",qa_live);
        if(qa_trace) {fclose(qa_trace);qa_trace=nullptr;}
        qa_complete=true;
    }
    qa_frame=false;
    in_fight_queue=false;
}
static bool fingerprint() {
    auto *dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    auto *nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->OptionalHeader.ImageBase!=0x400000 ||
        base!=reinterpret_cast<uint8_t*>(0x400000))return false;
    auto *s=IMAGE_FIRST_SECTION(nt);
    bool section=false;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        if(std::memcmp(s[i].Name,".text\0\0\0",8)==0 && s[i].VirtualAddress==0x1000 &&
            s[i].Misc.VirtualSize==647168 && s[i].SizeOfRawData==647168)section=true;
    if(!section)return false;
    const uint8_t expected[32]={0x45,0xcd,0xac,0xf0,0x1b,0x3a,0xeb,0x25,0xc7,0x94,0x97,0x50,0x4f,0x39,0x7c,0x3d,
        0x6e,0x6e,0x86,0x8f,0x7e,0xb3,0xd1,0xac,0x3b,0xd2,0x55,0x32,0xc8,0xf5,0x33,0xec};
    uint8_t digest[32];BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)!=0)return false;
    const auto status=BCryptHash(algorithm,nullptr,0,base+0x1000,647168,digest,sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm,0);
    return status==0 && std::memcmp(digest,expected,sizeof(digest))==0;
}
struct Patch {uint32_t rva,target;void *replacement;uint8_t old[5];};
static bool dependency_fingerprint() {
    HMODULE module=GetModuleHandleW(L"ALLEG40.DLL");wchar_t path[MAX_PATH];
    if(!module || !GetModuleFileNameW(module,path,MAX_PATH))return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size) || size.QuadPart<=0 || size.QuadPart>16*1024*1024) {CloseHandle(file);return false;}
    std::vector<uint8_t> data;
    try {data.resize(static_cast<size_t>(size.QuadPart));}
    catch (...) {CloseHandle(file);return false;}
    DWORD read=0;const bool read_ok=ReadFile(file,data.data(),static_cast<DWORD>(data.size()),&read,nullptr)!=0;
    CloseHandle(file);if(!read_ok || read!=data.size())return false;
    const uint8_t expected[32]={0x49,0x0f,0x5b,0x48,0xf4,0x94,0xf5,0x1a,0xbe,0xc8,0xd5,0xcc,0xe2,0x92,0xb6,0xf2,
        0x65,0x6c,0x92,0x1d,0xca,0x5a,0xe7,0x24,0xe2,0x71,0x26,0xa7,0x65,0xaf,0xd2,0xfa};
    uint8_t digest[32];BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)!=0)return false;
    const auto status=BCryptHash(algorithm,nullptr,0,data.data(),static_cast<ULONG>(data.size()),digest,sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm,0);
    return status==0 && std::memcmp(digest,expected,32)==0;
}
static bool install() {
    Patch patches[]={
        {0x713a9,0x9314f,reinterpret_cast<void*>(format_bridge),{}},
        {0x175de,0x86e50,reinterpret_cast<void*>(offset_bridge),{}},
        {0x175f3,0x86f70,reinterpret_cast<void*>(scaled_bridge),{}},
        {0x176cb,0x86f70,reinterpret_cast<void*>(scaled_bridge),{}},
        {0x176ed,0x86e50,reinterpret_cast<void*>(offset_bridge),{}},
        {0x1db53,0x4b60,reinterpret_cast<void*>(queue_bridge),{}},
        {0x4bbd,0x4940,reinterpret_cast<void*>(node_bridge),{}}
    };
    for(auto &p:patches) {
        auto *site=base+p.rva;
        if(site[0]!=0xe8 || site+5+*reinterpret_cast<int32_t*>(site+1)!=base+p.target)return false;
        std::memcpy(p.old,site,5);
    }
    // Initialization is synchronous on the main thread, before engine start.
    // One protection transaction covers all sites and permits complete rollback.
    DWORD old_protection=0;
    if(!VirtualProtect(base+0x1000,647168,PAGE_EXECUTE_READWRITE,&old_protection))return false;
    for(const auto &p:patches) {
        const int32_t relative=static_cast<int32_t>(reinterpret_cast<uint8_t*>(p.replacement)-(base+p.rva+5));
        std::memcpy(base+p.rva+1,&relative,4);
    }
    DWORD ignored=0;
    if(!VirtualProtect(base+0x1000,647168,old_protection,&ignored)) {
        for(const auto &p:patches)std::memcpy(base+p.rva,p.old,5);
        VirtualProtect(base+0x1000,647168,old_protection,&ignored);
        FlushInstructionCache(GetCurrentProcess(),base+0x1000,647168);return false;
    }
    FlushInstructionCache(GetCurrentProcess(),base+0x1000,647168);return true;
}
extern "C" uint32_t __cdecl MugenMod_GetAbiVersion() {return MML_ABI_VERSION;}
extern "C" int32_t __cdecl MugenMod_Init(const MugenModHostV1 *h) {
    if(!h || h->struct_size!=sizeof(*h) || h->abi_version!=1 || h->phase!=MML_PHASE_BEFORE_ENGINE_START)return 1;
    if(host)return 2;
    host=h;base=static_cast<uint8_t*>(h->engine_base);main_thread=GetCurrentThreadId();
    if(!fingerprint() || !dependency_fingerprint()) {log("Unsupported engine .text / ALLEG40 or prior hook: no changes applied.");return 3;}
    if(GlMath_GetAbiVersion()!=GL_ABI_VERSION || GlMath_Create(&math_context)!=GL_OK)return 4;
    try {before.resize(640*480*4);warped.resize(640*480*4);}
    catch (...) {GlMath_Destroy(math_context);math_context=nullptr;return 5;}
    QueryPerformanceFrequency(&frequency);
    get_image=reinterpret_cast<GetImageFn>(base+0x4eeb0);
    original_scaled=reinterpret_cast<PresentFn>(base+0x86f70);
    original_offset=reinterpret_cast<OffsetPresentFn>(base+0x86e50);
    original_format=reinterpret_cast<FormatFn>(base+0x9314f);
    original_queue=reinterpret_cast<QueueFn>(base+0x4b60);
    original_node=reinterpret_cast<NodeFn>(base+0x4940);
    wchar_t path[MAX_PATH];
    swprintf_s(path,L"%s\\verification.enable",h->mod_directory);
    qa_enabled=GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;
    if(qa_enabled) {
        try {qa_baseline.resize(640*480*2);qa_live.resize(640*480*2);qa_prior.resize(640*480*2);}
        catch (...) {qa_enabled=false;log("QA allocation failed; ordinary effect retained.");}
    }
    if(swprintf_s(path,L"%s\\metrics.csv",h->mod_directory)>=0) {
        _wfopen_s(&metrics,path,L"wb");
        if(metrics)std::fprintf(metrics,"warped_frame,present_frame,accepted_commands,depth,interval_ms,effect_ms\n");
    }
    if(!install()) {
        if(metrics) {fclose(metrics);metrics=nullptr;}
        GlMath_Destroy(math_context);math_context=nullptr;return 6;
    }
    log("GL1 adapter initialized: signed sorted-sprite boundary and diagnostic composite.");
    return 0;
}
