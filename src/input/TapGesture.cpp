#include "input/TapGesture.h"

#include <algorithm>
#include <cmath>

void TapGesture::down(int id, double x, double y, std::int64_t time)
{
    if (m_down.empty()) {
        m_start = time;
        m_most = 0;
        m_spoiled = false;
    }
    m_down[id] = {x, y};
    m_most = std::max(m_most, static_cast<int>(m_down.size()));
}

void TapGesture::move(int id, double x, double y)
{
    const auto it = m_down.find(id);
    if (it != m_down.end() && std::hypot(x - it->second.x, y - it->second.y) > kMaxMovePx)
        m_spoiled = true; // a pan or a pinch
}

int TapGesture::up(int id, std::int64_t time)
{
    if (!m_down.erase(id) || !m_down.empty())
        return 0;
    const bool tap = !m_spoiled && time - m_start <= kMaxMs && m_most >= 2;
    return tap ? m_most : 0;
}

void TapGesture::cancel()
{
    m_spoiled = true;
    m_down.clear();
}
