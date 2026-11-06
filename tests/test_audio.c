/* Deterministic PCM/queue regression: SDL streams, with the device kept paused. */
#define _GNU_SOURCE
#include <SDL3/SDL.h>
bool test_resume_audio(SDL_AudioStream *stream);
SDL_AudioStream *test_open_audio(SDL_AudioDeviceID,const SDL_AudioSpec*,SDL_AudioStreamCallback,void*);
static int live_device;
#define SDL_ResumeAudioStreamDevice test_resume_audio
#define SDL_OpenAudioDeviceStream test_open_audio
#include "src/runtime_audio.c"
#undef SDL_ResumeAudioStreamDevice
#undef SDL_OpenAudioDeviceStream
#include <assert.h>

static int starts;
static SDL_AudioDeviceID last_device;
SDL_AudioStream *test_open_audio(SDL_AudioDeviceID device,const SDL_AudioSpec *spec,SDL_AudioStreamCallback callback,void* user) {
    last_device=device;return SDL_OpenAudioDeviceStream(device,spec,callback,user);
}
bool test_resume_audio(SDL_AudioStream *stream) {
    if (live_device) return SDL_ResumeAudioStreamDevice(stream);
    /* 256 frames: 1024-frame quantum plus two reserve buffers, before playback. */
    assert(SDL_GetAudioStreamQueued(stream)>=6*256*2);
    ++starts;
    return true; /* keep the dummy device paused for repeatable queue checks */
}
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,nid)) return (uintptr_t)table[i].function;
    return 0;
}

static void conversion(int format) {
    int32_t handle=audio_open(0,0,0,256,48000,(uint32_t)format);
    assert(handle>=0);
    Port *p=&ports[handle&255];
    assert(p->stream);
    SDL_DestroyAudioStream(p->stream);
    SDL_AudioSpec spec={p->is_float ? SDL_AUDIO_F32 : SDL_AUDIO_S16,p->channels,48000};
    p->stream=SDL_CreateAudioStream(&spec,&spec); /* no device drains this stream */
    assert(p->stream);
    const int bytes=256*8*p->sample_bytes;
    unsigned char input[256*8*4], output[3*256*8*4];
    memset(input,0,sizeof(input));
    int32_t volumes[8];
    for (int c=0;c<8;++c) volumes[c]=c==1 ? VOLUME_0DB/2 : VOLUME_0DB;
    assert(audio_volume(handle,255,volumes)==0);
    volumes[0]=VOLUME_0DB+1;
    assert(audio_volume(handle,1,volumes)==ERR_INVALID_VOLUME);
    for (int f=0;f<256;++f) for (int c=0;c<8;++c) {
        int index=f*8+c;
        if (p->is_float) { float value=(c+1)/16.0f; memcpy(input+index*4,&value,4); }
        else { int16_t value=(int16_t)(c==0 ? -32768 : c==7 ? 32767 : (c+1)*2048); memcpy(input+index*2,&value,2); }
    }
    /* An empty queue is refilled with exactly two periods, followed by real PCM. */
    assert(output_port(handle,input,0)==256*8);
    assert(SDL_GetAudioStreamQueued(p->stream)==3*bytes);
    assert(p->next_deadline_ns==0); /* batched secondary ports never accumulate a bogus deadline */
    assert(SDL_GetAudioStreamData(p->stream,output,3*bytes)==3*bytes);
    for (int i=0;i<2*bytes;++i) assert(output[i]==0);
    const int map[8]={0,1,2,3,6,7,4,5};
    for (int f=0;f<256;++f) for (int c=0;c<8;++c) {
        int target=p->std_layout ? c : map[c];
        if (p->is_float) {
            float value; memcpy(&value,output+2*bytes+(f*8+target)*4,4);
            assert(value==((c+1)/16.0f)*(c==1 ? 0.5f : 1.0f));
        } else {
            int16_t value; memcpy(&value,output+2*bytes+(f*8+target)*2,2);
            assert(value==(c==0 ? -32768 : c==7 ? 32767 : (c+1)*2048)/(c==1 ? 2 : 1));
        }
    }
    /* A queued reserve does not get padded a second time. */
    assert(SDL_PutAudioStreamData(p->stream,output,2*bytes));
    assert(output_port(handle,input,0)==256*8);
    assert(SDL_GetAudioStreamQueued(p->stream)==3*bytes);
    assert(audio_close(handle)==0);
    assert(audio_output(handle,input)==ERR_NOT_OPENED);
}
int main(int argc, char **argv) {
    if (argc==2 && !strcmp(argv[1],"--device-smoke")) {
        live_device=1;
        setenv("BB_AUDIO","auto",1); setenv("BB_AUDIO_STATS","1",1);
        assert(audio_init()==0);
        int32_t handle=audio_open(0,0,0,256,48000,1);
        assert(handle>=0 && ports[handle&255].stream);
        int16_t silence[512]={0};
        for (int i=0;i<2000;++i) {
            assert(audio_output(handle,silence)==512);
            int queued=SDL_GetAudioStreamQueued(ports[handle&255].stream);
            assert(queued>=0 && queued<48000*4); /* less than one second */
        }
        assert(ports[handle&255].stat_starved==0);
        assert(audio_close(handle)==0);
        SDL_Quit();
        puts("PASS: live audio device silence, pacing and bounded queue (2000 buffers)");
        return 0;
    }
    assert(argc==1);
    setenv("SDL_AUDIODRIVER","dummy",1);
    setenv("BB_AUDIO","auto",1);
    assert(audio_init()==0);
    assert(audio_init()==ERR_ALREADY_INIT);
    unsetenv("BB_AUDIO_DEVICE");assert(sdl_audio());
    assert(playback_device()==SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    int count=0;SDL_AudioDeviceID *devices=SDL_GetAudioPlaybackDevices(&count);
    assert(devices && count>0);
    assert(!setenv("BB_AUDIO_DEVICE",SDL_GetAudioDeviceName(devices[0]),1));
    int32_t named=audio_open(0,0,0,256,48000,1);
    assert(named>=0 && ports[named&255].stream && last_device==devices[0]);
    assert(audio_close(named)==0);SDL_free(devices);
    last_device=0;
    assert(!setenv("BB_AUDIO_DEVICE","cachyborne missing audio fixture",1));
    int32_t missing=audio_open(0,0,0,256,48000,1);
    assert(missing>=0 && !ports[missing&255].stream && !last_device);
    assert(audio_close(missing)==0);
    unsetenv("BB_AUDIO_DEVICE");assert(starts==1);starts=0;
    for (int repeat=0;repeat<3;++repeat) {
        conversion(2); conversion(5); conversion(6); conversion(7);
    }
    assert(starts==12);
    assert(last_device==SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    /* Clock pacing is independent of game FPS, and batches wait only once. */
    setenv("BB_AUDIO","none",1); sdl_ready=0;
    int32_t a=audio_open(0,0,0,256,48000,1), b=audio_open(0,1,0,256,48000,1);
    int16_t pcm[512]={0};
    OutputParam pair[]={{a,pcm},{b,pcm}};
    assert(audio_outputs(pair,2)==512);
    uint64_t start=now_ns();
    assert(audio_outputs(pair,2)==512);
    assert(now_ns()-start>=4000000);
    assert(ports[a&255].next_deadline_ns>0 && ports[b&255].next_deadline_ns==0);
    /* A delayed call restarts cadence instead of replaying stale mixer slots in a burst. */
    uint64_t period=256ull*1000000000/48000;
    ports[a&255].next_deadline_ns=now_ns()-3*period;
    start=now_ns();
    assert(audio_outputs(pair,2)==512);
    assert(ports[a&255].next_deadline_ns>=start+period);
    assert(audio_close(a)==0 && audio_close(b)==0);
    SDL_Quit();
    puts("PASS: audio startup reserve, 8ch PCM/float mapping, gain, refill, reuse and batch pacing");
}
