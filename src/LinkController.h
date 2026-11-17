#pragma once

#include "ableton/Link.hpp"
#include "ReaperAPI.h"

#include <cmath>
#include <string>

/**
 * Wraps the Ableton Link C++ SDK and implements the master sync loop.
 *
 * REAPER is the absolute authority on tempo and timeline position.
 * On every control-surface tick, the controller captures the Link session state,
 * forces it to match REAPER's current state, and commits the mutation back.
 *
 * Transport (start/stop) is REAPER-authoritative as well, unless "follow remote
 * transport" is enabled: then a start/stop issued by a Link peer is forwarded to
 * REAPER's transport, and REAPER's timeline position is re-injected into the
 * session once REAPER actually starts.
 */
class LinkController
{
public:
    explicit LinkController(const ReaperAPI &api);
    ~LinkController();

    LinkController(const LinkController &) = delete;
    LinkController &operator=(const LinkController &) = delete;

    /** Called from the control surface Run() method (~30 Hz). */
    void update();

    /** Enable or disable the Link network connection. Disabled by default. */
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /** Let Link peers start/stop REAPER's transport. Disabled by default. */
    void setFollowRemoteTransport(bool follow);
    bool isFollowingRemoteTransport() const;

private:
    const ReaperAPI &m_api;
    ableton::Link m_link;
    const double m_quantum = 4.0;

    bool m_enabled = false;
    bool m_followRemote = false;
    bool m_wasPlaying = false;
    double m_lastBeats = -1.0;

    /** Link's isPlaying as of the end of the previous tick (-1 = unknown). */
    int m_lastLinkPlaying = -1;

    /**
     * A remote start/stop was forwarded to REAPER and we are waiting for
     * REAPER's play state to follow. While > 0 (ticks remaining), REAPER's
     * transport state is not re-asserted onto the session.
     */
    int m_pendingRemoteTicks = 0;
    static constexpr int kPendingRemoteTimeoutTicks = 15; // ~0.5 s at 30 Hz

    void resetTracking();

    /** Normalise negative beat positions into the positive cyclical domain. */
    double normalizeBeats(double beats) const;
};