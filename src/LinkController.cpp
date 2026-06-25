#include "LinkController.h"

LinkController::LinkController(const ReaperAPI &api)
    : m_api(api)
    , m_link(120.0)
{
    // Start disabled by default. The user enables the Link connection
    // explicitly via the registered REAPER actions.
    m_link.enable(false);
    m_link.enableStartStopSync(false);
}

LinkController::~LinkController()
{
    m_link.enable(false);
}

void LinkController::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;

    m_enabled = enabled;
    m_link.enable(enabled);
    m_link.enableStartStopSync(enabled);

    if (!enabled)
    {
        // Reset tracking state so a re-enable re-anchors cleanly.
        m_wasPlaying = false;
        m_lastBeats = -1.0;
    }
}

bool LinkController::isEnabled() const
{
    return m_enabled;
}

void LinkController::update()
{
    // Do nothing while the Link connection is disabled.
    if (!m_enabled)
        return;

    // 1. Fetch live metadata from REAPER engine.
    int playState = m_api.GetPlayState();
    bool isPlaying = (playState & 1) || (playState & 4);

    double positionSec = isPlaying ? m_api.GetPlayPosition() : m_api.GetCursorPosition();
    double beats = m_api.TimeMap2_timeToQN(nullptr, positionSec);
    double bpm = m_api.TimeMap2_GetDividedBpmAtTime(nullptr, positionSec);

    // Normalise negative beat positions (e.g. pre-roll / negative project start).
    beats = normalizeBeats(beats);

    // 2. Capture current Link session state.
    auto sessionState = m_link.captureAppSessionState();
    const auto hostTime = m_link.clock().micros();
    bool changed = false;
    bool playStateChanged = (isPlaying != m_wasPlaying);

    // 3. Force-sync tempo.
    if (std::abs(sessionState.tempo() - bpm) > 0.001)
    {
        sessionState.setTempo(bpm, hostTime);
        changed = true;
    }

    // 4. Force-sync transport play/stop state.
    if (playStateChanged)
    {
        sessionState.setIsPlaying(isPlaying, hostTime);
        changed = true;
    }

    // 5. Hard phase injection — only when the position has actually changed
    //    or drifted, so we don't flood the network on every tick.
    //    On play/stop transitions: always re-anchor the beat.
    //    While playing: only if Link's beat has drifted from REAPER's beat.
    //    While stopped: only if REAPER's cursor moved (seeking).
    bool shouldForce = playStateChanged;
    if (!shouldForce)
    {
        if (isPlaying)
        {
            double linkBeat = sessionState.beatAtTime(hostTime, m_quantum);
            shouldForce = std::abs(linkBeat - beats) > 0.05;
        }
        else
        {
            shouldForce = std::abs(beats - m_lastBeats) > 0.001;
        }
    }

    if (shouldForce)
    {
        sessionState.forceBeatAtTime(beats, hostTime, m_quantum);
        changed = true;
    }

    // 6. Commit mutation back to the Link engine (skipped if nothing changed).
    if (changed)
        m_link.commitAppSessionState(sessionState);

    m_lastBeats = beats;
    m_wasPlaying = isPlaying;
}

double LinkController::normalizeBeats(double beats) const
{
    if (beats >= 0.0)
        return beats;

    // Shift by enough quantum multiples to bring the beat into positive territory.
    double numQuantums = std::ceil(-beats / m_quantum);
    return beats + numQuantums * m_quantum;
}