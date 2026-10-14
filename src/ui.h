/* ui.h - ekrany tekstowe i nakladki na tor. */
#ifndef UI_H
#define UI_H

#include <exec/types.h>

void ui_init(void);                /* przed przejeciem sprzetu: laduje plansze tytulowa */
void ui_free(void);
void ui_invalidate(void);          /* tresc ekranu sie zmienila -> przerysuj oba bufory */
void ui_render(void);              /* raz na klatke, rysuje do bufora "back" */

BOOL ui_title_update(void);        /* TRUE = przejdz dalej */
BOOL ui_setup_update(void);        /* TRUE = start turnieju */
void ui_setup_enter(void);         /* wejscie w ustawienia: kursor na START TURNIEJU */
void ui_present_start(void);       /* prezentacja od poczatku */
BOOL ui_present_update(void);      /* TRUE = koniec prezentacji (3 s) */
BOOL ui_editing(void);             /* edycja nazwiska (ESC nie wychodzi z gry) */
BOOL ui_confirm(void);             /* FIRE/Return na ekranie tabeli, wyniku, podium */
void ui_pick_update(void);         /* wybor pol w biegach nominowanych */

#endif
