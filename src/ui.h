/* ui.h - ekrany tekstowe i nakladki na tor. */
#ifndef UI_H
#define UI_H

#include <exec/types.h>

void ui_init_title(void);          /* przed reszta danych: ilustracja tytulowa i jej paleta */
void ui_loading_screen(void);      /* po system_show_early: plansza z napisem WCZYTYWANIE */
void ui_init(void);                /* przed przejeciem sprzetu: laduje plansze tytulowa */
void ui_free(void);
void ui_invalidate(void);
BOOL ui_panel_covers(WORD x, WORD y, WORD w, WORD h);   /* prostokat zachodzi na panel nakladki */          /* tresc ekranu sie zmienila -> przerysuj oba bufory */
void ui_render(void);              /* raz na klatke, rysuje do bufora "back" */

BOOL ui_title_update(void);        /* TRUE = przejdz dalej */
UBYTE ui_setup_update(void);       /* SETUP_START = start turnieju, SETUP_OPTIONS = opcje, 0 = nic */
#define SETUP_START   1
#define SETUP_OPTIONS 2
void ui_setup_focus_options(void); /* po powrocie z opcji: kursor na OPCJE TURNIEJU */
void ui_options_enter(void);       /* opcje turnieju: kursor na STAN TORU */
BOOL ui_options_update(void);      /* TRUE = powrot do ustawien */
void ui_menu_enter(UBYTE row);     /* ekran wyboru: 0 TURNIEJ, 1 TRENING, 2 INSTRUKCJA, 3 REKORDY, 4 CREDITS */
UBYTE ui_menu_update(void);        /* 1 TURNIEJ, 2 TRENING, 3 INSTRUKCJA, 4 REKORDY, 5 CREDITS, 0 = nic */
void ui_name_entry_enter(UBYTE rider);   /* wpis imienia do rekordow (domyslne z nazwiska) */
BOOL ui_name_entry_update(void);         /* TRUE = zatwierdzone */
const char *ui_name_entry(void);         /* wpisane imie (7 znakow, dopelnione spacjami) */
void ui_help_enter(UBYTE page);    /* instrukcja od strony page (0..2) */
BOOL ui_help_update(void);         /* TRUE = powrot do wyboru */
void ui_setup_enter(void);         /* wejscie w ustawienia: kursor na START TURNIEJU */
void ui_present_start(void);       /* prezentacja od poczatku */
BOOL ui_present_update(void);      /* TRUE = koniec prezentacji (3 s) */
BOOL ui_editing(void);             /* edycja nazwiska (ESC nie wychodzi z gry) */
BOOL ui_confirm(void);             /* FIRE/Return na ekranie tabeli, wyniku, podium */
void ui_pick_update(void);         /* wybor pol w biegach nominowanych */

#endif
