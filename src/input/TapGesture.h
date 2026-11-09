#pragma once

#include <cstdint>
#include <map>

// Quick taps of several fingers at once on a touch screen (two fingers undo, three redo).
// Fed with every touch point that drives the view; times in milliseconds, positions in
// logical pixels.
class TapGesture
{
public:
    // From the first finger down to the last one up.
    static constexpr std::int64_t kMaxMs = 350;
    // How far a finger may slide and still be tapping.
    static constexpr double kMaxMovePx = 16.0;

    void down(int id, double x, double y, std::int64_t time);
    void move(int id, double x, double y);
    // A finger lifted: when it was the last one and the fingers only tapped, how many
    // there were (at most at once); 0 otherwise.
    int up(int id, std::int64_t time);
    // The touches were taken over (by the pen, a cancel): this gesture is no tap.
    void cancel();

private:
    struct Start
    {
        double x, y;
    };
    std::map<int, Start> m_down; // fingers on the screen, where they landed
    std::int64_t m_start = 0;
    int m_most = 0;
    bool m_spoiled = false;
};
