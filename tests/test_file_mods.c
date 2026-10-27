/* Exercise actual guest file operations on a linked overlay and writable saves. */
#include "../src/runtime_file.c"
#include <assert.h>
static int32_t guest_errno;
int32_t *runtime_errno(void) { return &guest_errno; }
int32_t runtime_guest_errno(int e) { return e; }
/* Reads into guest memory tell the GPU side (runtime_memory.c); nothing to tell here. */
void runtime_memory_note_write(uintptr_t address, uint64_t size) { (void)address; (void)size; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t n,const char *name) {
    for (size_t i=0;i<n;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
int main(void) {
    char root[]="/tmp/bbport-mod-files-XXXXXX";
    assert(mkdtemp(root));
    char game[512],user[512],source[512],link[512];
    snprintf(game,sizeof(game),"%s/game",root);
    snprintf(user,sizeof(user),"%s/user",root);
    snprintf(source,sizeof(source),"%s/mod.dcx",root);
    snprintf(link,sizeof(link),"%s/game/asset.dcx",root);
    assert(!mkdir(game,0755));
    FILE *f=fopen(source,"w"); assert(f); assert(fputs("modded",f)>=0); assert(!fclose(f));
    assert(!symlink(source,link));
    runtime_file_configure(game,user);
    for (int i=0;i<3;++i) {
        const char *path=i==0 ? "/app0/asset.dcx" : i==1 ? "/hostapp/asset.dcx" : "asset.dcx";
        int fd=(int)do_open(path,0,0); assert(fd>=3);
        char content[8]={0}; assert(do_read(fd,content,6)==6 && !strcmp(content,"modded"));
        GuestStat info; assert(!do_stat(path,&info) && info.size==6);
        assert(!do_close(fd));
        assert(do_open(path,2,0)==-EROFS);
        assert(do_open(path,0x400,0)==-EROFS);
        assert(do_truncate(path,0)==-EROFS);
        assert(path_op(path,2,0)==-EROFS);
        assert(do_rename(path,"/data/moved")==-EROFS);
    }
    assert(!strcmp(cachy_opening_voice("/app0/dvdroot_ps4/movie/sprj_opening.mp4",99240674,1),"eng"));
    assert(!strcmp(cachy_opening_voice("sprj_opening.mp4",99240674,0),"jaj"));
    assert(!cachy_opening_voice("other.mp4",99240674,1));
    assert(!cachy_opening_voice("sprj_opening.mp4",99240675,1));
    assert(!cachy_opening_voice("sprj_opening.mp4",99240674,8));
    /* Voice redirection affects sound banks only; missing alternatives fall back. */
    char dvd[1024],sound[1024],bank[1024],english[1024],resolved[1024];
    snprintf(dvd,sizeof(dvd),"%s/dvdroot_ps4",game); assert(!mkdir(dvd,0755));
    /* Text and layout follow BB_LANGUAGE even when the guest asks for English. */
    char category[1024], locale[1024], target[1024];
    const char *categories[]={"msg","menu"};
    const char *locales[]={"engus","spaes","spaar"};
    setenv("BB_LANGUAGE","20",1);
    for (unsigned c=0;c<2;++c) {
        snprintf(category,sizeof(category),"%s/dvdroot_ps4/%s",game,categories[c]);
        assert(!mkdir(category,0755));
        for (unsigned l=0;l<3;++l) {
            snprintf(locale,sizeof(locale),"%s/dvdroot_ps4/%s/%s",game,categories[c],locales[l]);
            assert(!mkdir(locale,0755));
            snprintf(target,sizeof(target),"%s/dvdroot_ps4/%s/%s/asset",game,categories[c],locales[l]);
            f=fopen(target,"w"); assert(f); fputs(locales[l],f); fclose(f);
        }
        char requested[128];snprintf(requested,sizeof(requested),"/app0/dvdroot_ps4/%s/engus/asset",categories[c]);
        assert(!translate(requested,resolved,sizeof(resolved)) && strstr(resolved,"/spaar/asset"));
        int text_fd=(int)do_open(requested,0,0); assert(text_fd>=3);
        char contents[6]={0};assert(do_read(text_fd,contents,5)==5 && !strcmp(contents,"spaar"));
        assert(!do_close(text_fd));
        assert(!unlink(resolved));
        assert(!translate(requested,resolved,sizeof(resolved)) && strstr(resolved,"/spaes/asset"));
        assert(!unlink(resolved));
        assert(!translate(requested,resolved,sizeof(resolved)) && strstr(resolved,"/engus/asset"));
        assert(!unlink(resolved));
        for (unsigned l=0;l<3;++l) {
            snprintf(locale,sizeof(locale),"%s/dvdroot_ps4/%s/%s",game,categories[c],locales[l]);
            assert(!rmdir(locale));
        }
        assert(!rmdir(category));
    }
    unsetenv("BB_LANGUAGE");
    snprintf(sound,sizeof(sound),"%s/dvdroot_ps4/sound",game); assert(!mkdir(sound,0755));
    snprintf(bank,sizeof(bank),"%s/dvdroot_ps4/sound/sprj_test_esa.fsb",game);
    snprintf(english,sizeof(english),"%s/dvdroot_ps4/sound/sprj_test_eng.fsb",game);
    f=fopen(bank,"w"); assert(f); fputs("spanish",f); fclose(f);
    f=fopen(english,"w"); assert(f); fputs("english",f); fclose(f);
    unsetenv("BB_VOICE_LANGUAGE");
    assert(!translate("/app0/dvdroot_ps4/sound/sprj_test_esa.fsb",resolved,sizeof(resolved)));
    assert(!strcmp(resolved,english));
    setenv("BB_VOICE_LANGUAGE","esa",1);
    assert(!translate("/hostapp/dvdroot_ps4/sound/sprj_test_eng.fsb",resolved,sizeof(resolved)));
    assert(!strcmp(resolved,bank));
    setenv("BB_VOICE_LANGUAGE","jaj",1);
    assert(!translate("dvdroot_ps4/sound/sprj_test_eng.fsb",resolved,sizeof(resolved)));
    assert(!strcmp(resolved,english));
    setenv("BB_VOICE_LANGUAGE","../../escape",1);
    assert(!strcmp(cachy_voice_language(),"eng"));
    assert(!translate("/app0/not_sound/sprj_test_esa.fsb",resolved,sizeof(resolved)));
    assert(strstr(resolved,"_esa.fsb"));
    setenv("BB_VOICE_LANGUAGE","DED",1);
    assert(cachy_voice_matches("deu") && cachy_voice_matches("ger"));
    setenv("BB_VOICE_LANGUAGE","jaj",1);
    assert(cachy_voice_matches("jpn") && !cachy_voice_matches("eng"));
    setenv("BB_VOICE_LANGUAGE","esa",1);
    assert(cachy_voice_matches("spa"));
    unsetenv("BB_VOICE_LANGUAGE");
    assert(!unlink(bank) && !unlink(english) && !rmdir(sound) && !rmdir(dvd));
    int dir=(int)do_open("/app0",0x20000,0); assert(dir>=3);
    char entries[1024]; int64_t count=do_getdents(dir,entries,sizeof(entries),NULL); assert(count>0);
    int found=0;
    for (int64_t p=0;p<count;) {
        uint16_t length; memcpy(&length,entries+p+4,2);
        assert(length);
        if (!strcmp(entries+p+8,"asset.dcx")) { assert(entries[p+6]==8); found=1; }
        p+=length;
    }
    assert(found && !do_close(dir));
    int save=(int)do_open("/data/test-save",0x202,0644); assert(save>=3);
    assert(do_write(save,"save",4)==4 && !do_close(save));
    assert(!path_op("/data/test-save",2,0));
    assert(!unlink(link) && !unlink(source) && !rmdir(game));
    const char *dirs[]={"temp0","download0","data"};
    for (int i=0;i<3;++i) { char p[1024]; snprintf(p,sizeof(p),"%s/%s",user,dirs[i]); assert(!rmdir(p)); }
    assert(!rmdir(user) && !rmdir(root));
    puts("Guest mod files: reads, stat, merged listing, readonly assets and writable saves PASS");
}
