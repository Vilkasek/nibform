cmake_minimum_required(VERSION 3.24)
project(Nibform VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(NIBFORM_BUILD_TESTS "Build the tests" ON)
include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Svg PrintSupport LinguistTools)
if(NIBFORM_BUILD_TESTS)
  find_package(Qt6 REQUIRED COMPONENTS Test)
endif()
find_package(OpenCASCADE REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Threads REQUIRED)

set(NIBFORM_WARNINGS -Wall -Wextra -Wno-unused-parameter)

# Geometry/model code with no Qt or GUI dependency, so it can be unit tested.
add_library(nibform_core STATIC
  src/input/PalmRejection.cpp
  src/input/PalmRejection.h
  src/input/TapGesture.cpp
  src/input/TapGesture.h
  src/model/Assembly.cpp
  src/model/Assembly.h
  src/model/Details.cpp
  src/model/Details.h
  src/model/Drawing.cpp
  src/model/Drawing.h
  src/model/Export.cpp
  src/model/Export.h
  src/model/Expression.cpp
  src/model/Expression.h
  src/model/Features.cpp
  src/model/Features.h
  src/model/Measure.cpp
  src/model/Measure.h
  src/model/Parameters.cpp
  src/model/Parameters.h
  src/model/Profiles.cpp
  src/model/Profiles.h
  src/model/Rebuild.cpp
  src/model/Rebuild.h
  src/model/Solids.cpp
  src/model/Solids.h
  src/model/TextShapes.cpp
  src/model/TextShapes.h
  src/model/Threads.cpp
  src/model/Threads.h
  src/model/Units.cpp
  src/model/Units.h
  src/sketch/Constraint.h
  src/sketch/ConstraintCommands.cpp
  src/sketch/ConstraintCommands.h
  src/sketch/DrawList.cpp
  src/sketch/DrawList.h
  src/sketch/Freehand.cpp
  src/sketch/Freehand.h
  src/sketch/Geometry.cpp
  src/sketch/Geometry.h
  src/sketch/Offset.cpp
  src/sketch/Offset.h
  src/sketch/Sketch.cpp
  src/sketch/Sketch.h
  src/sketch/SketchOps.cpp
  src/sketch/SketchOps.h
  src/sketch/Snapper.cpp
  src/sketch/Snapper.h
  src/sketch/Solver.cpp
  src/sketch/Solver.h
  src/sketch/Trim.cpp
  src/sketch/Trim.h
)
target_include_directories(nibform_core PUBLIC src ${OpenCASCADE_INCLUDE_DIR})
target_compile_options(nibform_core PRIVATE ${NIBFORM_WARNINGS})
target_link_libraries(nibform_core PUBLIC
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool TKShHealing TKFillet TKOffset
  TKMesh TKXSBase TKDESTEP TKDESTL TKDEIGES TKHLR
  TKXCAF TKLCAF TKCAF TKCDF # assemblies to STEP with their structure (XDE)
  TKService TKV3d # fonts: sketch text outlines
  Eigen3::Eigen Threads::Threads)

# Project files (Qt Core for JSON), shared by the app and its tests.
add_library(nibform_io STATIC src/io/ProjectFile.cpp src/io/ProjectFile.h src/io/Recovery.cpp src/io/Recovery.h
  src/io/SketchExchange.cpp src/io/SketchExchange.h src/io/ComponentFiles.cpp src/io/ComponentFiles.h)
target_compile_options(nibform_io PRIVATE ${NIBFORM_WARNINGS})
target_link_libraries(nibform_io PUBLIC nibform_core Qt6::Core)

add_executable(nibform
  src/main.cpp
  src/app/MainWindow.cpp
  src/app/Icons.cpp
  src/app/Icons.h
  src/app/Language.cpp
  src/app/Language.h
  src/app/MainWindow.h
  src/app/DrawingSheet.cpp
  src/app/DrawingSheet.h
  src/app/DrawingWidget.cpp
  src/app/DrawingWidget.h
  src/app/AssemblyPanel.cpp
  src/app/AssemblyPanel.h
  src/app/ParametersPanel.cpp
  src/app/ParametersPanel.h
  src/app/PatternPanel.cpp
  src/app/PatternPanel.h
  src/app/RadialMenu.cpp
  src/app/ShortcutsDialog.cpp
  src/app/ShortcutsDialog.h
  src/app/RadialMenu.h
  src/app/Theme.cpp
  src/app/Theme.h
  src/sketch/SketchEditor.cpp
  src/sketch/SketchEditorBodies.cpp
  src/sketch/SketchEditorSolids.cpp
  src/sketch/SketchEditorInspect.cpp
  src/sketch/SketchEditorExchange.cpp
  src/sketch/SketchEditorParameters.cpp
  src/sketch/SketchEditorAssembly.cpp
  src/sketch/SketchEditorDetails.cpp
  src/viewer/UnitText.h
  src/sketch/SketchEditor.h
  src/sketch/SketchTools.cpp
  src/sketch/SketchTools.h
  src/viewer/Glyphs.cpp
  src/viewer/Glyphs.h
  src/viewer/OcctViewerWidget.cpp
  src/viewer/OcctViewerWidget.h
  src/viewer/PenSample.h
  src/viewer/PlanarPrs.cpp
  src/viewer/PlanarPrs.h
  src/viewer/ValueEditor.cpp
  src/viewer/ValueEditor.h
  src/viewer/ViewerInteraction.h
)
target_compile_definitions(nibform PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_compile_options(nibform PRIVATE ${NIBFORM_WARNINGS})
target_link_libraries(nibform PRIVATE
  nibform_core
  nibform_io
  Qt6::Widgets
  Qt6::OpenGLWidgets
  Qt6::Svg
  Qt6::PrintSupport
  TKG3d TKBRep TKGeomBase TKTopAlgo TKPrim TKBO TKFillet
  TKService TKV3d TKOpenGl
)

# Translations: English in the sources, the other languages in translations/*.ts
# (Qt Linguist). `cmake --build build --target update_translations` refreshes the .ts
# files from the sources; the build compiles them into the app (:/i18n/).
# The window icon (drawn from docs/assets/logo.svg: rsvg-convert -w 256 -h 256).
qt_add_resources(nibform "app_icon" PREFIX "/" BASE src/app FILES src/app/nibform.png)

qt_add_translations(TARGETS nibform
  TS_FILES translations/nibform_pl.ts
  SOURCE_TARGETS nibform nibform_core nibform_io
  RESOURCE_PREFIX /i18n
  LUPDATE_OPTIONS -no-obsolete -locations none)

# Installed: the app, its desktop entry and icon, the .nibform file type and the
# AppStream metadata (all named by the app id, as Flatpak and software centers expect).
set(NIBFORM_APP_ID io.github.vilkasek.Nibform)
install(TARGETS nibform RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/${NIBFORM_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/${NIBFORM_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES packaging/${NIBFORM_APP_ID}.mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
        RENAME ${NIBFORM_APP_ID}.xml)
install(FILES docs/assets/logo.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME ${NIBFORM_APP_ID}.svg)
install(FILES src/app/nibform.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME ${NIBFORM_APP_ID}.png)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/nibform)

if(NOT NIBFORM_BUILD_TESTS)
  return()
endif()

enable_testing()
foreach(name sketch solver trim offset model features input toolkit freehand solid_features inspect drawing rebuild parameters)
  add_executable(test_${name} tests/test_${name}.cpp tests/Check.h)
  target_compile_options(test_${name} PRIVATE ${NIBFORM_WARNINGS})
  target_link_libraries(test_${name} PRIVATE nibform_core)
  add_test(NAME ${name} COMMAND test_${name})
endforeach()

# Rebuild timings on long histories (not run by ctest).
add_executable(bench_rebuild tests/bench_rebuild.cpp tests/LongModel.h)
target_compile_options(bench_rebuild PRIVATE ${NIBFORM_WARNINGS})
target_link_libraries(bench_rebuild PRIVATE nibform_core)

# The finishing features (M16); its enclosure can be saved as a project to look at
# (NIBFORM_SAVE_ENCLOSURE=path).
add_executable(test_details tests/test_details.cpp tests/Check.h)
target_compile_options(test_details PRIVATE ${NIBFORM_WARNINGS})
target_link_libraries(test_details PRIVATE nibform_io)
add_test(NAME details COMMAND test_details)

# Assemblies (M17): components, joints, interference, the hinge (saved as a project to
# look at with NIBFORM_SAVE_HINGE=path).
add_executable(test_assembly tests/test_assembly.cpp tests/Check.h)
target_compile_options(test_assembly PRIVATE ${NIBFORM_WARNINGS})
target_link_libraries(test_assembly PRIVATE nibform_io)
add_test(NAME assembly COMMAND test_assembly)

add_executable(test_project tests/test_project.cpp tests/Check.h)
target_compile_options(test_project PRIVATE ${NIBFORM_WARNINGS})
target_link_libraries(test_project PRIVATE nibform_io)
add_test(NAME project COMMAND test_project)

add_executable(test_project_fuzz tests/test_project_fuzz.cpp tests/Check.h)
target_compile_options(test_project_fuzz PRIVATE ${NIBFORM_WARNINGS})
target_link_libraries(test_project_fuzz PRIVATE nibform_io)
add_test(NAME project_fuzz COMMAND test_project_fuzz)
# A file that makes the loader hang fails the test instead of stalling the run.
set_tests_properties(project_fuzz PROPERTIES TIMEOUT 120)

add_executable(test_interop tests/test_interop.cpp tests/Check.h)
target_compile_options(test_interop PRIVATE ${NIBFORM_WARNINGS})
target_link_libraries(test_interop PRIVATE nibform_io)
add_test(NAME interop COMMAND test_interop)

# The floating value field, on Qt's offscreen platform (no display needed).
add_executable(test_value_editor tests/test_value_editor.cpp src/viewer/ValueEditor.cpp src/viewer/ValueEditor.h
  src/model/Units.cpp src/model/Units.h src/model/Expression.cpp src/model/Expression.h)
target_include_directories(test_value_editor PRIVATE src)
target_compile_options(test_value_editor PRIVATE ${NIBFORM_WARNINGS})
target_link_libraries(test_value_editor PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME value_editor COMMAND test_value_editor)
set_tests_properties(value_editor PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
