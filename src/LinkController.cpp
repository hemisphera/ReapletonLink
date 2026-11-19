#include "LinkController.h"

#include <cmath>

namespace
{
// Sync-thread cadence. Independent of REAPER's main-thread/UI load, so
// corrections stay smooth even while REAPER repaints heavily. Faster than the
// old ~30 Hz main-thread tick because each tick is now cheap.
constexpr auto kSyncInterval = std::chrono::milliseconds(15);

// Phase re-anchor thresholds (in beats).
constexpr double kDriftThreshold = 0.05; // 0.05 beats @ 120 bpm = 25 ms
constexpr double kSeekThreshold = 0.001; // cursor moved while stopped

// Tempo re-anchor threshold (in bpm).
constexpr double kTempoThreshold = 0.001;

// Window during which a forwarded remote start/stop waits for REAPER's
// transport to follow before REAPER's state is re-asserted on the session.
constexpr auto kPendingRemoteTimeout = std::chrono::milliseconds(500);
} // namespace

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
    stopWorker();
    m_link.enable(false);
}

void LinkController::setEnabled(bool enabled)
{
    if (enabled == m_enabled.load(std::memory_order_relaxed))
        return;

    m_enabled.store(enabled, std::memory_order_relaxed);

    // Sync thread first, so it never runs against a Link connection in the
    // wrong state, and so tracking state can be reset safely.
    if (!enabled)
        stopWorker();

    m_link.enable(enabled);
    m_link.enableStartStopSync(enabled);

    // Reset tracking state so a (re-)enable re-anchors cleanly.
    resetTracking();

    if (enabled)
        startWorker();
}

bool LinkController::isEnabled() const
{
    return m_enabled.load(std::memory_order_relaxed);
}

void LinkController::setFollowRemoteTransport(bool follow)
{
    m_followRemote.store(follow, std::memory_order_relaxed);
}

bool LinkController::isFollowingRemoteTransport() const
{
    return m_followRemote.load(std::memory_order_relaxed);
}

void LinkController::resetTracking()
{
    m_wasPlaying = false;
    m_lastBeats = -1.0;
    m_lastLinkPlaying = -1;
    m_pendingRemote = false;
}

void LinkController::update()
{
    // Nothing to do while the Link connection is disabled. A transport
    // command queued just before a disable is intentionally dropped.
    if (!m_enabled.load(std::memory_order_relaxed))
        return;

    // Execute transport requests made by Link peers. OnPlayButton/OnStopButton
    // are main-thread-only, so the sync thread queues them for us.
    processTransportQueue();

    // 1. Fetch live metadata from REAPER engine.
    int playState = m_api.GetPlayState();
    bool isRecording = (playState & 4) != 0;
    bool isPlaying = (playState & 1) || isRecording;

    double positionSec = isPlaying ? m_api.GetPlayPosition() : m_api.GetCursorPosition();
    double beats = m_api.TimeMap2_timeToQN(nullptr, positionSec);
    double tempo = m_api.TimeMap2_GetDividedBpmAtTime(nullptr, positionSec);

    // Normalise negative beat positions (e.g. pre-roll / negative project start).
    beats = normalizeBeats(beats);

    // Read the Link clock as late as possible so the (clock, beat) pair is as
    // tight as possible — the sync thread extrapolates from this pair.
    const auto hostTime = m_link.clock().micros();

    // 2. Publish the snapshot for the sync thread.
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_snapshot.valid = true;
        m_snapshot.playing = isPlaying;
        m_snapshot.recording = isRecording;
        m_snapshot.beats = beats;
        m_snapshot.tempo = tempo;
        m_snapshot.linkMicros = hostTime;
    }
}

void LinkController::startWorker()
{
    if (m_thread.joinable())
        return;

    resetTracking();
    {
        std::lock_guard<std::mutex> lock(m_wakeMutex);
        m_running = true;
    }
    m_thread = std::thread(&LinkController::syncLoop, this);
}

void LinkController::stopWorker()
{
    {
        std::lock_guard<std::mutex> lock(m_wakeMutex);
        m_running = false;
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void LinkController::syncLoop()
{
    std::unique_lock<std::mutex> lock(m_wakeMutex);
    while (m_running)
    {
        // Steady cadence; a notify only shortens the wait during shutdown.
        m_cv.wait_for(lock, kSyncInterval);
        if (!m_running)
            break;
        lock.unlock();
        syncTick();
        lock.lock();
    }
}

void LinkController::syncTick()
{
    // 1. Take the latest snapshot produced by the main thread.
    Snapshot snap;
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        snap = m_snapshot;
    }
    if (!snap.valid)
        return;

    const auto now = m_link.clock().micros();

    // 2. Extrapolate REAPER's beat position from the (Link clock, beat) clock
    //    pair, so tempo/phase corrections keep flowing at a steady rate even
    //    when REAPER's main thread (the sampler) lags under UI load.
    //    While playing, REAPER's beat advances at its current effective tempo,
    //    which is exactly what the snapshot carries; when stopped, the cursor
    //    position is used verbatim.
    double beats = snap.beats;
    double tempo = snap.tempo;
    if (snap.playing && tempo > 0.0)
        beats += static_cast<double>((now - snap.linkMicros).count()) / 1e6 * (tempo / 60.0);

    // 3. Capture current Link session state.
    auto sessionState = m_link.captureAppSessionState();
    bool changed = false;
    bool playStateChanged = (snap.playing != m_wasPlaying);
    bool linkPlaying = sessionState.isPlaying();

    // 4. Detect a start/stop issued by a Link peer: the session's play state
    //    changed since our last tick and REAPER's own transport did not.
    //    If REAPER changed too, REAPER wins (it is re-asserted below).
    bool remoteChange = m_lastLinkPlaying >= 0 &&
                        linkPlaying != (m_lastLinkPlaying == 1) &&
                        !playStateChanged;

    // The forwarding window is time-based and expires on its own, even if the
    // main thread stalls and stops delivering snapshots for a while.
    if (m_pendingRemote && std::chrono::steady_clock::now() >= m_pendingRemoteDeadline)
        m_pendingRemote = false;

    if (remoteChange && m_followRemote.load(std::memory_order_relaxed) &&
        linkPlaying != snap.playing)
    {
        bool accepted = false;
        if (linkPlaying)
        {
            // REAPER starts from its edit cursor; once it reports playing,
            // step 7 re-anchors the session beat to REAPER's position.
            queueTransportCommand(TransportCommand::Play);
            accepted = true;
        }
        else if (!snap.recording)
        {
            // Never let a peer stop an active recording.
            queueTransportCommand(TransportCommand::Stop);
            accepted = true;
        }

        if (accepted)
        {
            m_pendingRemote = true;
            m_pendingRemoteDeadline = std::chrono::steady_clock::now() + kPendingRemoteTimeout;
        }
    }

    // While a forwarded request is pending, wait for REAPER's transport to
    // follow (as reflected in a fresh snapshot) instead of overriding the
    // session with REAPER's stale state.
    if (m_pendingRemote && linkPlaying == snap.playing)
        m_pendingRemote = false;

    // 5. Force-sync tempo.
    if (std::abs(sessionState.tempo() - tempo) > kTempoThreshold)
    {
        sessionState.setTempo(tempo, now);
        changed = true;
    }

    // 6. Force-sync transport play/stop state. This propagates REAPER's own
    //    start/stop and reverts remote requests that were ignored or rejected
    //    (follow disabled, or a stop while recording).
    bool finalLinkPlaying = linkPlaying;
    if (!m_pendingRemote && linkPlaying != snap.playing)
    {
        sessionState.setIsPlaying(snap.playing, now);
        finalLinkPlaying = snap.playing;
        changed = true;
    }

    // 7. Hard phase injection — only when the position has actually changed
    //    or drifted, so we don't flood the network on every tick.
    //    On play/stop transitions: always re-anchor the beat.
    //    While playing: only if Link's beat has drifted from REAPER's beat.
    //    While stopped: only if REAPER's cursor moved (seeking).
    bool shouldForce = playStateChanged;
    if (!shouldForce)
    {
        if (snap.playing)
        {
            double linkBeat = sessionState.beatAtTime(now, m_quantum);
            shouldForce = std::abs(linkBeat - beats) > kDriftThreshold;
        }
        else
        {
            shouldForce = std::abs(beats - m_lastBeats) > kSeekThreshold;
        }
    }

    if (shouldForce)
    {
        sessionState.forceBeatAtTime(beats, now, m_quantum);
        changed = true;
    }

    // 8. Commit mutation back to the Link engine (skipped if nothing changed).
    if (changed)
        m_link.commitAppSessionState(sessionState);

    m_lastBeats = beats;
    m_wasPlaying = snap.playing;
    m_lastLinkPlaying = finalLinkPlaying ? 1 : 0;
}

void LinkController::queueTransportCommand(TransportCommand command)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queuedCommand = command;
}

void LinkController::processTransportQueue()
{
    TransportCommand command;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        command = m_queuedCommand;
        m_queuedCommand = TransportCommand::None;
    }

    switch (command)
    {
    case TransportCommand::Play:
        m_api.OnPlayButton();
        break;
    case TransportCommand::Stop:
        m_api.OnStopButton();
        break;
    case TransportCommand::None:
        break;
    }
}

double LinkController::normalizeBeats(double beats) const
{
    if (beats >= 0.0)
        return beats;

    // Shift by enough quantum multiples to bring the beat into positive territory.
    double numQuantums = std::ceil(-beats / m_quantum);
    return beats + numQuantums * m_quantum;
}
