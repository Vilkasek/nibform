// A long history for rebuild tests and benchmarks: a plate, then pockets cut from
// sketches on its top and holes drilled through it, in turn.
#pragma once

#include "model/Features.h"
#include "sketch/Sketch.h"

#include <algorithm>
#include <map>
#include <vector>

struct LongModel
{
    std::map<int, Sketch> sketches;
    std::vector<Feature> history;

    SketchLookup lookup() const
    {
        return [this](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };
    }

    // The history index of the extrusion that cuts pocket `i`, and its sketch's id.
    std::vector<std::size_t> pocketFeature;
    std::vector<int> pocketSketch;
};

// A `size` × `size` × 10 plate, then `pockets` round pockets (a sketch and a cut each),
// with a hole through after every `holeEvery` of them: 2 + 2 × pockets + pockets / holeEvery
// features.
inline LongModel longModel(int pockets, int holeEvery = 4, double size = 400.0)
{
    LongModel m;
    const gp_Ax3 ground(gp::Origin(), gp::DZ(), gp::DX());
    const gp_Ax3 top(gp_Pnt(0, 0, 10), gp::DZ(), gp::DX());
    Sketch plate(ground);
    const double h = size / 2.0;
    const PointId a = plate.addPoint({-h, -h}), b = plate.addPoint({h, -h}), c = plate.addPoint({h, h}),
                  d = plate.addPoint({-h, h});
    plate.addLine(a, b);
    plate.addLine(b, c);
    plate.addLine(c, d);
    plate.addLine(d, a);
    m.sketches.emplace(1, plate);
    Feature sketch;
    sketch.kind = Feature::Kind::Sketch;
    sketch.sketch = 1;
    m.history.push_back(sketch);
    Feature extrude;
    extrude.kind = Feature::Kind::Extrude;
    extrude.regions = {{1, gp_Pnt2d(0, 0)}};
    extrude.value = 10.0;
    m.history.push_back(extrude);

    // On a grid, 20 mm apart, away from the edges.
    const int perRow = std::max(1, static_cast<int>((size - 20.0) / 20.0));
    int nextSketch = 2;
    for (int i = 0; i < pockets; ++i) {
        const double x = -h + 20.0 + 20.0 * (i % perRow);
        const double y = -h + 20.0 + 20.0 * static_cast<double>(i / perRow); // row by row
        Sketch s(top);
        s.addCircle(s.addPoint({x, y}), 4.0);
        const int id = nextSketch++;
        m.sketches.emplace(id, s);
        Feature f;
        f.kind = Feature::Kind::Sketch;
        f.sketch = id;
        m.history.push_back(f);
        Feature cut;
        cut.kind = Feature::Kind::Extrude;
        cut.regions = {{id, gp_Pnt2d(x, y)}};
        cut.value = -4.0;
        m.pocketFeature.push_back(m.history.size());
        m.pocketSketch.push_back(id);
        m.history.push_back(cut);
        if (holeEvery > 0 && (i + 1) % holeEvery == 0) {
            Feature hole;
            hole.kind = Feature::Kind::Hole;
            hole.faces = {{gp_Pnt(0, 0, 10), gp::DZ()}};
            hole.points = {gp_Pnt(x + 9.0, y + 9.0, 10.0)};
            hole.hole.diameter = 3.0;
            m.history.push_back(hole);
        }
    }
    numberFeatures(m.history);
    return m;
}
