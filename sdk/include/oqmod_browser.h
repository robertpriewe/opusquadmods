/* Public browser/catalog bridge. No library key, private UI pointer or writer. */
#ifndef OQMOD_BROWSER_H
#define OQMOD_BROWSER_H
#include <stdint.h>
enum { OQMOD_LIBRARY_SOURCES=8, OQMOD_CATALOG_ROWS=5 };
struct oqmod_library_source {
    char mount[96],db[160],label[40];
    uint64_t device,inode; int64_t mtime,size; uint32_t port,color;
};
struct oqmod_catalog_item {uint64_t id;char title[256],artist[96],key[8];uint32_t bpm_x100;};
struct oqmod_catalog_page {uint32_t total,first,count;struct oqmod_catalog_item row[5];};
struct oqmod_catalog_query {
    struct oqmod_library_source source;
    uint64_t playlist,track;
    uint32_t level,first,restore,parent,cursor;
    int32_t status;
};
struct oqmod_browser_services {
    uint32_t size;
    /* Thread-safe copied source snapshots and read-only queries. A query
     * rechecks file identity, refuses WAL, opens/closes its reader locally.
     * 1=success, -1=media changed, -2=key unavailable, -3=WAL,
     * -4=open failed, -5=saved identity missing, -6=query failed. */
    unsigned (*sources)(struct oqmod_library_source *,unsigned capacity);
    int (*query)(struct oqmod_catalog_query *,struct oqmod_catalog_page *);
    int (*page)(unsigned deck,const unsigned char page[256]);
    void (*close)(unsigned side);
    int (*rich)(unsigned side,const unsigned char records[][64],unsigned count);
    unsigned (*state)(unsigned side,unsigned rich); /* requested deck or staged rich count */
    int (*ready)(void);
    /* GUI callback only: native load lock/media/playing checks remain shared.
     * Commit is NOT decoder completion. Result follows oqmod_load_result. */
    int (*preflight)(unsigned,const char *,uint64_t,uint64_t *);
    int (*dispatch)(unsigned,const char *,uint64_t,uint64_t,uint64_t,unsigned,unsigned);
};
struct oqmod_browser_provider {
    uint32_t size;
    int (*selector)(unsigned,int,uint64_t);
    int (*navigation)(uint64_t,int);
    int (*wheel)(uint64_t,int,unsigned);
    void (*poll)(void);
    void (*load_pump)(void);
    void (*enabled)(int);
    void (*controls)(unsigned);
    void (*bookmark)(const struct oqmod_library_source *,uint64_t,uint64_t,uint64_t);
};
enum oqmod_load_result {OQMOD_LOAD_OK,OQMOD_LOAD_NOT_READY,OQMOD_LOAD_MEDIA_CHANGED,
    OQMOD_LOAD_LOCK_OFF,OQMOD_LOAD_BLOCKED,OQMOD_LOAD_UNKNOWN};
/* Passive native PLAYLIST memory, copied under the foundation's observer lock.
 * source_for_media is a fresh identity lookup, not a cached borrowed pointer. */
struct oqmod_playlist_position {uint64_t media,playlist,track,serial;uint32_t cursor,parent,key,category,valid;};
struct oqmod_playlist_services {
    uint32_t size;
    int (*position)(struct oqmod_playlist_position *);
    int (*source_for_media)(uint64_t,struct oqmod_library_source *);
    void (*notice)(const char *,uint32_t milliseconds);
};
#endif
