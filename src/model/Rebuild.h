#pragma once

#include "model/Features.h"
#include "sketch/Sketch.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Speeding up rebuilds: the outcome of every feature is kept, keyed by everything that
// went into it (the feature itself, the geometry of the sketches it uses, and the key of
// the step before it), so a rebuild replays only from the first feature that changed.
// Editing an early feature and putting it back, undo, redo and previews that are
// accepted all find their steps already built.

// What a rebuild is told along the way (any may be empty).
struct RebuildControl
{
    std::function<bool()> cancelled;                        // checked between features
    std::function<void(std::size_t done, std::size_t total)> progress; // after each replayed one
};

class RebuildCache
{
public:
    // How many steps are kept (the least recently used go first); a few versions of a
    // long history.
    explicit RebuildCache(std::size_t capacity = 2000);

    // The same as rebuild(), reusing what earlier calls built. Empty when cancelled (the
    // features replayed so far stay cached). Not thread safe: one call at a time.
    std::optional<RebuildResult> rebuild(const SketchLookup& sketches, const std::vector<Feature>& features,
                                         const RebuildControl& control = {});

    struct Stats
    {
        std::size_t reused = 0;   // features taken from the cache by the last rebuild
        std::size_t replayed = 0; // and applied again
    };
    Stats lastStats() const { return m_stats; }
    std::size_t size() const { return m_steps.size(); }
    void clear();

private:
    struct Step
    {
        std::uint64_t previous = 0; // the chain key of the step before
        std::string key;            // this feature and the sketch geometry it uses
        std::vector<TopoDS_Shape> bodies;
        ModelContext context;
        std::string error;
        std::uint64_t used = 0;
    };
    std::string featureKey(const Feature& feature, const SketchLookup& sketches,
                           std::unordered_map<int, std::string>& sketchKeys);
    std::uint64_t importHash(const std::shared_ptr<const std::string>& shape);
    void store(std::uint64_t chain, Step step);
    void evict();

    std::size_t m_capacity;
    std::unordered_map<std::uint64_t, Step> m_steps; // by chain key
    std::uint64_t m_clock = 0;
    Stats m_stats;
    // Imported geometry can be large: hashed once per shared copy.
    std::vector<std::pair<std::weak_ptr<const std::string>, std::uint64_t>> m_imports;
};

// The geometry a rebuild depends on, of one sketch (its plane, points and curves; not
// its relations, which only ever moved the points).
std::string sketchGeometryKey(const Sketch& sketch);

// Rebuilds on a thread of its own, so the window stays responsive with long histories.
// Only the latest request matters: a newer one abandons the one running (at the next
// feature; what it built stays cached for the newer one).
class BackgroundRebuilder
{
public:
    struct Job
    {
        std::vector<std::pair<int, Sketch>> sketches; // by id: copies, the editor keeps editing its own
        std::vector<Feature> features;
    };
    struct Done
    {
        int ticket = 0;
        RebuildResult result;
        RebuildCache::Stats stats;
    };

    // `ready` and `progress` are called on the worker thread (post them on to the UI).
    BackgroundRebuilder(std::function<void()> ready, std::function<void(int ticket, std::size_t done, std::size_t total)> progress);
    ~BackgroundRebuilder();
    BackgroundRebuilder(const BackgroundRebuilder&) = delete;
    BackgroundRebuilder& operator=(const BackgroundRebuilder&) = delete;

    // Queues a rebuild; returns its ticket (they grow).
    int request(Job job);
    // Waits up to `milliseconds` (forever when negative) until request `ticket` or a newer
    // one has finished (an abandoned request never does); then take() has it.
    bool wait(int ticket, int milliseconds);
    // The latest finished rebuild, once (empty when there is none new).
    std::optional<Done> take();
    bool busy() const;

private:
    void run();

    std::function<void()> m_ready;
    std::function<void(int, std::size_t, std::size_t)> m_progress;
    RebuildCache m_cache; // the worker's alone
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;     // a job came, or stop
    std::condition_variable m_finished; // a job finished
    std::optional<std::pair<int, Job>> m_pending;
    int m_running = 0;  // the ticket being built, 0 when idle
    int m_lastTicket = 0;
    int m_doneTicket = 0; // the latest finished (or abandoned in favour of a newer one)
    std::optional<Done> m_done;
    bool m_stop = false;
    std::thread m_thread; // last: started when the rest is ready
};
