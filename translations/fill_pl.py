#!/usr/bin/env python3
"""Fills translations/nibform_pl.ts with the Polish texts below (run after update_translations
when sources change; new texts without an entry here are reported and stay unfinished)."""
import sys
import xml.etree.ElementTree as ET

PL = {
 "MainWindow": {
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
  "Open…": "Otwórz…", "Save": "Zapisz", "Save As…": "Zapisz jako…", "Export STEP…": "Eksportuj STEP…",
  "Export STL…": "Eksportuj STL…", "Quit": "Zakończ", "&Edit": "&Edycja", "Undo": "Cofnij", "Redo": "Ponów",
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
  "Saved %1": "Zapisano %1", "Save Project": "Zapisz projekt", "Export STEP": "Eksportuj STEP",
  "Export STL": "Eksportuj STL", "Export Failed": "Eksport nieudany", "Exported %1": "Wyeksportowano %1",
  "Sketch on Face": "Szkic na ścianie", "Pen%1: pressure %2%3": "Rysik%1: nacisk %2%3", " [eraser]": " [gumka]",
  " [button]": " [przycisk]",
 },
 "Model": {
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
 "SketchEditor": {
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
  "Select: tap to add  ·  drag to move  ·  tap a selected dimension to change it":
      "Zaznacz: dotknij, by dodać  ·  przeciągnij, by przesunąć  ·  zaznaczony wymiar: dotknij, by zmienić",
  "Bodies: %1": "Bryły: %1", "Sketch: empty": "Szkic pusty", "Sketch: conflicting constraints": "Szkic: sprzeczne więzy",
  "Sketch: fully defined": "Szkic w pełni określony", "Sketch: %1 DOF": "Szkic: %1 st. swobody",
  "Wall thickness [mm]": "Grubość [mm]", "Chamfer [mm]": "Faza [mm]",
  "Couldn't record the picks": "Nie udało się zapamiętać zaznaczenia",
  "Sketch": "Szkic", "Extrude": "Wyciągnięcie", "Fillet": "Zaokrąglenie", "Chamfer": "Faza", "Union": "Połączenie",
  "Subtract": "Odjęcie", "Intersect": "Część wspólna", "Delete": "Usunięcie", "Shell": "Skorupa",
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
