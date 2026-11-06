#include "model/Export.h"
#include "model/Tr.h"

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>

namespace {

TopoDS_Compound compoundOf(const std::vector<TopoDS_Shape>& bodies)
{
    TopoDS_Compound c;
    BRep_Builder builder;
    builder.MakeCompound(c);
    for (const TopoDS_Shape& b : bodies)
        builder.Add(c, b);
    return c;
}

} // namespace

std::string exportStep(const std::vector<TopoDS_Shape>& bodies, const std::string& path)
{
    if (bodies.empty())
        return QT_TRANSLATE_NOOP("Model", "No bodies to export");
    STEPControl_Writer writer;
    Interface_Static::SetCVal("write.step.unit", "MM");
    for (const TopoDS_Shape& b : bodies) {
        if (writer.Transfer(b, STEPControl_AsIs) != IFSelect_RetDone)
            return QT_TRANSLATE_NOOP("Model", "Could not prepare a body for STEP");
    }
    if (writer.Write(path.c_str()) != IFSelect_RetDone)
        return QT_TRANSLATE_NOOP("Model", "Could not write the STEP file");
    return {};
}

std::string exportStl(const std::vector<TopoDS_Shape>& bodies, const std::string& path)
{
    if (bodies.empty())
        return QT_TRANSLATE_NOOP("Model", "No bodies to export");
    // Mesh a copy, so the model's own (coarser, display) triangulation is left alone.
    const TopoDS_Shape shape = BRepBuilderAPI_Copy(compoundOf(bodies)).Shape();
    BRepMesh_IncrementalMesh mesh(shape, 0.05, false, 0.5 * M_PI / 180.0, true);
    if (!mesh.IsDone())
        return QT_TRANSLATE_NOOP("Model", "Could not mesh the bodies for STL");
    StlAPI_Writer writer;
    writer.ASCIIMode() = false;
    if (!writer.Write(shape, path.c_str()))
        return QT_TRANSLATE_NOOP("Model", "Could not write the STL file");
    return {};
}

std::vector<TopoDS_Shape> importStep(const std::string& path, std::string* error)
{
    std::vector<TopoDS_Shape> solids;
    STEPControl_Reader reader;
    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
        if (error)
            *error = QT_TRANSLATE_NOOP("Model", "Could not read the STEP file");
        return solids;
    }
    reader.TransferRoots();
    for (TopExp_Explorer it(reader.OneShape(), TopAbs_SOLID); it.More(); it.Next())
        solids.push_back(it.Current());
    return solids;
}
