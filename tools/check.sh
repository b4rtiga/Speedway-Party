#!/bin/sh
# Sprawdzenie skladni i typow kodu gry kompilatorem Clang (z pakietu pip "ziglang") dla 68020.
# Nie buduje pliku wykonywalnego - do tego jest Makefile z Bebbo GCC.
# Atrapy naglowkow NDK: tools/ndkstub/.
cd "$(dirname "$0")/.." || exit 1
status=0
for f in src/*.c; do
    python -m ziglang clang -target m68k-unknown-none -mcpu=68020 -fsyntax-only -std=gnu99 \
        -ffreestanding -nostdinc -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
        -Wno-missing-field-initializers -Itools/ndkstub "$f" || status=1
done
[ $status -eq 0 ] && echo "OK: wszystkie pliki bez bledow"
exit $status
