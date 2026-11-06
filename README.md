# Shapr Linux

Natywny, pen-first CAD dla Linuksa w duchu Shapr3D (docelowo: Framework 12 z rysikiem).
C++20 · Qt 6 · OpenCASCADE 7.9 · Eigen.

## Budowanie

```sh
sudo pacman -S --needed qt6-base qt6-tools opencascade eigen cmake ninja
cmake -S . -B build -G Ninja
cmake --build build
./build/shapr_linux
```

OpenCASCADE z Archa jest zbudowany pod GLX, więc aplikacja sama wymusza `QT_QPA_PLATFORM=xcb`
(na Waylandzie działa przez XWayland). Domyślnie używany jest profil OpenGL compatibility
(w core Mesa ogranicza grubość linii do 1 px); profil core: `SHAPR_GL_CORE=1`.
`SHAPR_SCREENSHOT=plik.png` renderuje jedną klatkę, zapisuje ją i kończy działanie.
`SHAPR_DEBUG_INPUT=1` wypisuje urządzenia wejścia i zdarzenia scrolla/gestów,
`SHAPR_INVERT_SCROLL=1` odwraca kierunek obrotu/przesuwania z touchpada.

Testy rdzenia: `ctest --test-dir build` (albo `./build/test_sketch`).

## Szkicowanie

| Narzędzie | Skrót | Jak |
|---|---|---|
| Zaznacz | `S` | dotknij elementu, Shift dodaje; `Delete` usuwa |
| Linia | `L` | tap-tap rysuje łamaną (kończy się na istniejącym punkcie lub `Esc`/`Enter`); przeciągnięcie = jeden odcinek |
| Prostokąt | `R` | dwa przeciwległe narożniki |
| Okrąg | `C` | środek, potem punkt na obwodzie |
| Łuk | `A` | początek, koniec, punkt na łuku |

Przyciąganie: punkty → początek układu → środki odcinków → wyrównanie w poziomie/pionie
(najpierw do punktu startowego narzędzia). `Alt` wyłącza przyciąganie.
`Esc` przerywa operację, drugi `Esc` wraca do zaznaczania. `Ctrl+Z` / `Ctrl+Shift+Z` cofnij/ponów.
Płaszczyznę szkicu (XY / XZ / YZ) wybierasz na pasku; siatka podąża za nią.

## Sterowanie widokiem

| Wejście | Akcja |
|---|---|
| Rysik (końcówka) | aktywne narzędzie szkicu |
| Rysik + przycisk | obrót widoku |
| 1 palec | obrót widoku |
| 2 palce | przesuwanie + pinch zoom |
| Mysz LPM / PPM / ŚPM / kółko | narzędzie szkicu / obrót / przesuwanie / zoom |
| Shift+PPM, Ctrl+PPM | przesuwanie, zoom |
| Touchpad: przewijanie 2 palcami | obrót widoku (TrackPoint ze środkowym przyciskiem tak samo) |
| Touchpad: Shift + 2 palce / 3 palce | przesuwanie |
| Touchpad: szczypanie, Ctrl + 2 palce | zoom |
| `F` | dopasuj widok |

## Plan

- [x] **M0** – okno Qt + widok OCCT, nawigacja mysz/touchpad/dotyk/rysik
- [x] **M1** – szkic na płaszczyźnie rysikiem (linia, prostokąt, okrąg, łuk), snapping
- [ ] **M2** – więzy i wymiary, solver
- [ ] **M3** – wykrywanie profili, push/pull, boolean
- [ ] **M4** – parametryczna historia, zapis projektu, eksport STEP/STL
- [ ] **M5** – szkic na ścianie, fillet/chamfer/shell
- [ ] **M6** – polish pod Framework 12 (obrót ekranu, palm rejection, tryb tabletu)
