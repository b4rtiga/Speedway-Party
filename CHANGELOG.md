# Speedway Party – historia zmian

Nowości w kolejnych wersjach gry, od najnowszej. Wersja angielska: [CHANGELOG_EN.md](CHANGELOG_EN.md).
Każda wersja to nagłówek `## wersja – data` (albo „w przygotowaniu”) i lista zmian – tak, żeby strona gry mogła je pokazywać.

## 0.88 – w przygotowaniu

- **Zawody w nocy:** nowa opcja PORA: DZIEŃ / NOC. W nocy tor oświetlają cztery maszty z reflektorami – lampy z miękką poświatą, łuki w pełnym ciepłym świetle, środki prostych trochę ciemniejsze.
- **Defekty motocykli:** w losowej chwili biegu silnik krztusi się, dymi i gaśnie, a zawodnik zjeżdża na murawę i czeka tam do końca biegu – dostaje 0 punktów („D”). Średnio jeden defekt na turniej, najwyżej jeden na zawodnika; można je wyłączyć.
- **Mecz drużynowy:** gospodarze (kaski czerwony i niebieski) przeciw gościom (biały i żółty) – biegi 5:1, 4:2, 3:3, wynik meczu w programie, po biegu, na ekranie końcowym i w gazecie.
- **Trening:** nowa pozycja w menu – gracz 1 sam na torze, okrążenia bez limitu, na murawie czas okrążenia, najlepszy czas i rekord toru.
- **Fajerwerki na podium** i **fala na trybunach** – kibice „falują” przy zmianie prowadzącego i na mecie.
- **Deszcz:** nowa opcja DESZCZ – w losowym biegu zaczyna padać, tor z biegu na bieg mięknie zamiast przesychać, w biegu widać krople.
- **Fotofinisz:** gdy drugi zawodnik minie metę najwyżej 0,1 s za zwycięzcą, gra pokazuje końcówkę biegu w zwolnionym tempie i stopklatkę na linii mety z różnicą czasu.
- **Upadki:** uderzenie w bandę z dużą prędkością albo mocne najechanie na rywala czasem kończy się upadkiem – czerwona flaga, upadły wykluczony, bieg powtarzany spod taśmy.
- **Równe szanse z każdego pola:** boty łamią motocykl przed łukiem na czas i każdy jedzie w biegu trochę inną linią. Wcześniej z czterech jednakowych botów pole C wygrywało 59% biegów, a pole A 1%; teraz każde pole wygrywa od 20 do 34%.
- **Gra pamięta ustawienia:** nazwiska i sterowanie graczy, poziomy botów oraz stan toru, porę, defekty i muzykę – po ponownym uruchomieniu wszystko jest jak ostatnio (plik `settings.dat` obok gry).
- **Mistrzostwa:** nowa wartość opcji ZAWODY – trzy rundy z tymi samymi zawodnikami, punkty biegowe się sumują (jak w Grand Prix). Po każdej rundzie klasyfikacja generalna, po ostatniej mistrz i fajerwerki.
- **Pad CD32** (i pad THEA500 Mini): czerwony = FIRE, niebieski = ESC – cała gra bez klawiatury. Dla twórców: `make cd32` składa obraz płyty CD32 (wymaga własnego pliku `CD32.TM`, jeszcze nieprzetestowany).
- **Mniej pamięci na dźwięk:** muzyka w menu w 4-bitowym ADPCM (Black Motor 104 KiB zamiast 208; gra dekoduje ją w trakcie grania) i gwizdek sędziego w 11 kHz (9 KiB zamiast 17) – razem o ok. 113 KiB mniej Chip RAM.
- **Nowy ekran USTAWIENIA** (z ekranu turnieju): stan toru (suchy, wilgotny, mokry albo losowy), pora (dzień albo noc), deszcz, zawody indywidualne, drużynowe albo mistrzostwa, defekty i muzyka. Pod listą zawodników widać skrót wybranych ustawień.
- **Menu:** TURNIEJ zamiast JEDZIEMY! i nowa pozycja TRENING; ekran zawodników nazywa się TURNIEJ.
- **Podprowadzające:** nowe wypukłe tablice w kolorach kasków z logo sponsorów (3DPJOY, DEV84), płynne kołysanie całej sylwetki z tablicą i buziaki z odlatującym serduszkiem.
- **Pasek na dole ekranu:** światła startowe gasną 2 s po starcie.
- **Plansza tytułowa** bez napisu o turnieju; podpowiedzi o klawiszach mniejszą czcionką.
- **Sterowanie:** szybkie naciśnięcia GÓRA/DÓŁ w menu już nie giną.
- **Poprawki:** bieg kończy się najpóźniej po minucie (wcześniej mógł trwać bez końca, gdy nikt nie puścił przycisku pod taśmą); w angielskiej tabeli wykluczenie oznaczone X.
- **Po angielsku:** pełna dokumentacja (PDF) i trailer.

## 0.87 – 2026-10-15

- **Wersja angielska:** osobna dyskietka `SpeedwayParty_EN.adf`, a w paczce zip obie wersje z dwiema ikonami i wspólnymi rekordami.
- **Poprawki:** ESC na ekranie ustawień wraca do menu (podpowiedź mówiła o wyjściu z gry), wyśrodkowany napis o awarii maszyny startowej.

## 0.86 – 2026-10-15

- **Ikony Workbencha:** gra, szuflada, dyskietka i instrukcja.
- **Dokumentacja:** lista konfiguracji, na których gra powinna ruszyć.

## 0.85 – 2026-10-15

- **Szybsze wczytywanie:** gra wczytuje o ok. 40% mniej danych, a plansza „WCZYTYWANIE…” pojawia się od razu.
- **Równe szanse na starcie:** zawodnik spod bandy już się nie zakopuje.
- **Rekordy toru:** imię (do 7 znaków) wpisuje się raz, po turnieju.
- **Okrzyk trybun** na ostatniej prostej, wyższe obroty silników pod taśmą.
- **Poprawki:** pauza w biegu, wyśrodkowany obraz.

## 0.84 – 2026-10-15

- **Pierwsza grywalna wersja:** turniej 15 biegów dla 1–4 graczy, boty na trzech poziomach, start spod taśmy, uślizg i kontra, tor wyjeżdżający się z biegu na bieg, polewanie i równanie toru, podium, gazeta po zawodach i rekordy toru.
