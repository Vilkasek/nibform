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
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <Poly_Triangulation.hxx>
#include <ShapeFix_Solid.hxx>
#include <StlAPI_Reader.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>
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

std::string exportIges(const std::vector<TopoDS_Shape>& bodies, const std::string& path)
{
    if (bodies.empty())
        return QT_TRANSLATE_NOOP("Model", "No bodies to export");
    IGESControl_Controller::Init();
    IGESControl_Writer writer("MM", 1); // BRep entities (faces), not just surfaces
    for (const TopoDS_Shape& b : bodies) {
        if (!writer.AddShape(b))
            return QT_TRANSLATE_NOOP("Model", "Could not prepare a body for IGES");
    }
    writer.ComputeModel();
    if (!writer.Write(path.c_str()))
        return QT_TRANSLATE_NOOP("Model", "Could not write the IGES file");
    return {};
}

// --- 3MF ----------------------------------------------------------------------------

namespace {

std::uint32_t crc32(const std::string& data)
{
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char byte : data)
        c = table[(c ^ byte) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// A zip archive of stored (uncompressed) files, as 3MF packages are.
std::string zipStored(const std::vector<std::pair<std::string, std::string>>& files)
{
    std::string out, directory;
    auto u16 = [](std::string& s, std::uint16_t v) {
        s.push_back(static_cast<char>(v & 0xFF));
        s.push_back(static_cast<char>(v >> 8));
    };
    auto u32 = [&](std::string& s, std::uint32_t v) {
        u16(s, static_cast<std::uint16_t>(v & 0xFFFF));
        u16(s, static_cast<std::uint16_t>(v >> 16));
    };
    for (const auto& [name, data] : files) {
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t crc = crc32(data);
        const auto size = static_cast<std::uint32_t>(data.size());
        u32(out, 0x04034b50);
        u16(out, 20);
        u16(out, 0); // flags
        u16(out, 0); // stored
        u16(out, 0); // time
        u16(out, 0x21); // date: 1980-01-01
        u32(out, crc);
        u32(out, size);
        u32(out, size);
        u16(out, static_cast<std::uint16_t>(name.size()));
        u16(out, 0);
        out += name;
        out += data;

        u32(directory, 0x02014b50);
        u16(directory, 20);
        u16(directory, 20);
        u16(directory, 0);
        u16(directory, 0);
        u16(directory, 0);
        u16(directory, 0x21);
        u32(directory, crc);
        u32(directory, size);
        u32(directory, size);
        u16(directory, static_cast<std::uint16_t>(name.size()));
        u16(directory, 0);
        u16(directory, 0);
        u16(directory, 0);
        u16(directory, 0);
        u32(directory, 0);
        u32(directory, offset);
        directory += name;
    }
    const std::uint32_t start = static_cast<std::uint32_t>(out.size());
    out += directory;
    u32(out, 0x06054b50);
    u16(out, 0);
    u16(out, 0);
    u16(out, static_cast<std::uint16_t>(files.size()));
    u16(out, static_cast<std::uint16_t>(files.size()));
    u32(out, static_cast<std::uint32_t>(directory.size()));
    u32(out, start);
    u16(out, 0);
    return out;
}

std::string xmlEscaped(const std::string& text)
{
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

} // namespace

std::string export3mf(const std::vector<PrintBody>& bodies, const std::string& path, bool inches)
{
    if (bodies.empty())
        return QT_TRANSLATE_NOOP("Model", "No bodies to export");
    const double scale = inches ? 1.0 / 25.4 : 1.0;
    std::ostringstream model;
    model.imbue(std::locale::classic());
    model.precision(9);
    model << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<model unit=\"" << (inches ? "inch" : "millimeter") << "\" xml:lang=\"en-US\" "
          << "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
          << " <metadata name=\"Application\">Nibform</metadata>\n <resources>\n  <basematerials id=\"1\">\n";
    for (const PrintBody& b : bodies) {
        char color[8];
        std::snprintf(color, sizeof color, "#%02X%02X%02X", static_cast<int>(std::lround(std::clamp(b.color[0], 0.0, 1.0) * 255)),
                      static_cast<int>(std::lround(std::clamp(b.color[1], 0.0, 1.0) * 255)),
                      static_cast<int>(std::lround(std::clamp(b.color[2], 0.0, 1.0) * 255)));
        model << "   <base name=\"" << xmlEscaped(b.name) << "\" displaycolor=\"" << color << "\"/>\n";
    }
    model << "  </basematerials>\n";
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        // Mesh a copy, as for STL; corners shared between faces become one vertex.
        const TopoDS_Shape shape = BRepBuilderAPI_Copy(bodies[i].shape).Shape();
        BRepMesh_IncrementalMesh mesh(shape, 0.05, false, 0.5 * M_PI / 180.0, true);
        if (!mesh.IsDone())
            return QT_TRANSLATE_NOOP("Model", "Could not mesh the bodies for 3MF");
        std::map<std::tuple<double, double, double>, int> index;
        std::vector<gp_Pnt> vertices;
        std::vector<std::array<int, 3>> triangles;
        for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            TopLoc_Location location;
            const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, location);
            if (tri.IsNull())
                continue;
            std::vector<int> ids(tri->NbNodes() + 1);
            for (int n = 1; n <= tri->NbNodes(); ++n) {
                const gp_Pnt p = tri->Node(n).Transformed(location.Transformation());
                const auto key = std::make_tuple(p.X(), p.Y(), p.Z());
                const auto found = index.find(key);
                if (found != index.end()) {
                    ids[n] = found->second;
                } else {
                    ids[n] = static_cast<int>(vertices.size());
                    index.emplace(key, ids[n]);
                    vertices.push_back(p);
                }
            }
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            for (int t = 1; t <= tri->NbTriangles(); ++t) {
                int a = 0, b = 0, c = 0;
                tri->Triangle(t).Get(a, b, c);
                if (reversed)
                    std::swap(b, c);
                if (ids[a] != ids[b] && ids[b] != ids[c] && ids[a] != ids[c])
                    triangles.push_back({ids[a], ids[b], ids[c]});
            }
        }
        model << "  <object id=\"" << i + 2 << "\" type=\"model\" name=\"" << xmlEscaped(bodies[i].name)
              << "\" pid=\"1\" pindex=\"" << i << "\">\n   <mesh>\n    <vertices>\n";
        for (const gp_Pnt& p : vertices)
            model << "     <vertex x=\"" << p.X() * scale << "\" y=\"" << p.Y() * scale << "\" z=\"" << p.Z() * scale << "\"/>\n";
        model << "    </vertices>\n    <triangles>\n";
        for (const auto& t : triangles)
            model << "     <triangle v1=\"" << t[0] << "\" v2=\"" << t[1] << "\" v3=\"" << t[2] << "\"/>\n";
        model << "    </triangles>\n   </mesh>\n  </object>\n";
    }
    model << " </resources>\n <build>\n";
    for (std::size_t i = 0; i < bodies.size(); ++i)
        model << "  <item objectid=\"" << i + 2 << "\"/>\n";
    model << " </build>\n</model>\n";

    const std::string types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>"
        "</Types>\n";
    const std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
        "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>\n";
    const std::string zip = zipStored({{"[Content_Types].xml", types}, {"_rels/.rels", rels}, {"3D/3dmodel.model", model.str()}});
    std::ofstream file(path, std::ios::binary);
    file.write(zip.data(), static_cast<std::streamsize>(zip.size()));
    if (!file)
        return QT_TRANSLATE_NOOP("Model", "Could not write the 3MF file");
    return {};
}

// --- Import -------------------------------------------------------------------------

namespace {

std::string extensionOf(const std::string& path)
{
    const auto dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? std::string() : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

// Above this many triangles an STL stays a mesh shell (sewing and modelling get slow).
constexpr int kMaxSolidTriangles = 60000;

} // namespace

std::optional<TopoDS_Shape> importShape(const std::string& path, std::string& error)
{
    const std::string ext = extensionOf(path);
    try {
        if (ext == "step" || ext == "stp") {
            STEPControl_Reader reader;
            if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
                error = QT_TRANSLATE_NOOP("Model", "Could not read the STEP file");
                return std::nullopt;
            }
            reader.TransferRoots();
            const TopoDS_Shape shape = reader.OneShape();
            if (shape.IsNull()) {
                error = QT_TRANSLATE_NOOP("Model", "The file has no geometry");
                return std::nullopt;
            }
            return shape;
        }
        if (ext == "iges" || ext == "igs") {
            IGESControl_Reader reader;
            if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
                error = QT_TRANSLATE_NOOP("Model", "Could not read the IGES file");
                return std::nullopt;
            }
            reader.TransferRoots();
            TopoDS_Shape shape = reader.OneShape();
            if (shape.IsNull()) {
                error = QT_TRANSLATE_NOOP("Model", "The file has no geometry");
                return std::nullopt;
            }
            // IGES often carries loose faces: sew them, and close them into a solid if they close.
            BRepBuilderAPI_Sewing sewing(1e-4);
            sewing.Add(shape);
            sewing.Perform();
            const TopoDS_Shape sewn = sewing.SewedShape();
            if (!sewn.IsNull() && sewn.ShapeType() == TopAbs_SHELL) {
                BRepBuilderAPI_MakeSolid solid(TopoDS::Shell(sewn));
                if (solid.IsDone() && BRepCheck_Analyzer(solid.Shape()).IsValid()) {
                    ShapeFix_Solid fix(TopoDS::Solid(solid.Shape()));
                    fix.Perform();
                    return fix.Solid();
                }
            }
            bool hasSolid = false;
            for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next())
                hasSolid = true;
            return hasSolid ? shape : (sewn.IsNull() ? shape : sewn);
        }
        if (ext == "stl") {
            TopoDS_Shape mesh;
            StlAPI_Reader reader;
            if (!reader.Read(mesh, path.c_str()) || mesh.IsNull()) {
                error = QT_TRANSLATE_NOOP("Model", "Could not read the STL file");
                return std::nullopt;
            }
            int faces = 0;
            for (TopExp_Explorer it(mesh, TopAbs_FACE); it.More(); it.Next())
                ++faces;
            if (faces > kMaxSolidTriangles)
                return mesh; // a big mesh: a reference to look at
            BRepBuilderAPI_Sewing sewing(1e-5);
            sewing.Add(mesh);
            sewing.Perform();
            const TopoDS_Shape sewn = sewing.SewedShape();
            if (!sewn.IsNull() && sewn.ShapeType() == TopAbs_SHELL && BRep_Tool::IsClosed(sewn)) {
                BRepBuilderAPI_MakeSolid solid(TopoDS::Shell(sewn));
                if (solid.IsDone()) {
                    ShapeFix_Solid fix(TopoDS::Solid(solid.Shape()));
                    fix.Perform();
                    return fix.Solid();
                }
            }
            return sewn.IsNull() ? mesh : sewn;
        }
    } catch (const Standard_Failure&) {
        error = QT_TRANSLATE_NOOP("Model", "The file could not be read");
        return std::nullopt;
    }
    error = QT_TRANSLATE_NOOP("Model", "Unknown file type (STEP, IGES or STL)");
    return std::nullopt;
}

std::string shapeToText(const TopoDS_Shape& shape)
{
    std::ostringstream out;
    BRepTools::Write(shape, out);
    return out.str();
}

std::optional<TopoDS_Shape> shapeFromText(const std::string& text)
{
    TopoDS_Shape shape;
    std::istringstream in(text);
    BRep_Builder builder;
    try {
        BRepTools::Read(shape, in, builder);
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
    if (shape.IsNull())
        return std::nullopt;
    return shape;
}
