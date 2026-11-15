#include "model/TextShapes.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Font_BRepFont.hxx>
#include <Font_BRepTextBuilder.hxx>
#include <Font_FontMgr.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <NCollection_String.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_SequenceOfHAsciiString.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <map>
#include <mutex>
#include <tuple>

namespace {

// A text's letters, laid out from the origin along X on the XY plane.
struct Glyphs
{
    TopoDS_Shape shape;
    std::vector<std::vector<gp_Pnt2d>> outlines; // in that plane
    std::optional<std::pair<gp_Pnt2d, gp_Pnt2d>> bounds;
};

using GlyphKey = std::tuple<std::string, std::string, double, bool, bool>;

std::mutex g_mutex; // FreeType and the cache, shared with the rebuild thread
std::map<GlyphKey, Glyphs> g_cache;

Glyphs makeGlyphs(const SketchText& text)
{
    Glyphs out;
    if (text.text.empty() || !(text.height > 0.0) || text.height > 1e5)
        return out;
    const Font_FontAspect aspect = text.bold && text.italic ? Font_FontAspect_BoldItalic
                                 : text.bold                ? Font_FontAspect_Bold
                                 : text.italic              ? Font_FontAspect_Italic
                                                            : Font_FontAspect_Regular;
    Font_BRepFont font;
    const std::string family = text.font.empty() ? std::string("sans-serif") : text.font;
    // A missing family falls back to another one; OCCT says so on the console every time.
    const Handle(Message_Messenger)& messenger = Message::DefaultMessenger();
    std::vector<Message_Gravity> levels;
    for (Message_SequenceOfPrinters::Iterator it(messenger->Printers()); it.More(); it.Next()) {
        levels.push_back(it.Value()->GetTraceLevel());
        it.Value()->SetTraceLevel(Message_Fail);
    }
    const bool found = font.FindAndInit(family.c_str(), aspect, text.height, Font_StrictLevel_Any);
    std::size_t i = 0;
    for (Message_SequenceOfPrinters::Iterator it(messenger->Printers()); it.More(); it.Next())
        it.Value()->SetTraceLevel(levels[i++]);
    if (!found)
        return out;
    try {
        Font_BRepTextBuilder builder;
        out.shape = builder.Perform(font, NCollection_String(text.text.c_str()), gp_Ax3());
    } catch (const Standard_Failure&) {
        return {};
    }
    if (out.shape.IsNull())
        return {};
    Bnd_Box box;
    for (TopExp_Explorer it(out.shape, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(it.Current());
        if (BRep_Tool::Degenerated(edge))
            continue;
        BRepAdaptor_Curve curve(edge);
        GCPnts_QuasiUniformDeflection points(curve, text.height * 0.004);
        if (!points.IsDone())
            continue;
        std::vector<gp_Pnt2d> line;
        for (int i = 1; i <= points.NbPoints(); ++i) {
            const gp_Pnt p = points.Value(i);
            line.emplace_back(p.X(), p.Y());
            box.Add(p);
        }
        if (line.size() >= 2)
            out.outlines.push_back(std::move(line));
    }
    if (!box.IsVoid()) {
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        out.bounds = std::pair(gp_Pnt2d(x0, y0), gp_Pnt2d(x1, y1));
    }
    return out;
}

const Glyphs& glyphs(const SketchText& text)
{
    // Callers hold g_mutex.
    const GlyphKey key{text.text, text.font, text.height, text.bold, text.italic};
    auto it = g_cache.find(key);
    if (it == g_cache.end()) {
        if (g_cache.size() > 256)
            g_cache.clear();
        it = g_cache.emplace(key, makeGlyphs(text)).first;
    }
    return it->second;
}

} // namespace

std::vector<TopoDS_Edge> textEdges(const Sketch& sketch, const SketchText& text)
{
    if (!sketch.point(text.at))
        return {};
    TopoDS_Shape shape;
    {
        const std::lock_guard lock(g_mutex);
        shape = glyphs(text).shape;
    }
    if (shape.IsNull())
        return {};
    const gp_Ax3& plane = sketch.plane();
    gp_Trsf place;
    place.SetDisplacement(gp_Ax3(), gp_Ax3(sketch.to3d(sketch.pos(text.at)), plane.Direction(), plane.XDirection()));
    const TopoDS_Shape placed = BRepBuilderAPI_Transform(shape, place, true).Shape();
    std::vector<TopoDS_Edge> edges;
    for (TopExp_Explorer it(placed, TopAbs_EDGE); it.More(); it.Next()) {
        if (!BRep_Tool::Degenerated(TopoDS::Edge(it.Current())))
            edges.push_back(TopoDS::Edge(it.Current()));
    }
    return edges;
}

std::vector<std::vector<gp_Pnt2d>> textOutlines(const Sketch& sketch, const SketchText& text)
{
    if (!sketch.point(text.at))
        return {};
    std::vector<std::vector<gp_Pnt2d>> outlines;
    {
        const std::lock_guard lock(g_mutex);
        outlines = glyphs(text).outlines;
    }
    const gp_Vec2d shift(sketch.pos(text.at).XY());
    for (auto& line : outlines) {
        for (gp_Pnt2d& p : line)
            p.Translate(shift);
    }
    return outlines;
}

std::optional<std::pair<gp_Pnt2d, gp_Pnt2d>> textBounds(const Sketch& sketch, const SketchText& text)
{
    if (!sketch.point(text.at))
        return std::nullopt;
    std::optional<std::pair<gp_Pnt2d, gp_Pnt2d>> bounds;
    {
        const std::lock_guard lock(g_mutex);
        bounds = glyphs(text).bounds;
    }
    if (!bounds)
        return std::nullopt;
    const gp_Vec2d shift(sketch.pos(text.at).XY());
    return std::pair(bounds->first.Translated(shift), bounds->second.Translated(shift));
}

bool textContains(const Sketch& sketch, const SketchText& text, const gp_Pnt2d& p)
{
    if (!sketch.point(text.at))
        return false;
    TopoDS_Shape shape;
    {
        const std::lock_guard lock(g_mutex);
        shape = glyphs(text).shape;
    }
    if (shape.IsNull())
        return false;
    const gp_Pnt2d local = p.Translated(-gp_Vec2d(sketch.pos(text.at).XY()));
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        BRepClass_FaceClassifier inside(TopoDS::Face(it.Current()), gp_Pnt(local.X(), local.Y(), 0.0), 1e-7);
        if (inside.State() == TopAbs_IN)
            return true;
    }
    return false;
}

std::optional<EntityId> textAt(const Sketch& sketch, const gp_Pnt2d& p, double tolerance)
{
    const auto& texts = sketch.texts();
    for (auto t = texts.rbegin(); t != texts.rend(); ++t) {
        const auto box = textBounds(sketch, *t);
        if (box && p.X() >= box->first.X() - tolerance && p.X() <= box->second.X() + tolerance
            && p.Y() >= box->first.Y() - tolerance && p.Y() <= box->second.Y() + tolerance)
            return t->id;
    }
    return std::nullopt;
}

std::vector<std::string> fontFamilies()
{
    const std::lock_guard lock(g_mutex);
    TColStd_SequenceOfHAsciiString names;
    Font_FontMgr::GetInstance()->GetAvailableFontsNames(names);
    std::vector<std::string> out;
    for (const auto& name : names)
        out.emplace_back(name->ToCString());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
