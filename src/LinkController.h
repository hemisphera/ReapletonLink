#pragma once

#include "ableton/Link.hpp"
#include "ReaperAPI.h"

#include <cmath>
#include <string>

/**
 * Wraps the Ableton Link C++ SDK and implements the one-way master sync loop.
 *
 * REAPER is the absolute authority on tempo, transport, and timeline position.
 * On every control-surface tick, the controller captures the Link session state,
 * forces it to match REAPER's current state, and commits the mutation back.
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

private:
    const ReaperAPI &m_api;
    ableton::Link m_link;
    const double m_quantum = 4.0;

    bool m_wasPlaying = false;
    double m_lastBeats = -1.0;

    /** Normalise negative beat positions into the positive cyclical domain. */
    double normalizeBeats(double beats) const;
};