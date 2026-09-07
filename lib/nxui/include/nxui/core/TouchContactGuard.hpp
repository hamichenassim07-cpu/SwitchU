#pragma once
#include <cstdint>

namespace nxui {
// A cancelled contact cannot become a fresh tap when its second finger lifts.
// It stays quarantined until all physical contacts have left the screen.
class TouchContactGuard {
public:
    bool update(unsigned count, uint32_t id) {
        m_cancelled = false;
        if (m_blocked) {
            m_cancelled = true;
            if (count == 0) m_blocked = false;
            m_tracking = false;
            return false;
        }
        if (count > 1 || (count == 1 && m_tracking && id != m_id)) {
            m_blocked = true;
            m_cancelled = true;
            m_tracking = false;
            return false;
        }
        m_tracking = count == 1;
        if (m_tracking) m_id = id;
        return m_tracking;
    }
    bool cancelled() const { return m_cancelled; }
private:
    uint32_t m_id = 0;
    bool m_tracking = false, m_blocked = false, m_cancelled = false;
};
}
