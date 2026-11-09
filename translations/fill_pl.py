#!/usr/bin/env python3
"""Fills translations/nibform_pl.ts with the Polish texts below (run after update_translations
when sources change; new texts without an entry here are reported and stay unfinished)."""
import sys
import xml.etree.ElementTree as ET

PL = {
 "MainWindow": {
  "Make a body first: a drawing shows the bodies": "Najpierw utwórz bryłę: rysunek pokazuje bryły",
  "Export Drawing": "Eksportuj rysunek", "Drawing": "Rysunek",
  "A technical drawing of the bodies: views, sections, dimensions; PDF, SVG or printed":
      "Rysunek techniczny brył: rzuty, przekroje, wymiary; PDF, SVG albo wydruk",
  "DRAWING": "RYSUNEK", "Back to Model": "Wróć do modelu",
  "Drag views, dimensions and notes; double-tap one for its options": "Przeciągaj rzuty, wymiary i notatki; dwuklik otwiera ich opcje",
  "Tap two corners or centers of a view, or a hole's rim for its diameter":
      "Dotknij dwóch narożników albo środków w rzucie, albo krawędzi otworu, by podać jego średnicę",
  "Note": "Notatka", "Tap where a note goes": "Dotknij miejsca na notatkę", "Section…": "Przekrój…", "Add View": "Dodaj rzut",
  "A4 Landscape": "A4 poziomo", "A4 Portrait": "A4 pionowo", "A3 Landscape": "A3 poziomo", "A3 Portrait": "A3 pionowo",
  "A2 Landscape": "A2 poziomo", "Title…": "Tytuł…", "Sheet": "Arkusz", "PDF": "PDF", "Export the sheet as PDF": "Eksportuj arkusz do PDF",
  "SVG": "SVG", "Export the sheet as SVG": "Eksportuj arkusz do SVG", "Print": "Drukuj",
  "Import…": "Importuj…", "A STEP, IGES or STL file: as bodies to build on, or as a reference":
      "Plik STEP, IGES albo STL: jako bryły do dalszej pracy albo jako odniesienie",
  "Export": "Eksportuj", "STEP…": "STEP…", "IGES…": "IGES…", "STL…": "STL…", "3MF (3D printing)…": "3MF (druk 3D)…",
  "Copy": "Kopiuj", "Paste": "Wklej",
  "Copies the selected curves (in a sketch) or bodies, to paste here or in another project":
      "Kopiuje zaznaczone krzywe (w szkicu) albo bryły, by wkleić je tu albo w innym projekcie",
  "Import Drawing (DXF / SVG)…": "Importuj rysunek (DXF / SVG)…", "Import Drawing": "Importuj rysunek",
  "Drawings (*.dxf *.svg)": "Rysunki (*.dxf *.svg)", "Export Sketch (DXF / SVG)…": "Eksportuj szkic (DXF / SVG)…",
  "Export Sketch": "Eksportuj szkic",
  "Nibform projects (*.nibform);;3D models (*.step *.stp *.iges *.igs *.stl)":
      "Projekty Nibform (*.nibform);;Modele 3D (*.step *.stp *.iges *.igs *.stl)",
  "Import": "Import", "3D models (*.step *.stp *.iges *.igs *.stl)": "Modele 3D (*.step *.stp *.iges *.igs *.stl)",
  "Import %1 as bodies to build on, or as a reference to look at?":
      "Zaimportować %1 jako bryły do dalszej pracy, czy jako odniesienie do podglądu?",
  "Reference": "Odniesienie",
  "The checkbox shows or hides a body; double-click the name to rename it":
      "Pole wyboru pokazuje albo ukrywa bryłę; dwuklik na nazwie zmienia ją",
  "Choose Color…": "Wybierz kolor…", "Body Color": "Kolor bryły", "Default Color": "Domyślny kolor",
  "Move": "Przesuń", "Measure": "Zmierz", "Section": "Przekrój",
  "Move (T): tap bodies, then drag the arrows and rings, or tap a corner, round edge or face of them and then where it goes; Enter types offsets and applies (or copies)":
      "Przesuń (T): dotknij brył, potem przeciągnij strzałki i pierścienie albo dotknij ich narożnika, okrągłej "
      "krawędzi lub ściany, a potem miejsca docelowego; Enter pozwala wpisać przesunięcie i zatwierdza (albo kopiuje)",
  "Measure (I): tap a face, edge or corner, and a second one to measure between them; the body's volume and mass properties too":
      "Zmierz (I): dotknij ściany, krawędzi albo narożnika, a potem drugiego, by zmierzyć między nimi; "
      "pokazuje też objętość i właściwości masowe bryły",
  "Section: tap a flat face or a plane to cut the view open along it; View → Section View turns it off":
      "Przekrój: dotknij płaskiej ściany albo płaszczyzny, by przeciąć widok wzdłuż niej; Widok → Przekrój wyłącza go",
  "Units": "Jednostki", "Millimetres": "Milimetry", "Inches": "Cale", "Section View": "Przekrój",
  "Bodies": "Bryły", "Body": "Bryła", "Color": "Kolor", "Body List": "Lista brył",
  "Revolve": "Obrót", "Sweep": "Przeciągnij", "Loft": "Wyciągnij przez profile", "Hole": "Otwór",
  "Revolve (R): tap sketch regions, then the axis (a sketch line, an edge or an axis)":
      "Obrót (R): dotknij obszarów szkicu, potem osi (linii szkicu, krawędzi albo osi)",
  "Sweep (Shift+W): tap the profile region, then a curve of the path sketch":
      "Przeciągnij (Shift+W): dotknij obszaru profilu, potem krzywej szkicu ścieżki",
  "Loft (Shift+F): tap profile regions on different planes in order, then Enter":
      "Wyciągnij przez profile (Shift+F): dotknij po kolei obszarów na różnych płaszczyznach, potem Enter",
  "Hole (H): tap a flat face where it goes (sketch points and round edges' centers snap); through, blind, counterbored or countersunk":
      "Otwór (H): dotknij płaskiej ściany tam, gdzie ma być (przyciąga do punktów szkicu i środków okrągłych "
      "krawędzi); przelotowy, nieprzelotowy, z pogłębieniem walcowym albo stożkowym",
  "Mirror (M): tap bodies, Enter, then the mirror plane (a flat face or a plane)":
      "Lustro (M): dotknij brył, Enter, potem płaszczyzny odbicia (płaskiej ściany albo płaszczyzny)",
  "Linear Pattern (Shift+L): tap bodies, then the direction (an edge, axis or sketch line). To repeat a feature (a hole, a boss), use Pattern in its history menu":
      "Szyk liniowy (Shift+L): dotknij brył, potem kierunku (krawędzi, osi albo linii szkicu). Żeby powtórzyć "
      "operację (otwór, występ), użyj Szyku w jej menu w historii",
  "Circular Pattern (Shift+O): tap bodies, then the axis (a round or straight edge, an axis). To repeat a feature, use Pattern in its history menu":
      "Szyk kołowy (Shift+O): dotknij brył, potem osi (okrągłej albo prostej krawędzi, osi). Żeby powtórzyć "
      "operację, użyj Szyku w jej menu w historii",
  "Offset Plane": "Płaszczyzna odsunięta", "Plane at Angle": "Płaszczyzna pod kątem",
  "Plane Through 3 Points": "Płaszczyzna przez 3 punkty", "Axis": "Oś",
  "Offset Plane: a construction plane parallel to a flat face or a plane; select it to start a sketch on it, or mirror about it":
      "Płaszczyzna odsunięta: płaszczyzna konstrukcyjna równoległa do płaskiej ściany albo płaszczyzny; "
      "zaznacz ją, by zacząć na niej szkic albo odbić względem niej",
  "Plane at Angle: a plane turned about an edge or axis": "Płaszczyzna pod kątem: obrócona wokół krawędzi albo osi",
  "Plane Through 3 Points: through three corners of bodies": "Płaszczyzna przez 3 punkty: przez trzy narożniki brył",
  "Axis: along a straight edge, through a round one's center, or through two corners":
      "Oś: wzdłuż prostej krawędzi, przez środek okrągłej albo przez dwa narożniki",
  "Linear Pattern…": "Szyk liniowy…", "Circular Pattern…": "Szyk kołowy…",
  "Freehand": "Odręcznie",
  "Freehand (P): draw roughly and get the clean shape (line, corners, rectangle, circle, arc); scribble over something to delete it":
      "Odręcznie (P): narysuj z grubsza, a powstanie czysty kształt (linia, łamana, prostokąt, okrąg, łuk); "
      "zamaż coś, by to usunąć",
  "Nothing to undo": "Nie ma czego cofnąć", "Nothing to redo": "Nie ma czego ponowić",
  "Undone (two-finger tap)": "Cofnięto (stuknięcie dwoma palcami)",
  "Redone (three-finger tap)": "Ponowiono (stuknięcie trzema palcami)",
  "&Help": "Pomo&c", "About Nibform": "O programie Nibform", "About Qt": "O Qt", "Open Recent": "Otwórz ostatnie",
  "Autosave failed: %1": "Autozapis nie powiódł się: %1",
  "Nibform closed unexpectedly. Restore the unsaved project autosaved at %1?":
      "Nibform zamknął się niespodziewanie. Przywrócić niezapisany projekt z autozapisu o %1?",
  "Nibform closed unexpectedly. Restore the unsaved changes to %1, autosaved at %2?":
      "Nibform zamknął się niespodziewanie. Przywrócić niezapisane zmiany w %1 z autozapisu o %2?",
  "Restore Unsaved Work": "Przywróć niezapisaną pracę", "Restore": "Przywróć", "Discard": "Odrzuć",
  "Later": "Później", "Restored the unsaved project": "Przywrócono niezapisany projekt", "Restored %1": "Przywrócono %1",
  "No recent projects": "Brak ostatnich projektów", "Clear List": "Wyczyść listę",
  "Pen-first parametric CAD for Linux.": "Parametryczny CAD dla Linuksa do pracy rysikiem.",
  "Free software under the GNU General Public License, version 3 or later. It comes with no warranty.":
      "Wolne oprogramowanie na licencji GNU General Public License w wersji 3 lub nowszej. Bez żadnej gwarancji.",
  "Quick": "Szybkie", "History": "Historia", "OpenGL error": "Błąd OpenGL", "&File": "&Plik", "New": "Nowy",
  "Open…": "Otwórz…", "Save": "Zapisz", "Save As…": "Zapisz jako…",
  "Quit": "Zakończ", "&Edit": "&Edycja", "Undo": "Cofnij", "Redo": "Ponów",
  "Delete Selected": "Usuń zaznaczone", "Clear Sketch": "Wyczyść szkic", "Cancel / Deselect": "Przerwij / odznacz",
  "Finish / Edit Dimension": "Zakończ / edytuj wymiar", "Model": "Bryła", "Sketch": "Szkic", "View": "Widok",
  "MODEL": "BRYŁA", "SKETCH": "SZKIC", "Select": "Zaznacz", "New Sketch": "Stwórz szkic",
  "New Sketch (K): on the selected flat face, otherwise on the ground; the arrow next to it picks the plane":
      "Stwórz szkic (K) – na zaznaczonej płaskiej ścianie, inaczej na podłożu; strzałka obok: wybór płaszczyzny",
  "On the Selected Face": "Na zaznaczonej ścianie", "Ground (XY)": "Podłoże (XY)", "Front (XZ)": "Przód (XZ)",
  "Right (YZ)": "Prawo (YZ)", "Edit Sketch": "Edytuj szkic",
  "Opens the sketch whose region is selected": "Otwiera szkic, którego obszar jest zaznaczony",
  "Extrude": "Wyciągnij", "Fillet": "Zaokrąglij", "Chamfer": "Fazuj", "Shell": "Skorupa", " Operation: ": " Bryła: ",
  "Automatic: pushing into a body cuts, pulling out of it or next to it joins,\nand away from bodies a new body is made":
      "Automatycznie: wciśnięcie w bryłę odejmuje, wyciągnięcie z niej albo przy niej dodaje,\na z dala od brył powstaje nowa bryła",
  "Automatic": "Automatycznie", "New Body": "Nowa bryła", "Join": "Dodaj", "Cut": "Odejmij", "Line": "Linia",
  "Rectangle": "Prostokąt", "Circle": "Okrąg", "Arc": "Łuk", "Trim": "Przytnij", "Offset": "Odsunięcie",
  "Construction": "Konstrukcyjna",
  "Construction (Q): the selected curves become reference geometry (dashed, never bounding a region), or back to regular":
      "Konstrukcyjna (Q) – zaznaczone krzywe stają się pomocnicze (przerywane, nie tworzą obszarów) albo z powrotem zwykłe",
  "Finish Sketch": "Zamknij szkic", "Back to the model (Ctrl+Enter)": "Wraca do brył (Ctrl+Enter)", "Fit": "Dopasuj",
  "Fit the view (F)": "Dopasuj widok (F)", "&View": "Wi&dok", "Tablet Mode": "Tryb tabletu",
  "Bigger buttons and pick areas, an Undo / Delete / Done bar on the side and a numeric keypad for values":
      "Większe przyciski i pola trafień, pasek Cofnij / Usuń / Gotowe z boku i klawiatura numeryczna przy wartościach",
  "Palm Rejection": "Odrzucanie dłoni",
  "Show Extruded Regions": "Pokaż wyciągnięte obszary",
  "Sketch regions already extruded are hidden, so the faces under them can be picked; show them to extrude one again":
      "Wyciągnięte już obszary szkiców są ukryte, żeby dało się wybrać ściany pod nimi; pokaż je, by wyciągnąć któryś ponownie",
  "Touch is ignored while the pen is near the screen, and for wide contacts":
      "Dotyk jest ignorowany, gdy rysik jest przy ekranie, i dla szerokich kontaktów",
  "Theme": "Motyw", "System": "Systemowy", "Light": "Jasny", "Dark": "Ciemny", "Faces": "Ściany", "Top": "Góra",
  "Front": "Przód", "Right": "Prawo", "Back": "Tył", "Left": "Lewo", "Bottom": "Dół", "Corners": "Narożniki",
  "Top-front-right": "Góra-przód-prawo", "Top-back-right": "Góra-tył-prawo", "Top-back-left": "Góra-tył-lewo",
  "Top-front-left": "Góra-przód-lewo", "Bottom-front-right": "Dół-przód-prawo", "Bottom-back-right": "Dół-tył-prawo",
  "Bottom-back-left": "Dół-tył-lewo", "Bottom-front-left": "Dół-przód-lewo", "Edges": "Krawędzie",
  "Top-front": "Góra-przód", "Top-right": "Góra-prawo", "Top-back": "Góra-tył", "Top-left": "Góra-lewo",
  "Front-right": "Przód-prawo", "Back-right": "Tył-prawo", "Back-left": "Tył-lewo", "Front-left": "Przód-lewo",
  "Bottom-front": "Dół-przód", "Bottom-back": "Dół-tył", "Bottom-right": "Dół-prawo", "Bottom-left": "Dół-lewo",
  "Isometric": "Izometria", "Constraints": "Więzy", "&Bodies": "&Bryły", "Union": "Połącz",
  "The selected bodies into one": "Zaznaczone bryły w jedną",
  "Subtracts the other selected bodies from the first": "Od pierwszej zaznaczonej bryły odejmuje pozostałe",
  "Intersect": "Część wspólna", "Keeps only what the selected bodies share": "Zostawia tylko to, co zaznaczone bryły mają wspólne",
  "&Constraints": "&Więzy", "Dimension": "Wymiar",
  "A line or 2 points: distance · a point and a line: distance to the line · 2 parallel lines: gap · 2 other lines: angle · circle: diameter · arc: radius\nWith nothing selected: a tool, tap what to dimension":
      "Linia lub 2 punkty: odległość · punkt i linia: odległość od linii · 2 równoległe linie: odstęp · 2 inne linie: kąt · okrąg: średnica · łuk: promień\nBez zaznaczenia: narzędzie, dotknij tego, co zwymiarować",
  "Horizontal": "Poziomo", "Lines or 2 points": "Linie albo 2 punkty", "Vertical": "Pionowo", "Parallel": "Równoległe",
  "2+ lines": "2+ linie", "Perpendicular": "Prostopadłe", "2 lines": "2 linie", "Equal": "Równe",
  "2+ lines (length) or 2+ circles/arcs (radius)": "2+ linie (długość) albo 2+ okręgi/łuki (promień)",
  "Tangent": "Styczne", "A line and a circle/arc, or 2 circles/arcs": "Linia i okrąg/łuk albo 2 okręgi/łuki",
  "Concentric": "Współśrodkowe", "2+ circles/arcs": "2+ okręgi/łuki", "Coincident": "Wspólny punkt",
  "2 points (merges them) or a point and a curve (point on the curve)":
      "2 punkty (łączy je) albo punkt i krzywa (punkt na krzywej)",
  "Midpoint": "Środek", "A point and a line": "Punkt i linia", "Fix": "Zablokuj",
  "Points or curves: pins them where they are": "Punkty albo krzywe: przypina je w obecnym miejscu",
  "empty: create a sketch and extrude it": "pusta – stwórz szkic i wyciągnij go",
  "Double-click: edit the sketch": "Dwuklik: edytuj szkic", "Double-click: change the value": "Dwuklik: zmień wartość",
  "Edit": "Edytuj", "Delete": "Usuń", "Cancel": "Anuluj", "Done": "Gotowe", "Untitled": "Bez nazwy",
  "Unsaved Changes": "Niezapisane zmiany", "Save the changes to the project?": "Zapisać zmiany w projekcie?",
  "Open Project": "Otwórz projekt", "Nibform projects (*.nibform)": "Projekty Nibform (*.nibform)",
  "Can't Open": "Nie można otworzyć", "Opened %1": "Otwarto %1", "Can't Save": "Nie można zapisać",
  "Saved %1": "Zapisano %1", "Save Project": "Zapisz projekt",
  "Export Failed": "Eksport nieudany", "Exported %1": "Wyeksportowano %1",
  "Sketch on Face": "Szkic na ścianie", "Pen%1: pressure %2%3": "Rysik%1: nacisk %2%3", " [eraser]": " [gumka]",
  " [button]": " [przycisk]",
  "S&ketch": "S&zkic", "Polygon": "Wielokąt", "Slot": "Rowek", "Ellipse": "Elipsa", "Spline": "Splajn",
  "Mirror": "Lustro", "Linear Pattern": "Szyk liniowy", "Circular Pattern": "Szyk kołowy", "Project": "Rzutuj",
  "Mirror (Shift+M): select curves, then tap the line to mirror them about; the copy stays symmetric":
      "Lustro (Shift+M) – zaznacz krzywe, potem dotknij linii, względem której je odbić; kopia pozostaje symetryczna",
  "Linear Pattern (Shift+L): select curves, then pick the step between copies and their count":
      "Szyk liniowy (Shift+L) – zaznacz krzywe, potem wskaż odstęp między kopiami i ich liczbę",
  "Circular Pattern (Shift+O): select curves, then pick the center and the count (a full turn)":
      "Szyk kołowy (Shift+O) – zaznacz krzywe, potem wskaż środek i liczbę (na pełnym obrocie)",
  "Project (Shift+K): tap body edges or faces to copy them into the sketch, pinned in place":
      "Rzutuj (Shift+K) – dotykaj krawędzi albo ścian brył, by skopiować je do szkicu, unieruchomione",
  "Reference Image": "Obraz podkładowy", "Insert Image…": "Wstaw obraz…", "Insert Image": "Wstaw obraz",
  "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;All files (*)":
      "Obrazy (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;Wszystkie pliki (*)",
  "Puts a picture on the sketch plane; then tap two points on it a known distance apart to scale it":
      "Kładzie obraz na płaszczyźnie szkicu; potem dotknij na nim dwóch punktów o znanej odległości, by go przeskalować",
  "Move / Scale Image": "Przesuń / skaluj obraz", "Opacity": "Krycie", "Remove Image": "Usuń obraz", "Image": "Obraz",
  "Reference image: insert a picture to trace over, scale it by two points":
      "Obraz podkładowy – wstaw obraz do odrysowania, przeskaluj go dwoma punktami",
  "Horizontal Dimension": "Wymiar poziomy", "Vertical Dimension": "Wymiar pionowy",
  "A line or 2 points: the distance along X": "Linia albo 2 punkty: odległość wzdłuż X",
  "A line or 2 points: the distance along Y": "Linia albo 2 punkty: odległość wzdłuż Y",
  "Symmetric": "Symetria",
  "2 points and a line: the points mirror each other about the line":
      "2 punkty i linia: punkty są swoimi odbiciami względem linii",
 },
 "Model": {
  "Could not prepare a body for IGES": "Nie udało się przygotować bryły do IGES",
  "Could not write the IGES file": "Nie udało się zapisać pliku IGES",
  "Could not mesh the bodies for 3MF": "Nie udało się utworzyć siatki brył do 3MF",
  "Could not write the 3MF file": "Nie udało się zapisać pliku 3MF", "The file has no geometry": "Plik nie zawiera geometrii",
  "Could not read the IGES file": "Nie udało się odczytać pliku IGES", "Could not read the STL file": "Nie udało się odczytać pliku STL",
  "The file could not be read": "Nie udało się odczytać pliku",
  "Unknown file type (STEP, IGES or STL)": "Nieznany rodzaj pliku (STEP, IGES albo STL)",
  "The imported geometry can't be read": "Nie da się odczytać zaimportowanej geometrii",
  "The file has no closed solids; import it as a reference": "Plik nie ma zamkniętych brył; zaimportuj go jako odniesienie",
  "The move isn't a rigid motion": "Przesunięcie nie jest ruchem sztywnym",
  "Pick the bodies to move": "Wybierz bryły do przesunięcia",
  "The sketch region it was made from is gone": "Obszar szkicu, z którego powstała, zniknął",
  "The axis is gone": "Oś zniknęła", "The path is gone": "Ścieżka zniknęła",
  "The mirror plane is gone": "Płaszczyzna odbicia zniknęła", "Pick the bodies to mirror": "Wybierz bryły do odbicia",
  "The pattern's direction is gone": "Kierunek szyku zniknął",
  "A pattern needs two instances or more": "Szyk potrzebuje co najmniej dwóch wystąpień",
  "The patterned feature didn't apply": "Powtarzana operacja się nie udała",
  "Pick the bodies to pattern": "Wybierz bryły do szyku", "Pick a flat face for the hole": "Wybierz płaską ścianę na otwór",
  "The face the hole was on is gone": "Ściana, na której był otwór, zniknęła",
  "The sketch point the hole was on is gone": "Punkt szkicu, na którym był otwór, zniknął",
  "Pick where the hole goes": "Wskaż, gdzie ma być otwór", "The hole doesn't reach any body": "Otwór nie sięga żadnej bryły",
  "The plane it was made from is gone": "Płaszczyzna, z której powstała, zniknęła", "Pick three points": "Wskaż trzy punkty",
  "The three points are in a line": "Trzy punkty leżą na jednej prostej",
  "The edge it was made from is gone": "Krawędź, z której powstała, zniknęła",
  "Pick two different points": "Wskaż dwa różne punkty", "Nothing to revolve": "Nie ma czego obracać",
  "Can't revolve: the axis must not run through the profile": "Nie można obrócić: oś nie może przechodzić przez profil",
  "Pick the profile and the path": "Wybierz profil i ścieżkę",
  "Can't sweep: the path may be too tight for the profile": "Nie można przeciągnąć: ścieżka może być za ciasna dla profilu",
  "A loft needs two profiles or more": "Potrzeba co najmniej dwóch profili",
  "Can't loft through these profiles": "Nie można wyciągnąć przez te profile",
  "No bodies to export": "Brak brył do eksportu",
  "Could not prepare a body for STEP": "Nie udało się przygotować bryły do zapisu STEP",
  "Could not write the STEP file": "Nie udało się zapisać pliku STEP",
  "Could not mesh the bodies for STL": "Nie udało się zbudować siatki do STL",
  "Could not write the STL file": "Nie udało się zapisać pliku STL",
  "Could not read the STEP file": "Nie udało się odczytać pliku STEP",
  "The sketch region that was extruded is gone": "Nie ma już obszaru szkicu, który był wyciągany",
  "The face that was extruded is gone": "Nie ma już ściany, która była wyciągana",
  "The edge that was rounded is gone": "Nie ma już krawędzi, która była zaokrąglana",
  "The face that was opened is gone": "Nie ma już ściany, która była otwierana",
  "The body this applied to is gone": "Nie ma już bryły, której to dotyczyło",
  "Pick edges of a body": "Zaznacz krawędzie bryły", "Nothing to extrude": "Nic do wyciągnięcia",
  "Could not extrude the profile": "Nie udało się wyciągnąć profilu",
  "Could not merge the profiles": "Nie udało się połączyć profili",
  "Could not join with the body": "Nie udało się dodać do bryły",
  "The cut doesn't reach any body": "Wycięcie nie trafia w żadną bryłę",
  "Could not cut from the body": "Nie udało się odjąć od bryły",
  "Can't fillet: the radius is too big for these edges": "Nie da się zaokrąglić – promień za duży dla tych krawędzi",
  "Can't chamfer: the distance is too big for these edges": "Nie da się sfazować – odległość za duża dla tych krawędzi",
  "Pick the faces to open": "Zaznacz ściany do otwarcia",
  "Can't shell this body; try thinner walls": "Nie da się zrobić skorupy tej bryły – spróbuj cieńszej ścianki",
  "Pick at least two bodies": "Zaznacz co najmniej dwie bryły",
  "The body operation failed": "Operacja na bryłach nie powiodła się",
  "The bodies don't intersect": "Bryły nie mają części wspólnej",
 },
 "OcctViewerWidget": {
  "Could not attach OpenCASCADE to the OpenGL context (try NIBFORM_GL_CORE=1).":
      "Nie udało się podpiąć OpenCASCADE do kontekstu OpenGL (spróbuj NIBFORM_GL_CORE=1).",
  "Could not wrap the Qt framebuffer.": "Nie udało się opakować framebuffera Qt.",
 },
 "ProjectFile": {
  "Unknown curve kind: %1": "Nieznany rodzaj krzywej: %1", "Unknown constraint kind: %1": "Nieznany rodzaj więzu: %1",
  "Unknown feature kind: %1": "Nieznany rodzaj operacji: %1",
  "Not a Nibform project file": "To nie jest plik projektu Nibform",
  "The project was saved by a newer version of the app": "Projekt zapisany nowszą wersją programu",
  "Can't save %1: %2": "Nie można zapisać %1: %2", "Can't open %1: %2": "Nie można otworzyć %1: %2",
 },
 "SketchExchange": {
  "Not a DXF file (or a binary one: save it as ASCII DXF)": "To nie jest plik DXF (albo jest binarny: zapisz go jako DXF ASCII)",
  "The drawing has no curves this app reads (lines, circles, arcs, polylines, splines, ellipses)":
      "Rysunek nie zawiera krzywych, które aplikacja czyta (linie, okręgi, łuki, polilinie, splajny, elipsy)",
  "Not a readable SVG file: %1": "Nie da się odczytać pliku SVG: %1",
  "The drawing has no shapes this app reads (paths, lines, rectangles, circles, ellipses, polylines)":
      "Rysunek nie zawiera kształtów, które aplikacja czyta (ścieżki, linie, prostokąty, okręgi, elipsy, polilinie)",
  "Could not write %1": "Nie udało się zapisać %1", "Could not open %1": "Nie udało się otworzyć %1",
 },
 "DrawingSheet": {
  "Scale": "Skala", "Units": "Jednostki", "Sheet": "Arkusz", "Date": "Data", "Untitled": "Bez tytułu",
 },
 "DrawingWidget": {
  "Part": "Część", "Front": "Przód", "Top": "Góra", "Left": "Lewo", "Right": "Prawo", "Section View": "Przekrój",
  "Cut and seen like the view:": "Cięty i widziany jak rzut:", "Drag the new view to its place": "Przeciągnij nowy rzut na miejsce",
  "Drawing Title": "Tytuł rysunku", "Title:": "Tytuł:",
  "Drawing: drag views, dimensions and notes  ·  double-tap one for its options  ·  right drag pans, the wheel zooms":
      "Rysunek: przeciągaj rzuty, wymiary i notatki  ·  dwuklik otwiera ich opcje  ·  prawy przycisk przesuwa, kółko przybliża",
  "Dimension: tap the second corner or center in the same view": "Wymiar: dotknij drugiego narożnika albo środka w tym samym rzucie",
  "Dimension: tap two corners or centers of a view, or the rim of a hole for its size":
      "Wymiar: dotknij dwóch narożników albo środków w rzucie, albo krawędzi otworu, by podać jego średnicę",
  "Note: tap where the text goes": "Notatka: dotknij miejsca, gdzie ma być tekst",
  "Scale": "Skala", "Hidden Edges": "Krawędzie niewidoczne", "Cut Position…": "Położenie cięcia…", "Section": "Przekrój",
  "Cut, from the middle of the part [%1]:": "Cięcie, od środka części [%1]:", "Delete View": "Usuń rzut",
  "Radius": "Promień", "Diameter": "Średnica", "Aligned": "Równoległy", "Horizontal": "Poziomy", "Vertical": "Pionowy",
  "Delete Dimension": "Usuń wymiar", "Edit Text…": "Edytuj tekst…", "Note": "Notatka", "Text:": "Tekst:",
  "Text Size": "Wielkość tekstu", "Delete Note": "Usuń notatkę", "Title…": "Tytuł…", "Fit the Sheet": "Dopasuj arkusz",
  "Could not write %1": "Nie udało się zapisać %1",
 },
 "SketchEditor": {
  "Reference": "Odniesienie", "Import": "Import", "Imported %1 as a reference": "Zaimportowano %1 jako odniesienie",
  "Imported %1": "Zaimportowano %1", "Open a sketch to import a drawing into": "Otwórz szkic, do którego zaimportować rysunek",
  "Imported from %1: %2 curves": "Zaimportowano z %1 krzywe: %2", "Open a sketch to export": "Otwórz szkic do wyeksportowania",
  "Copied curves: %1": "Skopiowano krzywe: %1", "Copied bodies: %1": "Skopiowano bryły: %1",
  "Open a sketch to paste curves into": "Otwórz szkic, do którego wkleić krzywe", "pasted": "wklejone",
  "Nothing to paste: copy curves or bodies first": "Nie ma czego wkleić: najpierw skopiuj krzywe albo bryły",
  "Move: type the offset or the turn  ·  Enter goes on": "Przesuń: wpisz przesunięcie albo obrót  ·  Enter przechodzi dalej",
  "Move: tap the bodies to move or copy": "Przesuń: dotknij brył do przesunięcia albo skopiowania",
  "Move: tap where it goes: a corner, round edge or face of another body":
      "Przesuń: dotknij miejsca docelowego: narożnika, okrągłej krawędzi albo ściany innej bryły",
  "Move: drag an arrow or ring, or tap a corner, round edge or face of it and then where it goes  ·  Enter types values and applies":
      "Przesuń: przeciągnij strzałkę albo pierścień, albo dotknij narożnika, okrągłej krawędzi lub ściany, a potem "
      "miejsca docelowego  ·  Enter: wpisz wartości i zatwierdź",
  "Measure: tap a face, edge or corner, and a second one to measure between them":
      "Zmierz: dotknij ściany, krawędzi albo narożnika, a potem drugiego, by zmierzyć między nimi",
  "Section: type how far to shift the cut  ·  Enter accepts": "Przekrój: wpisz, o ile przesunąć cięcie  ·  Enter zatwierdza",
  "Section: tap another face or plane to cut along it  ·  View → Section View turns it off":
      "Przekrój: dotknij innej ściany albo płaszczyzny, by ciąć wzdłuż niej  ·  Widok → Przekrój wyłącza",
  "Section: tap a flat face or a plane to cut the model open along it":
      "Przekrój: dotknij płaskiej ściany albo płaszczyzny, by przeciąć model wzdłuż niej",
  "Copy": "Kopia", "Move": "Przesunięcie", "Body %1": "Bryła %1", "Point: %1": "Punkt: %1",
  "Length: %1": "Długość: %1", "Area: %1": "Pole: %1", "Radius: %1  ·  diameter %2": "Promień: %1  ·  średnica %2",
  "Distance: %1": "Odległość: %1", "Angle: %1°": "Kąt: %1°", "Between centers: %1": "Między środkami: %1",
  "Volume: %1  ·  surface %2": "Objętość: %1  ·  powierzchnia %2", "Center of mass: %1": "Środek masy: %1",
  "Size: %1 × %2 × %3": "Wymiary: %1 × %2 × %3", "Mass at 1.24 g/cm³ (PLA): %1 g": "Masa przy 1,24 g/cm³ (PLA): %1 g",
  "Section: tap a flat face or a plane": "Przekrój: dotknij płaskiej ściany albo płaszczyzny",
  "Section offset [mm]": "Przesunięcie przekroju [mm]", "Keep this side": "Zostaw tę stronę",
  "Keep the other side": "Zostaw drugą stronę",
  "Move: these don't line up: pick corners, round edges or flat faces":
      "Przesuń: tego nie da się dopasować: wybierz narożniki, okrągłe krawędzie albo płaskie ściany",
  "Move along X [mm]": "Przesuń wzdłuż X [mm]", "Move along Y [mm]": "Przesuń wzdłuż Y [mm]",
  "Move along Z [mm]": "Przesuń wzdłuż Z [mm]", "Turn about Z [°]": "Obróć wokół Z [°]",
  "Revolve": "Obrót", "Sweep": "Przeciągnięcie", "Loft": "Wyciągnięcie przez profile", "Mirror": "Lustro",
  "Circular Pattern": "Szyk kołowy", "Linear Pattern": "Szyk liniowy", "Hole": "Otwór", "Axis": "Oś",
  "Plane": "Płaszczyzna", "Hole: pick a flat face": "Otwór: wybierz płaską ścianę",
  "Count (with the original)": "Liczba (z oryginałem)", "Over [°] (360: a full turn)": "Na kącie [°] (360: pełny obrót)",
  "Spacing [mm]": "Odstęp [mm]", "Hole diameter [mm]": "Średnica otworu [mm]", "Through": "Przelotowy",
  "Blind": "Nieprzelotowy", "Counterbore": "Pogłębienie walcowe", "Countersink": "Pogłębienie stożkowe",
  "Depth [mm]": "Głębokość [mm]", "Head diameter [mm]": "Średnica pogłębienia [mm]",
  "Counterbore depth [mm]": "Głębokość pogłębienia [mm]", "Offset [mm]": "Odsunięcie [mm]",
  "type the angle  ·  Enter accepts": "wpisz kąt  ·  Enter zatwierdza",
  "Revolve: tap the sketch regions to turn": "Obrót: dotknij obszarów szkicu do obrócenia",
  "Revolve: tap the axis: a sketch line, a straight or round edge, or an axis":
      "Obrót: dotknij osi: linii szkicu, prostej albo okrągłej krawędzi albo osi",
  "Sweep: tap the profile (a sketch region)": "Przeciągnięcie: dotknij profilu (obszaru szkicu)",
  "Sweep: tap the path: a curve of another sketch (its connected curves follow)":
      "Przeciągnięcie: dotknij ścieżki: krzywej innego szkicu (połączone z nią krzywe też należą do ścieżki)",
  "Loft: tap the profiles in order (regions of sketches on different planes)":
      "Wyciągnięcie przez profile: dotknij profili po kolei (obszarów szkiców na różnych płaszczyznach)",
  "Loft: %1 profiles  ·  tap more, or Enter to loft": "Wyciągnięcie przez profile: %1 profili  ·  dotknij kolejnych albo Enter",
  "type the size, pick the kind  ·  Enter accepts  ·  tap the face for more holes":
      "wpisz rozmiar, wybierz rodzaj  ·  Enter zatwierdza  ·  dotknij ściany, by dodać otwory",
  "Hole: tap a flat face where it goes (snaps to sketch points and round edges' centers)":
      "Otwór: dotknij płaskiej ściany tam, gdzie ma być (przyciąga do punktów szkicu i środków okrągłych krawędzi)",
  "Mirror: tap the bodies to mirror, then Enter (or tap a plane)": "Lustro: dotknij brył do odbicia, potem Enter (albo dotknij płaszczyzny)",
  "Mirror: %1 bodies  ·  Enter, then tap the mirror plane": "Lustro: %1 brył  ·  Enter, potem dotknij płaszczyzny odbicia",
  "Mirror: tap the mirror plane: a flat face or a plane": "Lustro: dotknij płaszczyzny odbicia: płaskiej ściany albo płaszczyzny",
  "Circular pattern": "Szyk kołowy", "Linear pattern": "Szyk liniowy",
  "type the count, then the %1  ·  Enter accepts": "wpisz liczbę, potem %1  ·  Enter zatwierdza",
  "angle": "kąt", "spacing": "odstęp",
  "%1: tap the bodies to repeat (or pick a feature in the history: Pattern…)":
      "%1: dotknij brył do powtórzenia (albo wybierz operację w historii: Szyk…)",
  "%1: tap the axis: a round or straight edge, an axis or a sketch line":
      "%1: dotknij osi: okrągłej albo prostej krawędzi, osi albo linii szkicu",
  "%1: tap the direction: a straight edge, an axis or a sketch line":
      "%1: dotknij kierunku: prostej krawędzi, osi albo linii szkicu",
  "Offset plane": "Płaszczyzna odsunięta", "type the distance  ·  Enter accepts": "wpisz odległość  ·  Enter zatwierdza",
  "Offset plane: tap a flat face or a plane": "Płaszczyzna odsunięta: dotknij płaskiej ściany albo płaszczyzny",
  "Plane at angle": "Płaszczyzna pod kątem",
  "Plane at angle: tap the plane (a flat face or a plane) and the axis to turn it about":
      "Płaszczyzna pod kątem: dotknij płaszczyzny (płaskiej ściany albo płaszczyzny) i osi obrotu",
  "Plane at angle: tap the axis to turn it about (an edge, an axis or a sketch line)":
      "Płaszczyzna pod kątem: dotknij osi obrotu (krawędzi, osi albo linii szkicu)",
  "Plane through 3 points: tap corners of a body (%1 of 3)": "Płaszczyzna przez 3 punkty: dotknij narożników bryły (%1 z 3)",
  "Axis: tap a straight or round edge, or two corners": "Oś: dotknij prostej albo okrągłej krawędzi albo dwóch narożników",
  "Axis: tap the second corner": "Oś: dotknij drugiego narożnika",
  "Dimension: type the value, or tap another point or line to measure to it  ·  Enter accepts, Esc cancels":
      "Wymiar: wpisz wartość albo dotknij innego punktu lub linii, by mierzyć do niego  ·  Enter zatwierdza, Esc anuluje",
  "Dimension: tap a second point, or a line": "Wymiar: dotknij drugiego punktu albo linii",
  "Dimension: tap a line, circle or arc, or two points, a point and a line, two lines  ·  Esc goes back to Select":
      "Wymiar: dotknij linii, okręgu lub łuku albo dwóch punktów, punktu i linii, dwóch linii  ·  Esc wraca do zaznaczania",
  "Automatic": "Automatycznie", "Automatic: cut": "Automatycznie: odejmij", "Automatic: join": "Automatycznie: dodaj",
  "New Body": "Nowa bryła", "Join": "Dodaj", "Cut": "Odejmij",
  "Select geometry first, then pick a constraint": "Najpierw zaznacz geometrię, potem wybierz więz",
  "This constraint doesn't fit the selection": "Ten więz nie pasuje do zaznaczenia",
  "This constraint already exists": "Ten więz już istnieje",
  "The constraints conflict; change rejected": "Więzy są sprzeczne – zmiana odrzucona",
  "Redundant constraint: this geometry is already defined": "Więz nadmiarowy – ta geometria jest już określona",
  "Length [mm]": "Długość [mm]", "Distance [mm]": "Odległość [mm]", "Radius [mm]": "Promień [mm]",
  "Diameter [mm]": "Średnica [mm]", "Angle [°]": "Kąt [°]",
  "Extrude: drag the arrow or type the distance  ·  Enter accepts, Esc cancels":
      "Wyciągnij: przeciągnij strzałkę albo wpisz odległość  ·  Enter zatwierdza, Esc anuluje",
  "Extrude: tap a sketch region or a flat face of a body": "Wyciągnij: dotknij obszaru szkicu albo płaskiej ściany bryły",
  "Fillet: %1": "Zaokrąglij: %1",
  "Fillet: tap more edges, type the radius  ·  Enter accepts": "Zaokrąglij: dotykaj kolejnych krawędzi, wpisz promień  ·  Enter zatwierdza",
  "Fillet: tap edges of a body": "Zaokrąglij: dotknij krawędzi bryły",
  "Chamfer: %1": "Fazuj: %1",
  "Chamfer: tap more edges, type the size  ·  Enter accepts": "Fazuj: dotykaj kolejnych krawędzi, wpisz wielkość  ·  Enter zatwierdza",
  "Chamfer: tap edges of a body": "Fazuj: dotknij krawędzi bryły",
  "Shell: %1": "Skorupa: %1",
  "Shell: tap more faces to open, type the wall thickness  ·  Enter accepts":
      "Skorupa: dotykaj kolejnych ścian do otwarcia, wpisz grubość ścianek  ·  Enter zatwierdza",
  "Shell: tap the faces to open": "Skorupa: dotknij ścian, które mają być otwarte",
  "Drag the arrow or type the distance  ·  Enter extrudes, Esc cancels":
      "Przeciągnij strzałkę albo wpisz odległość  ·  Enter wyciąga, Esc anuluje",
  "Tap a sketch region to extrude it, or a face of a body  ·  New Sketch to draw":
      "Dotknij obszaru szkicu, by go wyciągnąć, albo ściany bryły  ·  Stwórz szkic, by rysować",
  "  ·  point": "  ·  punkt", "  ·  midpoint": "  ·  środek odcinka", "  ·  origin": "  ·  początek układu",
  "  ·  aligned": "  ·  wyrównanie",
  "Select: tap to add  ·  drag to move  ·  tap a selected dimension to change it  ·  scribble over something to delete it":
      "Zaznacz: dotknij, by dodać  ·  przeciągnij, by przesunąć  ·  zaznaczony wymiar: dotknij, by zmienić  ·  "
      "zamaż coś, by to usunąć",
  "Freehand: draw a line, corners, a rectangle, a circle or an arc  ·  scribble over something to delete it  ·  tap to select":
      "Odręcznie: narysuj linię, łamaną, prostokąt, okrąg albo łuk  ·  zamaż coś, by to usunąć  ·  dotknij, by zaznaczyć",
  "Freehand: not recognized  ·  draw a line, corners, a rectangle, a circle or an arc":
      "Odręcznie: nie rozpoznano  ·  narysuj linię, łamaną, prostokąt, okrąg albo łuk",
  "Nothing under the scribble to delete": "Pod zamazaniem nie ma nic do usunięcia",
  "Scribbled out": "Zamazane – usunięto",
  "Bodies: %1": "Bryły: %1", "Sketch: empty": "Szkic pusty", "Sketch: conflicting constraints": "Szkic: sprzeczne więzy",
  "Sketch: fully defined": "Szkic w pełni określony", "Sketch: %1 DOF": "Szkic: %1 st. swobody",
  "Wall thickness [mm]": "Grubość [mm]", "Chamfer [mm]": "Faza [mm]",
  "Couldn't record the picks": "Nie udało się zapamiętać zaznaczenia",
  "Sketch": "Szkic", "Extrude": "Wyciągnięcie", "Fillet": "Zaokrąglenie", "Chamfer": "Faza", "Union": "Połączenie",
  "Subtract": "Odjęcie", "Intersect": "Część wspólna", "Delete": "Usunięcie", "Shell": "Skorupa",
  "Horizontal distance [mm]": "Odległość pozioma [mm]", "Vertical distance [mm]": "Odległość pionowa [mm]",
  "Project: there are no bodies to project": "Rzutuj: nie ma brył do rzutowania",
  "Project: tap to copy this edge into the sketch": "Rzutuj: dotknij, by skopiować tę krawędź do szkicu",
  "Project: tap to copy this face's outline into the sketch": "Rzutuj: dotknij, by skopiować obrys tej ściany do szkicu",
  "Project: tap a body edge or face": "Rzutuj: dotknij krawędzi albo ściany bryły",
  "Tap an edge or a face of a body": "Dotknij krawędzi albo ściany bryły",
  "Already in the sketch (or seen edge-on)": "Już jest w szkicu (albo widać ją dokładnie z boku)",
  "Can't read the picture %1": "Nie można odczytać obrazu %1",
 },
 "SketchTool": {
  "Line: pick the start": "Linia: wskaż początek", "Line: %1, angle %2°  ·  Esc finishes": "Linia: %1, kąt %2°  ·  Esc kończy",
  "Rectangle: pick the first corner": "Prostokąt: wskaż pierwszy narożnik", "Rectangle: %1 × %2": "Prostokąt: %1 × %2",
  "Circle: pick the center": "Okrąg: wskaż środek", "Circle: radius %1, diameter %2": "Okrąg: promień %1, średnica %2",
  "Arc: pick the start": "Łuk: wskaż początek", "Arc: pick the end": "Łuk: wskaż koniec", "Arc: radius %1": "Łuk: promień %1",
  "Arc: pick a point on the arc": "Łuk: wskaż punkt na łuku",
  "Trim: tap a piece to remove it  ·  drag across several at once":
      "Przytnij: dotknij fragmentu, by go usunąć  ·  przeciągnij przez kilka naraz",
  "Offset: tap a curve (takes its whole connected chain)": "Odsunięcie: dotknij krzywej (weźmie cały połączony łańcuch)",
  "Offset: too far, the curves would fold over  ·  Esc cancels": "Odsunięcie: za duże – krzywe by się wywinęły  ·  Esc anuluje",
  "Offset: type the distance  ·  Enter accepts, Esc cancels": "Odsunięcie: wpisz odległość  ·  Enter zatwierdza, Esc anuluje",
  "Offset: %1  ·  tap to type an exact value  ·  Esc cancels": "Odsunięcie: %1  ·  dotknij, by wpisać dokładną wartość  ·  Esc anuluje",
  "Offset [mm]": "Odsunięcie [mm]",
  "Polygon: type the number of sides  ·  Enter accepts, Esc cancels": "Wielokąt: wpisz liczbę boków  ·  Enter zatwierdza, Esc anuluje",
  "Polygon: pick the center": "Wielokąt: wskaż środek",
  "Polygon: %1 sides, radius %2  ·  pick a corner": "Wielokąt: boki: %1, promień %2  ·  wskaż wierzchołek",
  "Sides": "Boki",
  "Slot: pick the center of one end": "Rowek: wskaż środek jednego końca",
  "Slot: pick the center of the other end  ·  %1": "Rowek: wskaż środek drugiego końca  ·  %1",
  "Slot: pick the width  ·  %1": "Rowek: wskaż szerokość  ·  %1",
  "Ellipse: pick the center": "Elipsa: wskaż środek",
  "Ellipse: pick the end of an axis  ·  %1": "Elipsa: wskaż koniec osi  ·  %1",
  "Ellipse: pick the other axis  ·  %1 × %2": "Elipsa: wskaż drugą oś  ·  %1 × %2",
  "Spline: tap the points it passes through": "Splajn: dotykaj punktów, przez które ma przejść",
  "Spline: tap to close it": "Splajn: dotknij, by go zamknąć",
  "Spline: %1 points  ·  tap the first one to close it  ·  Enter finishes, Esc cancels":
      "Splajn: punkty: %1  ·  dotknij pierwszego, by zamknąć  ·  Enter kończy, Esc anuluje",
  "Chamfer: too big for these lines": "Fazuj: za duża faza dla tych linii",
  "Fillet: too big for these lines": "Zaokrąglij: za duży promień dla tych linii",
  "Chamfer: type the distance  ·  Enter accepts, Esc cancels": "Fazuj: wpisz odległość  ·  Enter zatwierdza, Esc anuluje",
  "Fillet: type the radius  ·  Enter accepts, Esc cancels": "Zaokrąglij: wpisz promień  ·  Enter zatwierdza, Esc anuluje",
  "Chamfer: tap a corner where two lines meet": "Fazuj: dotknij narożnika, w którym spotykają się dwie linie",
  "Fillet: tap a corner where two lines meet": "Zaokrąglij: dotknij narożnika, w którym spotykają się dwie linie",
  "Chamfer: tap the corner of two lines": "Fazuj: dotknij narożnika dwóch linii",
  "Fillet: tap the corner of two lines": "Zaokrąglij: dotknij narożnika dwóch linii",
  "Chamfer [mm]": "Faza [mm]", "Radius [mm]": "Promień [mm]", "Distance [mm]": "Odległość [mm]",
  "Mirror": "Lustro", "Linear pattern": "Szyk liniowy", "Circular pattern": "Szyk kołowy",
  "%1: tap the curves to copy": "%1: dotknij krzywych do skopiowania",
  "%1: %2 curves  ·  tap more, or Enter to go on": "%1: krzywe: %2  ·  dotykaj kolejnych albo Enter, by przejść dalej",
  "%1: type how many in all (with the original)  ·  Enter accepts": "%1: wpisz, ile ma być razem (z oryginałem)  ·  Enter zatwierdza",
  "Mirror: tap the line to mirror about": "Lustro: dotknij linii, względem której odbić",
  "Linear pattern: pick where the first copy goes  ·  %1": "Szyk liniowy: wskaż, gdzie ma trafić pierwsza kopia  ·  %1",
  "Linear pattern: pick a point to measure the step from": "Szyk liniowy: wskaż punkt, od którego mierzyć odstęp",
  "Circular pattern: pick the center": "Szyk kołowy: wskaż środek",
  "Count": "Liczba",
  "Image: insert a picture first (Image → Insert Image…)": "Obraz: najpierw wstaw obraz (Obraz → Wstaw obraz…)",
  "Image: type the real distance between the two points  ·  Enter accepts":
      "Obraz: wpisz rzeczywistą odległość między punktami  ·  Enter zatwierdza",
  "Image: tap the second point  ·  %1 now": "Obraz: dotknij drugiego punktu  ·  teraz %1",
  "Image: drag it to move it  ·  tap two points a known distance apart to scale it":
      "Obraz: przeciągnij, by przesunąć  ·  dotknij dwóch punktów o znanej odległości, by przeskalować",
 },
 "ValueEditor": {"Accept (Enter)": "Zatwierdź (Enter)", "Cancel (Esc)": "Anuluj (Esc)"},
}

path = sys.argv[1] if len(sys.argv) > 1 else "translations/nibform_pl.ts"
tree = ET.parse(path)
missing = []
used = set()
for ctx in tree.getroot().findall("context"):
    name = ctx.find("name").text
    for msg in ctx.findall("message"):
        source = msg.find("source").text
        used.add((name, source))
        tr = msg.find("translation")
        text = PL.get(name, {}).get(source)
        if text is None:
            missing.append(f"{name}: {source!r}")
            continue
        tr.text = text
        tr.attrib.pop("type", None)  # finished
tree.write(path, encoding="utf-8", xml_declaration=True)
with open(path, encoding="utf-8") as f:
    text = f.read()
if "<!DOCTYPE TS>" not in text:
    head, rest = text.split("\n", 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(head + "\n<!DOCTYPE TS>\n" + rest)
for m in missing:
    print("missing:", m)
# Entries for texts the sources no longer have: delete them from PL above.
for name, texts in PL.items():
    for source in texts:
        if (name, source) not in used:
            print(f"unused: {name}: {source!r}")
sys.exit(1 if missing else 0)
