#include "model/Rebuild.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <type_traits>

namespace {

// Keys are byte strings: every value that matters, written exactly (doubles bit for bit,
// so any change at all counts as one).
struct KeyWriter
{
    std::string& out;

    template <typename T>
    void raw(const T& v)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        char bytes[sizeof(T)];
        std::memcpy(bytes, &v, sizeof(T));
        out.append(bytes, sizeof(T));
    }
    void num(double v) { raw(v); }
    void num(int v) { raw(v); }
    void flag(bool v) { out.push_back(v ? '1' : '0'); }
    template <typename E>
        requires std::is_enum_v<E>
    void kind(E v)
    {
        raw(static_cast<int>(v));
    }
    void text(const std::string& s)
    {
        raw(s.size());
        out += s;
    }
    void point(const gp_Pnt& p)
    {
        num(p.X());
        num(p.Y());
        num(p.Z());
    }
    void point(const gp_Pnt2d& p)
    {
        num(p.X());
        num(p.Y());
    }
    void dir(const gp_Dir& d)
    {
        num(d.X());
        num(d.Y());
        num(d.Z());
    }
    void frame(const gp_Ax3& a)
    {
        point(a.Location());
        dir(a.Direction());
        dir(a.XDirection());
        dir(a.YDirection());
    }
    void sep(char c) { out.push_back(c); }
};

std::uint64_t fnv(std::uint64_t h, const void* data, std::size_t size)
{
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

constexpr std::uint64_t kFnvBasis = 0xcbf29ce484222325ULL;

std::uint64_t chainKey(std::uint64_t previous, const std::string& key)
{
    return fnv(fnv(kFnvBasis, &previous, sizeof previous), key.data(), key.size());
}

} // namespace

// Everything a feature holds goes into its key: a field left out would let a change of it
// go unnoticed, and the old result be shown. A new field changes these sizes; then add it
// to featureKey() (or sketchGeometryKey()) and update the numbers. (Checked where they
// are known: 64-bit libstdc++, as on the development machine and in CI.)
#if defined(__GLIBCXX__) && __SIZEOF_POINTER__ == 8
static_assert(sizeof(Feature) == 472, "Feature changed: is the new field in featureKey()?");
static_assert(sizeof(SketchEntity) == 72, "SketchEntity changed: is the new field in sketchGeometryKey()?");
static_assert(sizeof(SketchPoint) == 24, "SketchPoint changed: is the new field in sketchGeometryKey()?");
#endif

std::string sketchGeometryKey(const Sketch& sketch)
{
    std::string out;
    KeyWriter w{out};
    w.frame(sketch.plane());
    w.raw(sketch.points().size());
    for (const SketchPoint& p : sketch.points()) {
        w.num(p.id);
        w.point(p.pos);
    }
    w.raw(sketch.entities().size());
    for (const SketchEntity& e : sketch.entities()) {
        w.num(e.id);
        w.kind(e.kind);
        w.num(e.a);
        w.num(e.b);
        w.num(e.c);
        w.num(e.radius);
        w.flag(e.construction);
        w.flag(e.closed);
        w.raw(e.through.size());
        for (PointId p : e.through)
            w.num(p);
    }
    return out;
}

RebuildCache::RebuildCache(std::size_t capacity)
    : m_capacity(std::max<std::size_t>(capacity, 16))
{
}

void RebuildCache::clear()
{
    m_steps.clear();
    m_imports.clear();
}

std::uint64_t RebuildCache::importHash(const std::shared_ptr<const std::string>& shape)
{
    if (!shape)
        return 0;
    std::erase_if(m_imports, [](const auto& entry) { return entry.first.expired(); });
    for (const auto& [known, hash] : m_imports) {
        if (known.lock() == shape)
            return hash;
    }
    const std::uint64_t hash = fnv(kFnvBasis, shape->data(), shape->size());
    m_imports.emplace_back(shape, hash);
    return hash;
}

std::string RebuildCache::featureKey(const Feature& f, const SketchLookup& sketches,
                                     std::unordered_map<int, std::string>& sketchKeys)
{
    std::string out;
    KeyWriter w{out};
    // The geometry of every sketch it uses: a region moving, a path or a point a hole
    // sits on changing all change what it builds.
    auto usesSketch = [&](int id) {
        auto known = sketchKeys.find(id);
        if (known == sketchKeys.end()) {
            const Sketch* sketch = sketches(id);
            known = sketchKeys.emplace(id, sketch ? sketchGeometryKey(*sketch) : std::string("missing")).first;
        }
        w.sep('S');
        w.num(id);
        w.text(known->second);
    };

    w.kind(f.kind);
    w.num(f.id);
    w.num(f.sketch);
    w.num(f.value);
    w.kind(f.operation);
    w.kind(f.boolean);
    w.raw(f.regions.size());
    for (const RegionPick& r : f.regions) {
        w.num(r.sketch);
        w.point(r.inside);
    }
    w.raw(f.faces.size());
    for (const FacePick& p : f.faces) {
        w.point(p.middle);
        w.dir(p.normal);
    }
    w.raw(f.edges.size());
    for (const EdgePick& p : f.edges) {
        w.point(p.middle);
        w.dir(p.direction);
    }
    w.raw(f.bodies.size());
    for (const gp_Pnt& p : f.bodies)
        w.point(p);
    w.kind(f.axis.kind);
    w.point(f.axis.edge.middle);
    w.dir(f.axis.edge.direction);
    w.num(f.axis.sketch);
    w.num(f.axis.entity);
    w.num(f.axis.datum);
    w.kind(f.plane.kind);
    w.point(f.plane.face.middle);
    w.dir(f.plane.face.normal);
    w.num(f.plane.datum);
    w.num(f.pathSketch);
    w.num(f.pathEntity);
    w.kind(f.pattern);
    w.num(f.count);
    w.num(f.source);
    w.kind(f.hole.kind);
    w.num(f.hole.diameter);
    w.num(f.hole.depth);
    w.num(f.hole.headDiameter);
    w.num(f.hole.headDepth);
    w.raw(f.holeAt.size());
    for (const SketchPointRef& p : f.holeAt) {
        w.num(p.sketch);
        w.num(p.point);
    }
    w.raw(f.points.size());
    for (const gp_Pnt& p : f.points)
        w.point(p);
    w.kind(f.datum);
    w.raw(f.matrix.size());
    for (double v : f.matrix)
        w.num(v);
    w.flag(f.copy);
    w.raw(importHash(f.shape));
    w.raw(f.shape ? f.shape->size() : std::size_t(0));
    w.text(f.label);
    w.flag(f.reference);

    if (f.kind == Feature::Kind::Sketch)
        usesSketch(f.sketch);
    for (const RegionPick& r : f.regions)
        usesSketch(r.sketch);
    if (f.axis.kind == AxisRef::Kind::SketchLine)
        usesSketch(f.axis.sketch);
    if (f.pathSketch >= 0)
        usesSketch(f.pathSketch);
    for (const SketchPointRef& p : f.holeAt)
        usesSketch(p.sketch);
    return out;
}

void RebuildCache::store(std::uint64_t chain, Step step)
{
    step.used = ++m_clock;
    m_steps[chain] = std::move(step);
}

void RebuildCache::evict()
{
    if (m_steps.size() <= m_capacity)
        return;
    // Down to three quarters, so this doesn't run on every rebuild.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> byUse; // (used, chain)
    byUse.reserve(m_steps.size());
    for (const auto& [chain, step] : m_steps)
        byUse.emplace_back(step.used, chain);
    std::sort(byUse.begin(), byUse.end());
    const std::size_t drop = m_steps.size() - m_capacity * 3 / 4;
    for (std::size_t i = 0; i < drop; ++i)
        m_steps.erase(byUse[i].second);
}

std::optional<RebuildResult> RebuildCache::rebuild(const SketchLookup& sketches, const std::vector<Feature>& features,
                                                   const RebuildControl& control)
{
    m_stats = {};
    std::unordered_map<int, std::string> sketchKeys;
    std::vector<std::string> keys;
    std::vector<std::uint64_t> chains;
    keys.reserve(features.size());
    chains.reserve(features.size());
    std::uint64_t previous = kFnvBasis;
    for (const Feature& f : features) {
        keys.push_back(featureKey(f, sketches, sketchKeys));
        previous = chainKey(previous, keys.back());
        chains.push_back(previous);
    }

    // The longest run of steps from the start that is cached as it is now.
    RebuildResult result;
    std::size_t start = 0;
    const Step* last = nullptr;
    for (; start < features.size(); ++start) {
        const auto found = m_steps.find(chains[start]);
        const std::uint64_t before = start == 0 ? kFnvBasis : chains[start - 1];
        if (found == m_steps.end() || found->second.previous != before || found->second.key != keys[start])
            break;
        found->second.used = ++m_clock;
        result.errors.push_back(found->second.error);
        last = &found->second;
    }
    if (last) {
        result.bodies = last->bodies;
        result.context = last->context;
    }
    m_stats.reused = start;

    for (std::size_t i = start; i < features.size(); ++i) {
        if (control.cancelled && control.cancelled())
            return std::nullopt;
        const Feature& f = features[i];
        FeatureResult step = applyFeature(sketches, result.bodies, f, result.context);
        result.errors.push_back(step.error);
        if (step.ok()) {
            remember(result.context, f, step);
            result.bodies = std::move(step.bodies);
        }
        Step cached;
        cached.previous = i == 0 ? kFnvBasis : chains[i - 1];
        cached.key = keys[i];
        cached.bodies = result.bodies;
        cached.context = result.context;
        cached.error = step.error;
        store(chains[i], std::move(cached));
        ++m_stats.replayed;
        if (control.progress)
            control.progress(i + 1 - start, features.size() - start);
    }
    evict();
    return result;
}

// --- BackgroundRebuilder ------------------------------------------------------------------

BackgroundRebuilder::BackgroundRebuilder(std::function<void()> ready,
                                         std::function<void(int, std::size_t, std::size_t)> progress)
    : m_ready(std::move(ready))
    , m_progress(std::move(progress))
    , m_thread([this] { run(); })
{
}

BackgroundRebuilder::~BackgroundRebuilder()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

int BackgroundRebuilder::request(Job job)
{
    int ticket;
    {
        std::lock_guard lock(m_mutex);
        ticket = ++m_lastTicket;
        m_pending.emplace(ticket, std::move(job)); // replaces one not started yet
    }
    m_wake.notify_all();
    return ticket;
}

bool BackgroundRebuilder::wait(int ticket, int milliseconds)
{
    std::unique_lock lock(m_mutex);
    auto finished = [&] { return m_doneTicket >= ticket; };
    if (milliseconds < 0) {
        m_finished.wait(lock, finished);
        return true;
    }
    return m_finished.wait_for(lock, std::chrono::milliseconds(milliseconds), finished);
}

std::optional<BackgroundRebuilder::Done> BackgroundRebuilder::take()
{
    std::lock_guard lock(m_mutex);
    std::optional<Done> done = std::move(m_done);
    m_done.reset();
    return done;
}

bool BackgroundRebuilder::busy() const
{
    std::lock_guard lock(m_mutex);
    return m_running != 0 || m_pending.has_value();
}

void BackgroundRebuilder::run()
{
    for (;;) {
        std::pair<int, Job> job;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || m_pending.has_value(); });
            if (m_stop)
                return;
            job = std::move(*m_pending);
            m_pending.reset();
            m_running = job.first;
        }
        const int ticket = job.first;
        const auto& sketches = job.second.sketches;
        SketchLookup lookup = [&sketches](int id) -> const Sketch* {
            for (const auto& [sid, sketch] : sketches) {
                if (sid == id)
                    return &sketch;
            }
            return nullptr;
        };
        RebuildControl control;
        // A newer request makes this one pointless.
        control.cancelled = [this] {
            std::lock_guard lock(m_mutex);
            return m_stop || m_pending.has_value();
        };
        control.progress = [this, ticket](std::size_t done, std::size_t total) {
            if (m_progress)
                m_progress(ticket, done, total);
        };
        std::optional<RebuildResult> result = m_cache.rebuild(lookup, job.second.features, control);
        {
            std::lock_guard lock(m_mutex);
            m_running = 0;
            if (result) {
                m_done = Done{ticket, std::move(*result), m_cache.lastStats()};
                m_doneTicket = ticket;
            }
        }
        if (result) {
            m_finished.notify_all();
            if (m_ready)
                m_ready();
        }
    }
}
