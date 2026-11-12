// How long rebuilds take on long histories (M14): a full replay, and the cached
// rebuilds that follow an edit early, in the middle and at the end. Not a test: run
// `./build/bench_rebuild [pockets...]` and read the numbers.

#include "LongModel.h"

#include "model/Rebuild.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace {

double secondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void bench(int pockets)
{
    LongModel m = longModel(pockets);
    const std::size_t n = m.history.size();
    std::printf("%d pockets, %zu features\n", pockets, n);

    // Where the time goes, by kind of feature (one full replay, by hand).
    std::map<std::string, std::pair<double, int>> byKind;
    const char* names[] = {"sketch", "extrude", "fillet", "chamfer", "combine", "delete", "shell", "revolve",
                           "sweep", "loft", "mirror", "pattern", "hole", "datum", "move", "import"};
    RebuildResult manual;
    auto start = std::chrono::steady_clock::now();
    double slowest = 0.0;
    std::size_t slowestAt = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto t = std::chrono::steady_clock::now();
        FeatureResult step = applyFeature(m.lookup(), manual.bodies, m.history[i], manual.context);
        if (step.ok()) {
            remember(manual.context, m.history[i], step);
            manual.bodies = std::move(step.bodies);
        }
        const double s = secondsSince(t);
        auto& entry = byKind[names[static_cast<int>(m.history[i].kind)]];
        entry.first += s;
        ++entry.second;
        if (s > slowest) {
            slowest = s;
            slowestAt = i;
        }
    }
    const double full = secondsSince(start);
    std::printf("  full replay       %8.3f s  (slowest feature #%zu: %.3f s)\n", full, slowestAt, slowest);
    for (const auto& [kind, entry] : byKind)
        std::printf("    %-8s %4d × %8.2f ms\n", kind.c_str(), entry.second, 1000.0 * entry.first / entry.second);

    RebuildCache cache;
    start = std::chrono::steady_clock::now();
    cache.rebuild(m.lookup(), m.history);
    std::printf("  first, cached     %8.3f s\n", secondsSince(start));
    start = std::chrono::steady_clock::now();
    cache.rebuild(m.lookup(), m.history);
    std::printf("  unchanged         %8.3f s\n", secondsSince(start));

    auto edit = [&](const char* what, std::size_t pocket) {
        m.history[m.pocketFeature[pocket]].value -= 0.5;
        const auto t = std::chrono::steady_clock::now();
        cache.rebuild(m.lookup(), m.history);
        const auto stats = cache.lastStats();
        std::printf("  edit %-12s %8.3f s  (%zu replayed)\n", what, secondsSince(t), stats.replayed);
    };
    edit("early", 1);
    edit("middle", m.pocketFeature.size() / 2);
    edit("last", m.pocketFeature.size() - 1);
    // Undo of the early edit: all cached.
    m.history[m.pocketFeature[1]].value += 0.5;
    m.history[m.pocketFeature[m.pocketFeature.size() / 2]].value += 0.5;
    m.history[m.pocketFeature.back()].value += 0.5;
    start = std::chrono::steady_clock::now();
    cache.rebuild(m.lookup(), m.history);
    std::printf("  undo all three    %8.3f s  (%zu replayed)\n", secondsSince(start), cache.lastStats().replayed);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        bench(50);
        bench(100);
    }
    for (int i = 1; i < argc; ++i)
        bench(std::atoi(argv[i]));
}
