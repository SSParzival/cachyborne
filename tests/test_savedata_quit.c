// SPDX-License-Identifier: GPL-2.0-or-later
// Actual save mount/unmount module plus actual quit controller, temporary files.
#define _GNU_SOURCE
#include "../src/runtime_savedata.c"
#include "../src/runtime_quit.c"
#include <assert.h>
static char test_root[]="/tmp/cachyborne-quit-save-XXXXXX";
static unsigned events;
static int mounted_paths[SLOTS];
static char paths[SLOTS][700];
static char saved_files[2][740];
const char *runtime_file_user_dir(void) { return test_root; }
int runtime_file_mount(const char *guest,const char *host) {
    const int slot=atoi(guest+9);
    assert(slot>=0 && slot<SLOTS && !mounted_paths[slot]);
    mounted_paths[slot]=1;snprintf(paths[slot],sizeof(paths[slot]),"%s",host);
    return 0;
}
void runtime_file_unmount(const char *guest) {
    const int slot=atoi(guest+9);
    assert(slot>=0 && slot<SLOTS && mounted_paths[slot]);
    mounted_paths[slot]=0;
}
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
bool SDL_PushEvent(SDL_Event *event) {
    assert(event->type==SDL_EVENT_QUIT);
    // Also proves the save-data mutex was released before the quit callback.
    assert(runtime_savedata_mounted()==0);
    assert(!mounted_paths[0] && !mounted_paths[1]);
    for (int i=0;i<2;++i) {
        FILE *file=fopen(saved_files[i],"rb");assert(file);
        char data[16]={0};assert(fread(data,1,11,file)==11);fclose(file);
        assert(!strcmp(data,"saved exact"));
    }
    ++events;return true;
}
const char *SDL_GetError(void) { return "unexpected test event failure"; }
int main(void) {
    assert(mkdtemp(test_root));
    runtime_savedata_configure("CUSA09999");
    assert(!save_initialize(NULL));
    DirName a={.data="SLOT_A"},b={.data="SLOT_B"};
    Mount2 request_a={.user=1,.dir=&a,.mode=MODE_CREATE};
    Mount2 request_b={.user=1,.dir=&b,.mode=MODE_CREATE};
    MountResult result_a,result_b;
    assert(!save_mount2(&request_a,&result_a));
    assert(!save_mount2(&request_b,&result_b));
    assert(runtime_savedata_mounted()==2);
    for (int i=0;i<2;++i) {
        snprintf(saved_files[i],sizeof(saved_files[i]),"%.*s/userdata0010",(int)sizeof(paths[i])-1,paths[i]);
        FILE *file=fopen(saved_files[i],"wb");assert(file);
        assert(fwrite("saved exact",1,11,file)==11);assert(!fclose(file));
    }
    runtime_quit_confirmed();runtime_quit_at_title();assert(!events);
    assert(!save_umount(&result_a.point));assert(!events && runtime_savedata_mounted()==1);
    assert(!save_umount(&result_b.point));assert(events==1 && !runtime_savedata_mounted());
    assert(save_umount(&result_b.point)==ERR_NOT_FOUND);assert(events==1);
    assert(!save_terminate());
    assert(!nftw(test_root,remove_entry,16,FTW_DEPTH|FTW_PHYS));
    puts("PASS: real save mounts, both files present at quit, last-unmount gating and callback after save mutex release");
}
