/* records_test.c - test tabeli rekordow na PC: wstawianie i remisy, pelna lista, imie
 * domyslne, wpis czekajacy na imie, zapis i odczyt pliku (takze uszkodzonego).
 *   sh tools/sim/records_test.sh */
#include <stdio.h>
#include <string.h>
#include "hof.h"

static int fails;
#define CHECK(cond, what) do { if (!(cond)) { printf("BLAD: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

static BOOL name_is(const HofEntry *e, const char *s) { return (BOOL)(memcmp(e->name, s, HOF_NAME) == 0); }

/* te same czasy, a przy zajetych miejscach te same imiona */
static BOOL same(const HofTrack *a, const HofTrack *b)
{
    int i;
    for (i = 0; i < HOF_N; i++) {
        if (a->heat[i].frames != b->heat[i].frames || a->lap[i].frames != b->lap[i].frames) return FALSE;
        if (a->heat[i].frames && !name_is(&a->heat[i], b->heat[i].name)) return FALSE;
        if (a->lap[i].frames && !name_is(&a->lap[i], b->lap[i].name)) return FALSE;
    }
    return TRUE;
}

int main(void)
{
    HofEntry *heat = g_hof[0].heat, *lap = g_hof[0].lap;
    UBYTE buf[HOF_FILE_LEN], bad[HOF_FILE_LEN];
    char nm[HOF_NAME + 1];
    UWORD len;
    HofTrack saved;

    hof_clear();
    CHECK(hof_insert(heat, 3000, "ADAM   ", 0) == 0, "pierwszy wpis na 1. miejscu");
    CHECK(hof_insert(heat, 2900, "BARTEK ", 0) == 0, "lepszy czas wypiera na 1. miejsce");
    CHECK(hof_insert(heat, 3000, "CELINA ", 0) == 2, "remis: za istniejacym wpisem");
    CHECK(name_is(&heat[1], "ADAM   ") && name_is(&heat[2], "CELINA "), "kolejnosc przy remisie");
    CHECK(hof_insert(heat, 3100, "DAREK  ", 0) == 3 && hof_insert(heat, 3200, "EWA    ", 0) == 4, "lista do 5 miejsc");
    CHECK(hof_insert(heat, 3200, "FILIP  ", 0) == -1, "remis z 5. przy pelnej liscie odpada");
    CHECK(hof_insert(heat, 3300, "GOSIA  ", 0) == -1, "gorszy czas przy pelnej liscie odpada");
    CHECK(hof_insert(heat, 2950, "HANKA  ", 0) == 1 && name_is(&heat[4], "DAREK  "), "wstawka w srodek przesuwa i wypycha ostatni");
    CHECK(hof_insert(heat, 0, "IGOR   ", 0) == -1 && hof_insert(heat, 40000, "IGOR   ", 0) == -1, "zly czas odrzucony");

    hof_default_name(nm, "MAREK NOWAK");         CHECK(strcmp(nm, "MAREK  ") == 0, "imie: pierwsze slowo nazwiska");
    hof_default_name(nm, "Krzysztof");           CHECK(strcmp(nm, "KRZYSZT") == 0, "imie: najwyzej 7 znakow, wielkie litery");
    hof_default_name(nm, "\x83" "UKASZ");        CHECK(strcmp(nm, "\x83" "UKASZ ") == 0, "imie: polska litera zostaje");
    hof_default_name(nm, "GRACZ 2");             CHECK(strcmp(nm, "GRACZ  ") == 0, "imie: domyslne nazwisko gracza");
    hof_default_name(nm, "");                    CHECK(strcmp(nm, "       ") == 0, "imie: puste nazwisko");

    hof_clear();
    hof_insert(lap, 500, "OLA    ", 0);
    hof_insert(lap, 480, "KOWAL  ", 2);           /* zawodnik 1 czeka na imie */
    hof_insert(heat, 2500, "KOWAL  ", 2);
    CHECK(hof_pending(1) && !hof_pending(0), "wpis czekajacy na imie");
    hof_set_name(1, "JANEK  ");
    CHECK(name_is(&lap[0], "JANEK  ") && name_is(&heat[0], "JANEK  ") && name_is(&lap[1], "OLA    "), "wpisane imie w obu tabelach");
    CHECK(!hof_pending(1), "po wpisie nic nie czeka");
    hof_insert(lap, 470, "ZUZA   ", 3);
    hof_resolve_pending();
    CHECK(!hof_pending(2) && name_is(&lap[0], "ZUZA   "), "przed zapisem zostaje domyslne imie");

    len = hof_serialize(buf);
    CHECK(len == HOF_FILE_LEN && len == 6 + 2 * 5 * 9 + 2 && memcmp(buf, "SPHF", 4) == 0 && buf[4] == 3, "dlugosc, naglowek i wersja 3");
    saved = g_hof[0];
    hof_clear();
    CHECK(hof_deserialize(buf, len) && same(&saved, &g_hof[0]), "zapis i odczyt bez zmian");

    memcpy(bad, buf, len); bad[10] ^= 1;
    CHECK(!hof_deserialize(bad, len), "zla suma kontrolna odrzucona");
    CHECK(!hof_deserialize(buf, len - 1), "zla dlugosc odrzucona");
    memcpy(bad, buf, len); bad[4] = 1;
    CHECK(!hof_deserialize(bad, len), "wersja 1 (3 znaki) odrzucona");
    CHECK(same(&saved, &g_hof[0]), "zly plik nie zmienia tabeli");

    {
        /* plik wersji 2 (imie 6 znakow) z wydanej gry: wczytany, imie dopelnione spacja */
        static const UBYTE v2_names[2][5][6] = { { "ADAM  " }, { "OLA   " } };
        UBYTE v2[HOF_FILE_LEN_V2], *q = v2;
        UWORD sum = 0x5350;
        int c, i, k;
        memcpy(q, "SPHF", 4); q += 4; *q++ = 2; *q++ = 1;
        for (c = 0; c < 2; c++)
            for (i = 0; i < HOF_N; i++) {
                UWORD f = (UWORD)(i == 0 ? (c ? 270 : 1160) : 0);
                *q++ = (UBYTE)(f >> 8); *q++ = (UBYTE)f;
                for (k = 0; k < 6; k++) *q++ = (UBYTE)(i == 0 ? v2_names[c][0][k] : ' ');
            }
        for (k = 0; k < q - v2; k++) sum = (UWORD)(((sum << 1) | (sum >> 15)) + v2[k]);
        *q++ = (UBYTE)(sum >> 8); *q++ = (UBYTE)sum;
        CHECK(hof_deserialize(v2, (LONG)sizeof(v2)) && heat[0].frames == 1160 && name_is(&heat[0], "ADAM   ")
              && lap[0].frames == 270 && name_is(&lap[0], "OLA    ") && !heat[1].frames, "plik wersji 2 wczytany (imie + spacja)");
    }

    printf(fails ? "\n%d BLEDOW\n" : "\nOK: wszystkie testy rekordow\n", fails);
    return fails ? 1 : 0;
}
