# Third-party notices

## Fenshot

`boarddetector.cpp` / `boarddetector.h` contain a C++ adaptation of the
Fenshot chessboard detector. Fenshot is distributed under the MIT License:

https://github.com/scoriiu/fenshot

The `assets/models/chess-tiles-v2.onnx` model is shipped locally by this
application from the same project.

## tensorflow_chessbot

The detector is based on the MIT-licensed chessboard finder originally
published by Elucidation:

https://github.com/Elucidation/tensorflow_chessbot

## Crafty opening-book compatibility

`assets/books/book.bin` is read using the Crafty opening-book format. The
Crafty-compatible Zobrist key table in `craftyhashkeys.h` is derived from
Crafty's `data.c` table:

https://github.com/MichaelB7/Crafty

Crafty is copyright 1996-2020 by Robert M. Hyatt and the Crafty team. Crafty's
README states that it is provided under restrictive, personal-use terms rather
than a permissive open-source license. The provenance and redistribution terms
of the supplied `assets/books/book.bin` should be verified separately before
redistributing it.
