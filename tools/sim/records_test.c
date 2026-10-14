/* records_test.c - test tabeli rekordow na PC: wstawianie i remisy, pelna lista, inicjaly
 * domyslne, wpis czekajacy na inicjaly, zapis i odczyt pliku (takze uszkodzonego).
 *   sh tools/sim/records_test.sh */
#include <stdio.h>
#include <string.h>
#include "hof.h"

static int fails;
#define CHECK(cond, what) do { if (!(cond)) { printf("BLAD: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

static BOOL init_is(const HofEntry *e, const char *s) { return (BOOL)(memcmp(e->init, s, 3) == 0); }

/* te same czasy, a przy zajetych miejscach te same inicjaly */
static BOOL same(const HofTrack *a, const HofTrack *b)
{
    int i;
    for (i = 0; i < HOF_N; i++) {
        if (a->heat[i].frames != b->heat[i].frames || a->lap[i].frames != b->lap[i].frames) return FALSE;
        if (a->heat[i].frames && !init_is(&a->heat[i], b->heat[i].init)) return FALSE;
        if (a->lap[i].frames && !init_is(&a->lap[i], b->lap[i].init)) return FALSE;
    }
    return TRUE;
}

int main(void)
{
    HofEntry *heat = g_hof[0].heat, *lap = g_hof[0].lap;
    UBYTE buf[HOF_FILE_LEN], bad[HOF_FILE_LEN];
    char in[HOF_INIT + 1];
    UWORD len;
    HofTrack saved;

    hof_clear();
    CHECK(hof_insert(heat, 3000, "AAA", 0) == 0, "pierwszy wpis na 1. miejscu");
    CHECK(hof_insert(heat, 2900, "BBB", 0) == 0, "lepszy czas wypiera na 1. miejsce");
    CHECK(hof_insert(heat, 3000, "CCC", 0) == 2, "remis: za istniejacym wpisem");
    CHECK(init_is(&heat[1], "AAA") && init_is(&heat[2], "CCC"), "kolejnosc przy remisie");
    CHECK(hof_insert(heat, 3100, "DDD", 0) == 3 && hof_insert(heat, 3200, "EEE", 0) == 4, "lista do 5 miejsc");
    CHECK(hof_insert(heat, 3200, "FFF", 0) == -1, "remis z 5. przy pelnej liscie odpada");
    CHECK(hof_insert(heat, 3300, "GGG", 0) == -1, "gorszy czas przy pelnej liscie odpada");
    CHECK(hof_insert(heat, 2950, "HHH", 0) == 1 && init_is(&heat[4], "DDD"), "wstawka w srodek przesuwa i wypycha ostatni");
    CHECK(hof_insert(heat, 0, "III", 0) == -1 && hof_insert(heat, 40000, "III", 0) == -1, "zly czas odrzucony");

    hof_default_initials(in, "NOWAK");        CHECK(strcmp(in, "NOW") == 0, "inicjaly z nazwiska");
    hof_default_initials(in, "Jo");           CHECK(strcmp(in, "JO-") == 0, "krotkie nazwisko dopelnione '-'");
    hof_default_initials(in, "\x83" "UK 7");  CHECK(strcmp(in, "LUK") == 0, "polska litera bez ogonka, spacja pominieta");
    hof_default_initials(in, "");             CHECK(strcmp(in, "---") == 0, "puste nazwisko");

    hof_clear();
    hof_insert(lap, 500, "CPU", 0);
    hof_insert(lap, 480, "KOW", 2);           /* zawodnik 1 czeka na inicjaly */
    hof_insert(heat, 2500, "KOW", 2);
    CHECK(hof_pending(1) && !hof_pending(0), "wpis czekajacy na inicjaly");
    hof_set_initials(1, "XYZ");
    CHECK(init_is(&lap[0], "XYZ") && init_is(&heat[0], "XYZ") && init_is(&lap[1], "CPU"), "wpisane inicjaly w obu tabelach");
    CHECK(!hof_pending(1), "po wpisie nic nie czeka");
    hof_insert(lap, 470, "ABC", 3);
    hof_resolve_pending();
    CHECK(!hof_pending(2) && init_is(&lap[0], "ABC"), "przed zapisem zostaja domyslne inicjaly");

    len = hof_serialize(buf);
    CHECK(len == HOF_FILE_LEN && memcmp(buf, "SPHF", 4) == 0, "dlugosc i naglowek pliku");
    saved = g_hof[0];
    hof_clear();
    CHECK(hof_deserialize(buf, len) && same(&saved, &g_hof[0]), "zapis i odczyt bez zmian");

    memcpy(bad, buf, len); bad[10] ^= 1;
    CHECK(!hof_deserialize(bad, len), "zla suma kontrolna odrzucona");
    CHECK(!hof_deserialize(buf, len - 1), "zla dlugosc odrzucona");
    memcpy(bad, buf, len); bad[4] = 2;
    CHECK(!hof_deserialize(bad, len), "inna wersja odrzucona");
    CHECK(same(&saved, &g_hof[0]), "zly plik nie zmienia tabeli");

    printf(fails ? "\n%d BLEDOW\n" : "\nOK: wszystkie testy rekordow\n", fails);
    return fails ? 1 : 0;
}
