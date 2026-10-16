# Speedway Party – changelog

What is new in each version of the game, newest first. Polish version: [CHANGELOG.md](CHANGELOG.md).
Each version is a `## version – date` heading (or “in preparation”) with a list of changes, so the game's website can show them.

## 0.88 – in preparation

- **Night meetings:** a new TIME: DAY / NIGHT option. At night four floodlight masts light the track – the bends in warm light, the straights in half-darkness.
- **Machine defects:** at a random moment of a heat an engine coughs, smokes and dies, and the rider rolls onto the infield and waits there until the end of the heat – 0 points (“D”). About one defect per tournament, at most one per rider; they can be switched off.
- **Team match:** the home team (red and blue helmets) against the away team (white and yellow) – heats end 5:1, 4:2 or 3:3, and the match score is shown in the programme, after each heat, on the final screen and in the newspaper.
- **Practice:** a new menu item – player 1 alone on the track, unlimited laps, with the lap time, the best lap and the track record on the infield.
- **Fireworks over the podium** and a **crowd wave** – the stands ripple when the lead changes and at the finish.
- **Rain:** a new RAIN option – it starts raining at a random heat, the track gets softer heat by heat instead of drying, and you can see the drops.
- **Photo finish:** when the runner-up crosses the line at most 0.1 s behind the winner, the game shows the end of the heat in slow motion and a freeze-frame on the line with the margin.
- **Falls:** hitting the fence at high speed or running hard into a rival sometimes ends in a fall – red flag, the faller is excluded and the heat is rerun from the tape.
- **Fair chances from every gate:** bots lay the bike down in time before a bend, and each one takes a slightly different line in a heat. Before, with four identical bots gate C won 59% of heats and gate A 1%; now every gate wins between 20 and 34%.
- **Settings are remembered:** player names and controls, bot skill, track condition, time of day, defects and music – after a restart everything is as it was (`settings.dat` next to the game).
- **Championship:** a new EVENT option value – three rounds with the same riders, heat points add up (as in the Grand Prix). Overall standings after each round, the champion and fireworks after the last one.
- **CD32 pad** (and THEA500 Mini's pad): red = FIRE, blue = ESC – the whole game without a keyboard. For developers: `make cd32` builds a CD32 disc image (needs your own `CD32.TM` file, not tested yet).
- **Less memory for sound:** the menu music in 4-bit ADPCM (Black Motor 104 KiB instead of 208; the game decodes it while playing) and the referee's whistle at 11 kHz (9 KiB instead of 17) – about 113 KiB less Chip RAM in total.
- **New SETTINGS screen** (from the tournament screen): track condition (dry, damp, wet or random), time of day (day or night), rain, an individual event, a team match or a championship, defects and music. A summary of the settings is shown under the list of riders.
- **Menu:** TOURNAMENT instead of LET'S RACE! and a new PRACTICE item; the riders screen is called TOURNAMENT.
- **Start girls:** new glossy boards in the helmet colours with sponsor logos (3DPJOY, DEV84), a smooth sway of the whole figure with the board, and kisses with a flying heart.
- **Bottom bar:** the start lights go off 2 s after the start.
- **Title screen** without the tournament line; key hints in a smaller font.
- **Controls:** quick UP/DOWN presses in the menus are no longer lost.
- **Fixes:** a heat now ends after a minute at the latest (before, it could go on forever if nobody let go at the tape); an exclusion is marked X in the English table.
- **In English:** the full manual (PDF) and the trailer.

## 0.87 – 2026-10-15

- **English version:** a separate `SpeedwayParty_EN.adf` floppy, and the zip package holds both versions with two icons and shared records.
- **Fixes:** ESC on the settings screen returns to the menu (the hint said it quits the game), the starting gate failure message is centred.

## 0.86 – 2026-10-15

- **Workbench icons:** game, drawer, floppy and manual.
- **Manual:** a list of configurations the game should run on.

## 0.85 – 2026-10-15

- **Faster loading:** the game loads about 40% less data, and the “LOADING…” screen appears at once.
- **A fair start:** a rider starting by the fence no longer bogs down.
- **Track records:** the name (up to 7 characters) is entered once, after the tournament.
- **Crowd cheer** on the last straight, higher engine revs at the tape.
- **Fixes:** pause during a heat, centred picture.

## 0.84 – 2026-10-15

- **The first playable version:** a 15-heat tournament for 1–4 players, bots at three skill levels, the tape start, sliding and counter-steering, a track that wears in from heat to heat, watering and grading, the podium, the newspaper after the meeting and track records.
