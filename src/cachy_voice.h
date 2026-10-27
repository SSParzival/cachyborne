/* Shared FSB and ISO-639 audio-language policy; text language stays independent. */
#ifndef CACHY_VOICE_H
#define CACHY_VOICE_H
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static inline const char *cachy_voice_language(void) {
    static const char *const codes[] = {"eng", "esa", "ese", "ded", "frf", "iti", "jaj", "ptb"};
    const char *value = getenv("BB_VOICE_LANGUAGE");
    if (value) for (unsigned i=0; i<sizeof(codes)/sizeof(codes[0]); ++i)
        if (!strcasecmp(value,codes[i])) return codes[i];
    return "eng";
}
/* CUSA03173 1.09 opening: eight untagged AAC tracks, identified on the retail
 * 99,240,674-byte asset. Restrict the fallback to this layout, never arbitrary videos. */
static inline const char *cachy_opening_voice(const char *path, long long bytes, unsigned track) {
    static const char *const voices[]={"jaj","eng","frf","iti","ded","ese","esa","ptb"};
    const char *base=strrchr(path,'/');
    if (bytes!=99240674 || strcmp(base ? base+1 : path,"sprj_opening.mp4") || track>=8) return NULL;
    return voices[track];
}
static inline const char *cachy_voice_iso(void) {
    const char *code=cachy_voice_language();
    if (!strcmp(code,"esa") || !strcmp(code,"ese")) return "spa";
    if (!strcmp(code,"ded")) return "deu";
    if (!strcmp(code,"frf")) return "fra";
    if (!strcmp(code,"iti")) return "ita";
    if (!strcmp(code,"jaj")) return "jpn";
    if (!strcmp(code,"ptb")) return "por";
    return "eng";
}
static inline int cachy_voice_matches(const char *language) {
    const char *iso=cachy_voice_iso();
    return !strncasecmp(language,iso,3) || !strncasecmp(language,cachy_voice_language(),3) ||
        (!strcmp(iso,"deu") && !strncasecmp(language,"ger",3)) ||
        (!strcmp(iso,"fra") && !strncasecmp(language,"fre",3));
}
#endif
