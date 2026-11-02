# Music Maker

This is an AR proyect for creating music using markers

## Dependencies

- ARToolkit
- libao (install with `sudo apt-get install libao-dev` on Ubuntu)
- libsndfile (install with `sudo apt-get install libsndfile-dev` on Ubuntu)

## Printing the markers

The markers are in the `printables` folder as PDFs. Print them at actual size (the `.xcf` files are the GIMP sources).

- `multimarker.pdf` (A2): the board. The 12 markers around the edge let the camera track the sheet, and the space inside them is a grid of 8 columns (steps) by 7 rows (the notes C3 to B3). A bar moves across the columns and plays every instrument marker in the current column.
- `Note.pdf`, `Piano.pdf` and `Marimba.pdf` (A4): instrument markers, 12 copies each. Place one on the board to add a note played with the synth, the piano or the marimba. The row sets the pitch, and rotating the marker changes the note length (half, one, one and a half or two steps).
- `Metronome.pdf` (A4): sets the tempo. The bar only moves while at least one metronome marker is visible. With one it advances a step every second, with two every half second, and with three or more every quarter second.

`Xilophone.pdf` is an older design and is not used.
