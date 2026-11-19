#pragma once

#include "ableton/Link.hpp"
#include "ReaperAPI.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

/**
 * Wraps the Ableton Link C++ SDK and implements the master sync loop.
 *
 * REAPER is the absolute authority on tempo and timeline position.
 *
 * Threading model (keeps phase corrections smooth under REAPER UI load):
 * - The REAPER main thread (control surface Run(), ~30 Hz) does two things:
 *   it *samples* REAPER's transport state into a snapshot (see Snapshot) and
 *   executes transport start/stop requested by Link peers. REAPER API
 *   functions are not thread-safe, so nothing else touches them here.
 * - A dedicated sync thread (started while Link is enabled) extrapolates
 *   REAPER's beat position between snapshots using the (Link clock, beat)
 *   clock pair and holds the Link session in sync at a steady cadence,
 *   independent of REAPER's main-thread load. All ableton::Link calls are
 *   thread-safe by design.
 *
 * Transport (start/stop) is REAPER-authoritative as well, unless "follow
 * remote transport" is enabled: then a start/stop issued by a Link peer is
 * queued for the main thread to forward to REAPER's transport, and REAPER's
 * timeline position is re-injected into the session once REAPER actually
 * starts.
 */
class LinkController
{
public:
    explicit LinkController(const ReaperAPI &api);
    ~LinkController();

    LinkController(const LinkController &) = delete;
    LinkController &operator=(const LinkController &) = delete;

    /**
     * Called from the control surface Run() method (~30 Hz, REAPER main
     * thread). Samples REAPER state into the snapshot and drains the
     * transport command queue. Cheap by design.
     */
    void update();

    /** Enable or disable the Link network connection. Disabled by default. */
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /** Let Link peers start/stop REAPER's transport. Disabled by default. */
    void setFollowRemoteTransport(bool follow);
    bool isFollowingRemoteTransport() const;

private:
    /**
     * REAPER state sampled by the main thread and consumed by the sync
     * thread. `beats` is REAPER's (normalised) beat position at Link clock
     * time `linkMicros`, so the sync thread can extrapolate between samples.
     */
    struct Snapshot
    {
        bool valid = false;
        bool playing = false;
        bool recording = false;
        double beats = 0.0;                    // REAPER beat position at linkMicros
        double tempo = 120.0;                  // effective bpm at the playhead
        std::chrono::microseconds linkMicros;  // Link clock time paired with `beats`
    };

    /** Transport requests the sync thread queues for the main thread. */
    enum class TransportCommand
    {
        None,
        Play,
        Stop
    };

    // -- sync thread --------------------------------------------------------
    void syncLoop();
    void syncTick();
    void startWorker();
    void stopWorker();

    // -- main thread --------------------------------------------------------
    void processTransportQueue();
    void queueTransportCommand(TransportCommand command);

    // Shared by both threads; only called while the worker is stopped.
    void resetTracking();
    /** Normalise negative beat positions into the positive cyclical domain. */
    double normalizeBeats(double beats) const;

    const ReaperAPI &m_api;
    ableton::Link m_link;
    const double m_quantum = 4.0;

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_followRemote{false};

    // Main thread -> sync thread
    std::mutex m_snapshotMutex;
    Snapshot m_snapshot;

    // Sync thread -> main thread (transport requests)
    std::mutex m_queueMutex;
    TransportCommand m_queuedCommand = TransportCommand::None;

    // Sync thread machinery
    std::thread m_thread;
    std::condition_variable m_cv;
    std::mutex m_wakeMutex;
    bool m_running = false; // guarded by m_wakeMutex

    // Sync-thread-only tracking state (valid while the worker runs)
    bool m_wasPlaying = false;
    double m_lastBeats = -1.0;

    /** Link's isPlaying as of the end of the previous tick (-1 = unknown). */
    int m_lastLinkPlaying = -1;

    /**
     * A remote start/stop was forwarded to REAPER and we are waiting for
     * REAPER's play state to follow. While set, REAPER's transport state is
     * not re-asserted onto the session. Time-based, so main-thread stalls do
     * not distort the window.
     */
    bool m_pendingRemote = false;
    std::chrono::steady_clock::time_point m_pendingRemoteDeadline;
};
