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

Testy rdzenia: `ctest --test-dir build` (albo `./build/test_sketch`, `./build/test_solver`, `./build/test_trim`, `./build/test_offset`, `./build/test_value_editor`).

## Szkicowanie

| Narzędzie | Skrót | Jak |
|---|---|---|
| Zaznacz | `S` | dotknij elementu, Shift dodaje; `Delete` usuwa |
| Linia | `L` | tap-tap rysuje łamaną (kończy się na istniejącym punkcie lub `Esc`/`Enter`); przeciągnięcie = jeden odcinek |
| Prostokąt | `R` | dwa przeciwległe narożniki |
| Okrąg | `C` | środek, potem punkt na obwodzie |
| Łuk | `A` | początek, koniec, punkt na łuku |
| Odsunięcie | `U` | dotknij krzywej – bierze cały połączony łańcuch (lub okrąg); kopia podąża za kursorem (strona i odległość), kolejne dotknięcie pyta o dokładną wartość. Narożniki są przecinane na nowo, linie dostają „równoległe”, łuki dzielą środek z oryginałem |
| Przytnij | `X` | jak w Fusionie: podświetla na czerwono fragment między najbliższymi przecięciami, dotknięcie go usuwa; przeciągnięcie usuwa wszystko po drodze (jeden krok cofania). Krzywa bez przecięć znika cała, okrąg zamienia się w łuk |

Przyciąganie: punkty → początek układu → środki odcinków → wyrównanie w poziomie/pionie
(najpierw do punktu startowego narzędzia). `Alt` wyłącza przyciąganie.
Przyciąganie zostawia więzy: wyrównana linia dostaje „poziomo”/„pionowo”, prostokąt – oba,
punkt na środku odcinka – „środek”, punkt w początku układu – „zablokuj”.
`Esc` przerywa operację, drugi `Esc` wraca do zaznaczania. `Ctrl+Z` / `Ctrl+Shift+Z` cofnij/ponów.
Płaszczyznę szkicu (XY / XZ / YZ) wybierasz na pasku; siatka podąża za nią.

## Więzy i wymiary

W trybie Zaznacz dotykaj punktów, linii i łuków – każde dotknięcie dodaje element do zaznaczenia,
dotknięcie pustego miejsca je czyści. Potem wybierz więz z drugiego paska (albo menu *Więzy*);
przyciski, które nie pasują do zaznaczenia, są wyszarzone.

| Więz | Skrót | Zaznaczenie |
|---|---|---|
| Wymiar | `D` | linia / 2 punkty → odległość, punkt + linia → odległość od linii, 2 równoległe linie → odstęp (dodaje „równoległe”, jeśli trzeba), 2 inne linie → kąt, okrąg → średnica, łuk → promień |
| Poziomo / Pionowo | `H` / `V` | linie albo 2 punkty |
| Równoległe / Prostopadłe | `P` / `N` | 2+ / 2 linie |
| Równe | `E` | 2+ linie (długość) albo 2+ okręgi/łuki (promień) |
| Styczne | `T` | linia i okrąg/łuk albo 2 okręgi/łuki |
| Współśrodkowe | `O` | 2+ okręgi/łuki |
| Wspólny punkt | `I` | 2 punkty (zostają połączone) albo punkt i krzywa |
| Środek | `M` | punkt i linia |
| Zablokuj | `Z` | punkty albo krzywe (przypina je w miejscu) |

Dotknięcie zaznaczonego wymiaru, dwuklik na nim albo `Enter` otwiera pole wartości przy etykiecie:
`Enter` lub ✓ zatwierdza, `Esc` lub ✕ anuluje (przecinek i kropka działają jako separator). Przeciągnięcie punktu lub krzywej
przesuwa geometrię, a solver pilnuje więzów. Geometria w pełni określona robi się zielona,
pasek stanu pokazuje liczbę pozostałych stopni swobody. Więzy sprzeczne albo nadmiarowe
są odrzucane z komunikatem. Glify więzów i etykiety wymiarów można zaznaczyć i usunąć (`Delete`).

Solver (`src/sketch/Solver.cpp`, tylko Eigen): Gauss-Newton z krokiem o minimalnej normie,
więc przy każdej zmianie geometria przesuwa się jak najmniej; stopnie swobody i „w pełni określone”
elementy wynikają z jądra jakobianu (SVD).

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
| `1`–`6` | ściany: góra, przód, prawo, tył, lewo, dół |
| `Ctrl+1`–`8` | narożniki: górne (przód-prawo, tył-prawo, tył-lewo, przód-lewo), potem dolne w tej samej kolejności; `Ctrl+1` = izometria |
| `Shift+1`–`0` | krawędzie: górne (przód, prawo, tył, lewo), pionowe (przód-prawo, tył-prawo, tył-lewo, przód-lewo), dół-przód, dół-tył |
| `Ctrl+Shift+9` / `0` | krawędzie dół-prawo / dół-lewo |
| Kostka w rogu | kliknij ścianę, krawędź lub narożnik |

Wszystkie widoki są też w menu *Widok*. Tam też *Motyw*: systemowy (podąża za jasnym/ciemnym
trybem pulpitu, odczytywanym przez xdg-desktop-portal), jasny albo ciemny; wybór jest zapamiętywany.

## Plan

- [x] **M0** – okno Qt + widok OCCT, nawigacja mysz/touchpad/dotyk/rysik
- [x] **M1** – szkic na płaszczyźnie rysikiem (linia, prostokąt, okrąg, łuk), snapping
- [x] **M2** – więzy i wymiary, solver
- [ ] **M3** – wykrywanie profili, push/pull, boolean
- [ ] **M4** – parametryczna historia, zapis projektu, eksport STEP/STL
- [ ] **M5** – szkic na ścianie, fillet/chamfer/shell
- [ ] **M6** – polish pod Framework 12 (obrót ekranu, palm rejection, tryb tabletu)
