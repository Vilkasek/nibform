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
(na Waylandzie działa przez XWayland). Gdyby kontekst OpenGL się nie podpinał: `SHAPR_GL_COMPAT=1`.
`SHAPR_SCREENSHOT=plik.png` renderuje jedną klatkę, zapisuje ją i kończy działanie.
`SHAPR_DEBUG_INPUT=1` wypisuje urządzenia wejścia i zdarzenia scrolla/gestów,
`SHAPR_INVERT_SCROLL=1` odwraca kierunek obrotu/przesuwania z touchpada.

## Sterowanie

| Wejście | Akcja |
|---|---|
| Rysik (końcówka) | zaznaczanie; docelowo szkicowanie |
| Rysik + przycisk | obrót widoku |
| 1 palec | obrót widoku |
| 2 palce | przesuwanie + pinch zoom |
| Mysz LPM / PPM / ŚPM / kółko | zaznaczanie / obrót / przesuwanie / zoom |
| Shift+PPM, Ctrl+PPM | przesuwanie, zoom |
| Touchpad: przewijanie 2 palcami | obrót widoku (TrackPoint ze środkowym przyciskiem tak samo) |
| Touchpad: Shift + 2 palce / 3 palce | przesuwanie |
| Touchpad: szczypanie, Ctrl + 2 palce | zoom |
| `F`, `Esc` | dopasuj widok, odznacz |

## Plan

- [x] **M0** – okno Qt + widok OCCT, nawigacja mysz/touchpad/dotyk/rysik
- [ ] **M1** – szkic na płaszczyźnie rysikiem (linia, prostokąt, okrąg, łuk), snapping
- [ ] **M2** – więzy i wymiary, solver
- [ ] **M3** – wykrywanie profili, push/pull, boolean
- [ ] **M4** – parametryczna historia, zapis projektu, eksport STEP/STL
- [ ] **M5** – szkic na ścianie, fillet/chamfer/shell
- [ ] **M6** – polish pod Framework 12 (obrót ekranu, palm rejection, tryb tabletu)
