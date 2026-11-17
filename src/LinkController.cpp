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

    // Reset tracking state so a (re-)enable re-anchors cleanly.
    resetTracking();
}

bool LinkController::isEnabled() const
{
    return m_enabled;
}

void LinkController::setFollowRemoteTransport(bool follow)
{
    m_followRemote = follow;
    m_pendingRemoteTicks = 0;
}

bool LinkController::isFollowingRemoteTransport() const
{
    return m_followRemote;
}

void LinkController::resetTracking()
{
    m_wasPlaying = false;
    m_lastBeats = -1.0;
    m_lastLinkPlaying = -1;
    m_pendingRemoteTicks = 0;
}

void LinkController::update()
{
    // Do nothing while the Link connection is disabled.
    if (!m_enabled)
        return;

    // 1. Fetch live metadata from REAPER engine.
    int playState = m_api.GetPlayState();
    bool isRecording = (playState & 4) != 0;
    bool isPlaying = (playState & 1) || isRecording;

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
    bool linkPlaying = sessionState.isPlaying();

    // 3. Detect a start/stop issued by a Link peer: the session's play state
    //    changed since our last tick and REAPER's own transport did not.
    //    If REAPER changed too, REAPER wins (it is re-asserted below).
    bool remoteChange = m_lastLinkPlaying >= 0 &&
                        linkPlaying != (m_lastLinkPlaying == 1) &&
                        !playStateChanged;

    if (remoteChange && m_followRemote && linkPlaying != isPlaying)
    {
        bool accepted = false;
        if (linkPlaying)
        {
            // REAPER starts from its edit cursor; once it reports playing,
            // step 6 re-anchors the session beat to REAPER's position.
            m_api.OnPlayButton();
            accepted = true;
        }
        else if (!isRecording)
        {
            // Never let a peer stop an active recording.
            m_api.OnStopButton();
            accepted = true;
        }

        if (accepted)
            m_pendingRemoteTicks = kPendingRemoteTimeoutTicks;
    }

    // While a forwarded request is pending, wait for REAPER's transport to
    // follow instead of overriding the session with REAPER's stale state.
    if (m_pendingRemoteTicks > 0)
    {
        if (isPlaying == linkPlaying)
            m_pendingRemoteTicks = 0;
        else
            --m_pendingRemoteTicks;
    }

    // 4. Force-sync tempo.
    if (std::abs(sessionState.tempo() - bpm) > 0.001)
    {
        sessionState.setTempo(bpm, hostTime);
        changed = true;
    }

    // 5. Force-sync transport play/stop state. This propagates REAPER's own
    //    start/stop and reverts remote requests that were ignored or rejected
    //    (follow disabled, or a stop while recording).
    bool finalLinkPlaying = linkPlaying;
    if (m_pendingRemoteTicks == 0 && linkPlaying != isPlaying)
    {
        sessionState.setIsPlaying(isPlaying, hostTime);
        finalLinkPlaying = isPlaying;
        changed = true;
    }

    // 6. Hard phase injection — only when the position has actually changed
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

    // 7. Commit mutation back to the Link engine (skipped if nothing changed).
    if (changed)
        m_link.commitAppSessionState(sessionState);

    m_lastBeats = beats;
    m_wasPlaying = isPlaying;
    m_lastLinkPlaying = finalLinkPlaying ? 1 : 0;
}

double LinkController::normalizeBeats(double beats) const
{
    if (beats >= 0.0)
        return beats;

    // Shift by enough quantum multiples to bring the beat into positive territory.
    double numQuantums = std::ceil(-beats / m_quantum);
    return beats + numQuantums * m_quantum;
}