#pragma once

#include <QString>
#include <TopoDS_Shape.hxx>

#include <optional>

// What another project file gives an assembly as a component: its shown bodies, rebuilt
// from its history, as one shape (a compound when there are several). Empty, with `error`
// set, when the file can't be read or shows no body.
std::optional<TopoDS_Shape> projectShape(const QString& path, QString* error = nullptr);
