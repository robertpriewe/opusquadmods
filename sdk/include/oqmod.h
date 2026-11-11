#ifndef OQMOD_H
#define OQMOD_H
/* OQ Mods SDK, API version 1: add-on mods for Robs Opus Quad Mods.
 *
 * A mod is a folder on the settings USB:
 *     MODS/<folder>/mod.so      built with this header (see the SDK's build.sh)
 * It exports one function, oqmod_init(). The firmware loads mods when the app starts
 * (every mod in MODS/, each listed in the Mod Menu by its plain name; its page has its own options but no reload button), shows each one with its own switch,
 * and calls the mod back:
 *   tick()    about 30 times a second on EP136's message thread (never block in it);
 *             settings and most api calls work only from the mod's own callbacks, not
 *             from threads the mod creates (see API-REFERENCE.md);
 *   draw()    when the overlay needs painting (WAVEFORM screen), overlay thread;
 *   touch()   when that area is tapped, overlay thread (not the native GUI thread);
 *   enabled() when the user switches the mod on or off (it starts off).
 * Keep work in tick() small; draw() only paints. Use api->redraw() to ask for a repaint.
 * A crash in a mod takes the app down (the firmware then restarts in stock mode):
 * check every pointer and length. Strings are UTF-8; colours are 0xRRGGBB. */
#include <stdint.h>
#include "oqmod_browser.h"
#include "oqmod_stems.h"

#define OQMOD_API_VERSION 1

struct oqmod_deck {                /* one deck, as the firmware sees it */
    int loaded, playing;           /* 1/0 */
    uint32_t elapsed_ms, length_ms;
    uint32_t bpm_x100;             /* current (pitched) BPM x 100, 0 when unknown */
    int32_t pitch_x100;            /* pitch in percent x 100 */
    char title[128], artist[128], key[8];
};

struct oqmod_canvas;               /* opaque: the mod's overlay area while drawing */
/* A USB-owned native theme, registered once during init and resident until
 * restart. The host copies this table; it never owns the colour algorithm.
 * set/palette_set run on the native message thread. surface/ink may run on
 * drawing threads. ready=0 must leave all native hooks in pass-through mode. */
struct oqmod_theme_provider {
    uint32_t size;
    int (*set)(int),(*on)(void),(*palette_set)(int),(*palette)(void);
    uint32_t (*surface)(uint32_t),(*ink)(uint32_t);
};
/* Render instrumentation remains single-owned by the host. A USB policy may
 * change only the checked native timer classes and rebase a completed paint.
 * Hooks run on native timer/GUI threads: never call settings/UI/I-O here.
 * Context is per paint call, so nested passes do not share scratch state. */
struct oqmod_paint_context {uintptr_t instance;double before;uint64_t start_ns;};
struct oqmod_render_provider {
    uint32_t size;
    int (*period)(void *timer,int milliseconds,uintptr_t caller,uint64_t now_ns);
    void (*begin)(struct oqmod_paint_context *context,uint64_t now_ns);
    void (*end)(struct oqmod_paint_context *context);
};
struct oqmod_encoder_event {uint64_t identity;int32_t raw;uint32_t push,shift;};
struct oqmod_fx_target {uint32_t size,ready,value,mic,generation,shift;};
struct oqmod_slip_snapshot {uint32_t size,ready,on,input_on,selected[2];};
/* GUI-thread native component operations; returned class names are borrowed RTTI. */
struct oqmod_components {
    uint32_t size;
    const char *(*class_name)(const void *);
    int (*bounds)(void *,int,int,int,int);
    int (*visible)(void *,int);
    int (*reparent)(void *,void *,int);
    int (*resize)(void *,int,int);
    int (*repaint)(void *);
    int (*refresh_card)(void *);
    int (*stock_cards)(void *);
};
#define OQMOD_NATIVE_VIEW_FX_SIDEBAR 1u
struct oqmod_native_view_context {void *owner,*parent;int parent_box[4];};
struct oqmod_native_view_state {
    char *root,*owner,*parent,*time,*percent,*chooser;
    int chooser_screen[4];
};
struct oqmod_native_view_provider {
    uint32_t size;
    int (*converge)(const struct oqmod_native_view_context *,struct oqmod_native_view_state *);
    void (*restore)(void);
    int (*visible)(int,struct oqmod_native_view_state *);
};
/* Cached analysis only: no file/database work or borrowed buffers. */
#define OQMOD_ANALYSIS_CUES 8u
#define OQMOD_ANALYSIS_MEMORY 10u
#define OQMOD_ANALYSIS_LOOPS 5u
struct oqmod_analysis_snapshot {
    uint32_t size,valid,generation,columns,beats,length_ms,cue_revision;
    uint64_t at_ms,changed_ms,anchor_at,text_until_ms;
    uint32_t elapsed_ms,total_ms,anchor_ms,loop_a,loop_b,zoom,theme,title_hidden;
    uint16_t bpm_x100,color;int16_t tempo_x100;
    char key[4],title[40],artist[128],text_override[41];
    uint32_t cue_ms[8],memory_ms[10],loop_ms[5][2],memories,loops;
    uint8_t cue_color[8];
};
struct oqmod_analysis_beats {uint32_t time_ms[2];uint8_t beat[2];};
#define OQMOD_SIDE_RECORD_LEGACY 1u
struct oqmod_side_record_provider {
    uint32_t size;
    int (*record)(unsigned side,uint64_t now_ms,unsigned char out[64]);
};
struct oqmod_waveform_parts {char *lane[4],*prev,*dif,*card[4];void *card_obj[4];};
struct oqmod_layout_inputs {unsigned enabled,layout,overviews,order,render_mode,stems,stems_open,sidebar,authorized;};
struct oqmod_layout_provider {
    uint32_t size;
    int (*update)(void *,const struct oqmod_layout_inputs *);
    int (*cards)(void *);
    void (*suspend)(void);
    int (*paint_begin)(void *,void *);
    void (*paint_end)(void *,void *);
    int (*pitch)(const struct oqmod_layout_inputs *);
};
#define OQMOD_PANELS_MAX 10u
#define OQMOD_PANEL_TOUCH 1u
struct oqmod_panel {int x,y,w,h;uint32_t flags;int hit_margin_x,hit_margin_y;};
struct oqmod_waveform_context {void *view;uint32_t layout,selected[2];};
/* Reserved overlay slots follow native popup visibility in all layouts. */
enum {OQMOD_SURFACE_WAVEFORM=0, OQMOD_SURFACE_FX_FREQUENCY=1,OQMOD_SURFACE_USB_SHORTCUTS=2};
struct oqmod_surface {int x,y,w,h;uint32_t background,band,selected,text,dim,divider;};
#define OQMOD_USB_MAX 3u
/* color is native 0xAARRGGBB (A=255 when known); zero is unknown. Keeping
 * the validity byte distinguishes configured black from an unknown colour. */
struct oqmod_usb_source {uint64_t key;uint32_t port,color;char name[40];};
struct oqmod_usb_snapshot {
    uint32_t size,count,generation,reserved;uint64_t active;
    struct oqmod_usb_source sources[OQMOD_USB_MAX];
};

struct oqmod_api {
    uint32_t version, size;        /* OQMOD_API_VERSION, sizeof(struct oqmod_api) */
    /* log one line to the diagnostics report (System Info > Save diagnostics) */
    void (*log)(const char *line);
    /* deck 1..4; returns 1 when the deck has data */
    int (*deck)(unsigned deck, struct oqmod_deck *out);
    /* 1 while the WAVEFORM screen is shown */
    int (*waveform_screen)(void);
    /* the mod's own settings, saved in MODS/<folder>/settings.txt */
    int (*setting_get)(const char *key, int fallback);
    void (*setting_set)(const char *key, int value);
    /* ask for draw() to be called soon */
    void (*redraw)(void);
    /* painting, only inside draw(); coordinates relative to the area */
    void (*fill)(struct oqmod_canvas *c, int x, int y, int w, int h, uint32_t rgb);
    void (*outline)(struct oqmod_canvas *c, int x, int y, int w, int h, int thickness, uint32_t rgb);
    void (*text)(struct oqmod_canvas *c, int x, int y, int w, int h, const char *utf8, int px, uint32_t rgb);
    /* milliseconds since the app started */
    uint64_t (*now_ms)(void);
    /* up to four lines shown on the mod's page in the Mod Menu while it is
     * on ("" clears); any thread */
    void (*set_status)(const char *text);

    /* ---- advanced: mods that change the deck software itself ----------------------
     * Raw addresses only fit this deck-software version; common needs can become named
     * hooks (see the README). oqmod_events.h has the named events.
     * Addresses belong to one build of the deck software (EP136): check ep136_ok() first.
     * hook() replaces a function whose first instruction is `expected` (not PC-relative)
     * and returns its original through *orig; patch() swaps one vtable slot holding
     * `expected`. Both return 1 on success, 0 when the check fails (nothing changed).
     * Hooks stay installed until the app restarts; switch behaviour with your enabled(). */
    int (*ep136_ok)(void);
    int (*hook)(uintptr_t function, uint32_t expected, void *replacement, void **orig);
    int (*patch)(uintptr_t slot, uintptr_t expected, void *replacement, void **orig);
    /* press PLAY on deck 1..4 the way the deck's own play button does; 1 if sent.
     * Call it from tick(). */
    int (*deck_play)(unsigned deck);

    /* ---- more controls ------------------------------------------------------------
     * option(): inside oqmod_init only. Adds an on/off option row under the mod's switch
     * (up to four per mod, with choice()), saved as the mod's setting `key`; returns its value. Read it
     * later with setting_get(key, fallback).
     * quick_toggle(): a button in the right-side Beat FX box that switches the mod on and
     * off, labelled on_label / off_label; shown only while option `key` is on (NULL key:
     * always). NULL labels remove it. One mod's button is shown at a time. */
    int (*option)(const char *key, const char *label, int fallback);
    void (*quick_toggle)(const char *on_label, const char *off_label, const char *key);

    /* ---- LEDs (R104) --------------------------------------------------------------
     * led_set(): one LED of one player side (0 left, 1 right) shows rgb (0xRRGGBB; on/off
     * LEDs: any non-zero colour is on) for hold_ms (at most 10000; 0 releases it). The
     * deck's own lights come back when the hold runs out, so refresh a few times a
     * second from tick() to keep an LED. led_clear() releases every LED set by mods; it
     * also happens when a mod is switched off. LED numbers: OQMOD_LED_* below. */
    int (*led_set)(unsigned side, unsigned led, uint32_t rgb, uint32_t hold_ms);
    void (*led_clear)(void);

    /* ---- screens (R104) -----------------------------------------------------------
     * set_area(): move or resize the mod's overlay area (inside 1280x800; w = 0 hides
     * it); call from tick(). layout(): 0 off the WAVEFORM screen, else 1 = 2 DECK,
     * 2 = 2/4 DECK, 3 = 4 DECK. side_text(): show text (up to 40 characters, drawn in
     * capitals) on deck 1..4's side screen in place of its track title for hold_ms (at
     * most 60000; "" or 0 ms ends it); shown while the deck has a track loaded. */
    int (*set_area)(int x, int y, int w, int h);
    int (*layout)(void);
    int (*side_text)(unsigned deck, const char *text, uint32_t hold_ms);

    /* ---- more controls (R104) -----------------------------------------------------
     * choice(): inside oqmod_init only. An option row with count (2..8) named values,
     * shown as "label: name"; a tap moves to the next. Saved as setting `key` (0-based);
     * returns it. A mod has up to four option rows (option() and choice() together). */
    int (*choice)(const char *key, const char *label, const char *const *names, unsigned count, int fallback);

    /* ---- SDK 1.2 / R111: named native services (append-only ABI) ---------------
     * native_bind(): init only; bind this add-on to one documented service.
     * It refuses unknown services and duplicate owners. Native pointers never
     * leave the host. get() reads a property; set() validates and queues a change
     * for the native GUI thread, never calls JUCE from an overlay callback.
     * See NATIVE-SERVICES.md for names, ranges and shared-backend boundaries.
     * menu_order(): init only, lower numbers appear earlier (default 1000).
     * default_enabled(): init only; used ONLY if no saved on.<folder> exists.
     * R112 service protocol 2: get("protocol",0) >= 2; get("generation",0)
     * changes on settings reload/removal. get("applied.<property>",0) changes
     * after a queued write is consumed (including a no-op), not on acceptance.
     * Wait for that change before syncing read-back over your own preference.
     * Read-only telemetry: waveform_fps_x10, light_theme; bind("telemetry")
     * permits multiple readers and accepts no native_set() writes.
     * Icons are optional MODS/<folder>/icon.pgm (P5, 48x48, maxval 255).
     * Missing/malformed icons fall back to the puzzle icon. */
    int (*native_bind)(const char *service);
    int (*native_get)(const char *property, int fallback);
    int (*native_set)(const char *property, int value);
    void (*menu_order)(int priority);
    void (*default_enabled)(int on);
    /* Like choice(), but shows every value as a separate button on its page. */
    int (*choice_buttons)(const char *key,const char *label,const char *const *names,unsigned count,int fallback);
    /* SDK 1.3 engine extraction: surface() returns visibility and placement;
     * use_surface() binds this mod's window/touch to that surface's lifetime.
     * Bind during init; geometry may only occupy the returned reserved slot.
     * The host hides it immediately on native popups, independent of tick(). */
    int (*surface)(unsigned which,struct oqmod_surface *out);
    int (*use_surface)(unsigned which);
    /* SDK 1.3: cached native USB identities/names/profile colours, no I/O.
     * Set out->size before calling; returns 1 even when count=0. The generation
     * changes when the list/identity/name/colour changes, not on active-source
     * highlighting alone. Never choose a drive by array index after a refresh. */
    int (*usb_sources)(struct oqmod_usb_snapshot *out);
    /* Queue native SOURCE -> USB -> Local Library navigation by exact key and
     * displayed generation; never loads a track. Requires an enabled caller on
     * the unobscured waveform page. Returns acceptance, not navigation success.
     * The host revalidates owner, generation and actual presence at dispatch. */
    int (*usb_open)(uint64_t key,uint32_t generation);
    /* Same bounded native-font text drawing; bold is 0 or 1. */
    void (*text_weight)(struct oqmod_canvas *c,int x,int y,int w,int h,const char *utf8,int px,int bold,uint32_t rgb);
    /* Init-only acknowledgement that this bound service is a complete external
     * implementation, not an older settings-only client. Requires the host's
     * engine_external capability. This is compatibility negotiation, not a
     * security attestation; arbitrary .so code already runs in-process. */
    int (*native_engine)(void);
    /* Advanced: identify a native object already received by your raw hook.
     * kind=1 is EP136 ContinuousPlayLoader; kind=2 is DirectionHandler.
     * Returns deck 1..4 or 0 unknown.
     * No pointer is returned or dereferenced by the caller; the host compares
     * identity with its retained early-constructor registry. */
    unsigned (*object_deck)(unsigned kind,const void *instance);
    /* Owner-only providers below: each needs native_bind(<service>) and native_engine()
     * during init; the owning service is named per call (API-REFERENCE.md). */
    int (*theme_provider)(const struct oqmod_theme_provider *provider); /* light-theme */
    int (*render_provider)(const struct oqmod_render_provider *provider); /* smooth-waveforms, waveform-layout */
    /* Init-only single-owner routing, after native menu/side-browser consumers.
     * Return 1 to consume rotation. Host acknowledges the absolute RX counter
     * and forwards zero delta, preserving push state. No I/O/UI in this callback.
     * Disabled/removed owners are skipped. */
    int (*encoder_bind)(uint64_t identity,int (*event)(const struct oqmod_encoder_event *));
    int (*fx_target_read)(struct oqmod_fx_target *out);   /* any mod; set out->size */
    int (*fx_target_select)(unsigned value); /* enabled owner's tick only */
    int (*time_lease)(int active); /* fx-target: 250 ms native TIME suppression lease */
    /* Init only: bounded native cleanup ticks also run while disabled or USB
     * is unavailable. Do not read/write USB settings or activate new behavior
     * in that state. Only release/finish state already owned by this mod. */
    int (*background_tick)(int allow);
    int (*deck_slip_read)(struct oqmod_slip_snapshot *out);
    int (*deck_slip_set)(unsigned deck,unsigned on);     /* slip-mode, from tick() */
    /* Init-only callbacks for up to ten independent small overlay panels.
     * draw/touch coordinates are local to that panel; masks can omit TOUCH.
     * Each panel has its own small native window, not a full-screen capture. */
    int (*panel_renderer)(void (*draw)(struct oqmod_canvas *,unsigned,int,int),
                          int (*touch)(unsigned,int,int));
    int (*panels_set)(const struct oqmod_panel *panels,unsigned count);
    /* Init only: called on the GUI thread with the WAVEFORM view and its layout. */
    int (*waveform_observer)(void (*update)(const struct oqmod_waveform_context *));
    /* Init only: may rewrite side-screen title chunks (up to 256 bytes) before sending. */
    int (*side_text_filter)(void (*filter)(unsigned char *chunk,uint32_t size));
    int (*deck_text)(unsigned deck,int artist,char *out,uint32_t size); /* 0 title, 1 artist */
    int (*metadata_hide)(unsigned deck_mask);            /* hide-titles; bit 0 = deck 1 */
    int (*components)(struct oqmod_components *out);     /* operations work from tick() only */
    int (*native_view_provider)(unsigned kind,const struct oqmod_native_view_provider *provider); /* fx-sidebar */
    /* The three analysis reads work only inside a side_record_provider callback. */
    int (*analysis_snapshot)(unsigned deck,struct oqmod_analysis_snapshot *out);
    int (*analysis_columns)(unsigned deck,uint32_t generation,uint32_t first,unsigned count,uint16_t *out);
    int (*analysis_beats)(unsigned deck,uint32_t generation,uint32_t before_ms,struct oqmod_analysis_beats *out);
    int (*side_record_provider)(unsigned kind,const struct oqmod_side_record_provider *provider); /* side-now-playing-legacy */
    int (*layout_provider)(const struct oqmod_layout_provider *provider); /* waveform-layout */
    int (*waveform_parts)(void *view,struct oqmod_waveform_parts *out);
    int (*waveform_sidebar)(unsigned action,const struct oqmod_native_view_context *,struct oqmod_native_view_state *);
    int (*browser_services)(struct oqmod_browser_services *);   /* side-browser (both) */
    int (*browser_provider)(const struct oqmod_browser_provider *);
    int (*playlist_services)(struct oqmod_playlist_services *); /* playlist-editor (both) */
    int (*playlist_provider)(void (*gui_tick)(int authorized));
    int (*stems_audio)(struct oqmod_stems_audio *);             /* stems (both) */
    int (*stems_ui)(struct oqmod_stems_context *,unsigned available,unsigned on,int open);
    /* R114: init-only menu actions (up to nine), executed on the settings tick,
     * never on the touch/render thread. Disabled mods cannot execute actions.
     * Register diagnostics-only packages with a regular `diagnostics.only` asset;
     * they load only when enable_diagnostics.txt is present at software startup. */
    int (*menu_action)(const char *label,void (*callback)(void));
    /* Shared report collector, not a feature engine. Request is asynchronous;
     * status copies system information and the last save result into caller memory. */
    int (*diagnostic_save)(void);
    int (*diagnostic_status)(char *out,uint32_t capacity);
    /* Rename an existing action from tick()/callbacks; no action IDs change. */
    int (*menu_action_label)(unsigned action,const char *label);
    /* R121: jog ring brightness for both players, percent 1..100 (100 = unchanged).
     * Scales whatever the ring shows (deck colour, Party Mode, mods' LEDs); never
     * brightens. Call from tick()/enabled(); returns 0 when another enabled mod owns it.
     * Switching the mod off restores 100. Check a->size before calling (older firmware). */
    int (*ring_brightness)(unsigned percent);
};

/* LED numbers for led_set() (both sides have each). */
enum {
    OQMOD_LED_JOG_CENTER = 0,            /* on/off */
    OQMOD_LED_DECK_1_2 = 1, OQMOD_LED_DECK_3_4 = 2, OQMOD_LED_INNER_CIRCLE = 3,
    OQMOD_LED_CUE_A = 4,                 /* hot cue pads A..H: 4..11 (RGB) */
    OQMOD_LED_JOG_RING = 12,             /* RGB */
    OQMOD_LED_VINYL = 13, OQMOD_LED_MASTER_TEMPO = 14, OQMOD_LED_MASTER = 15,
    OQMOD_LED_SLIP_REVERSE = 16, OQMOD_LED_KEY_SYNC = 18, OQMOD_LED_RESET = 19,
    OQMOD_LED_BEAT_SYNC = 20, OQMOD_LED_QUANTIZE = 22, OQMOD_LED_LOOP_OUT = 23,
    OQMOD_LED_LOOP_IN = 24,              /* button lights: on/off */
    OQMOD_LED_PLAY = 25,                 /* on/off */
    OQMOD_LED_CUE = 26,                  /* RGB */
    OQMOD_LED_COUNT = 27
};

struct oqmod_info {                /* filled in by oqmod_init() */
    uint32_t api_version;          /* set to OQMOD_API_VERSION */
    const char *name;              /* shown in the menu, up to 40 characters */
    const char *description;       /* one or two short lines */
    const char *version;           /* e.g. "1.0" */
    int x, y, w, h;                /* overlay area on the 1280x800 screen; w = 0 for none */
    void (*tick)(void);
    void (*draw)(struct oqmod_canvas *c, int w, int h);
    int (*touch)(int x, int y);    /* return 1 if handled */
    void (*enabled)(int on);
};

/* Exported by every mod. Return 0 on success; keep api for later calls. */
__attribute__((visibility("default")))
int oqmod_init(const struct oqmod_api *api, struct oqmod_info *info);

#endif
