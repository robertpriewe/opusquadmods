/* Set List: an example add-on mod using the SDK's named events (oqmod_events.h).
 *
 * While it is on, every track that starts playing is written to a text file in the mod's
 * folder on the USB (MODS/setlist/setlist-<n>.txt, a new file each time the mod is switched
 * on), with the time since the start of the set, the deck, artist, title, BPM and key. A
 * track counts once it has played for `min_seconds`, so a quick cue preview is left out.
 * The last three entries show as status lines on the mod's page.
 *
 * Events used: track_loaded (a new track on a deck), play / pause. */
#include <stdio.h>
#include <string.h>
#include "oqmod.h"
#include "oqmod_events.h"

static const struct oqmod_api *api;
static int on;
static char path[400];
static uint64_t set_start_ms, play_since_ms[5];
static int counted[5];           /* this deck's current track is already in the list */
static char last[3][260];
static unsigned entries;
static const unsigned min_seconds[] = {5, 15, 30};

static void show(void) {
    char text[900];
    snprintf(text, sizeof text, "%u tracks so far, in %s\n%s\n%s\n%s", entries,
             path[0] ? strrchr(path, '/') + 1 : "(no file)", last[0], last[1], last[2]);
    api->set_status(text);
}
static void write_entry(unsigned deck) {
    struct oqmod_deck d;
    if (!api->deck(deck, &d) || !d.loaded) return;
    unsigned s = (unsigned)((api->now_ms() - set_start_ms) / 1000);
    char line[400];
    snprintf(line, sizeof line, "%u:%02u:%02u  Deck %u  %s - %s  %u.%02u BPM  %s",
             s / 3600, s / 60 % 60, s % 60, deck, d.artist[0] ? d.artist : "?", d.title[0] ? d.title : "?",
             d.bpm_x100 / 100, d.bpm_x100 % 100, d.key);
    FILE *f = path[0] ? fopen(path, "a") : NULL;
    if (f) { fprintf(f, "%s\n", line); fclose(f); }
    memmove(last[1], last[0], sizeof last[0] * 2);
    snprintf(last[0], sizeof last[0], "%s - %s", d.artist, d.title);
    entries++;
    show();
}

static void track_loaded(unsigned deck, const struct oqmod_deck *now) {
    (void)now;
    counted[deck] = 0;
    play_since_ms[deck] = 0;
}
static void play(unsigned deck, const struct oqmod_deck *now) {
    (void)now;
    play_since_ms[deck] = api->now_ms();
}
static void paused(unsigned deck, const struct oqmod_deck *now) {
    (void)now;
    play_since_ms[deck] = 0;
}
static const struct oq_events events = {.track_loaded = track_loaded, .play = play, .pause = paused};

static void tick(void) {
    if (!on) return;
    oq_events_poll(api, &events);
    int choice = api->setting_get("min", 1);
    unsigned min_ms = min_seconds[choice >= 0 && choice < 3 ? choice : 1] * 1000u;
    for (unsigned deck = 1; deck <= 4; deck++)
        if (play_since_ms[deck] && !counted[deck] && api->now_ms() - play_since_ms[deck] >= min_ms) {
            counted[deck] = 1;
            write_entry(deck);
        }
}
static void enabled(int yes) {
    on = yes;
    if (!yes) return;
    /* a new file for each set */
    char dir[300];
    path[0] = 0;
    if (oq_mod_dir(dir, sizeof dir)) {
        int n = api->setting_get("number", 0) + 1;
        api->setting_set("number", n);
        snprintf(path, sizeof path, "%s/setlist-%d.txt", dir, n);
    }
    set_start_ms = api->now_ms();
    entries = 0;
    memset(last, 0, sizeof last);
    memset(counted, 0, sizeof counted);
    memset(play_since_ms, 0, sizeof play_since_ms);
    show();
}

int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < sizeof *a) return 1;
    api = a;
    static const char *const names[] = {"5 s", "15 s", "30 s"};
    api->choice("min", "Counts after playing for", names, 3, 1);
    info->api_version = OQMOD_API_VERSION;
    info->name = "Set List";
    info->description = "Writes every track you play to a text file on the USB.";
    info->version = "1.0";
    info->tick = tick;
    info->enabled = enabled;
    return 0;
}
