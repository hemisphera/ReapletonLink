#include "LinkController.h"
#include "reaper_plugin.h"
#include "ReaperAPI.h"

/**
 * Control surface that drives the Ableton Link master sync loop.
 *
 * Registered via rec->Register("csurf_inst", ...) so that REAPER calls Run()
 * approximately 30 times per second on the main thread.
 */
class LinkControlSurface : public IReaperControlSurface
{
public:
    explicit LinkControlSurface(const ReaperAPI &api)
        : m_controller(api)
    {
    }

    const char *GetTypeString() override { return "ReapletonLink"; }
    const char *GetDescString() override { return "ReapletonLink (Headless)"; }
    const char *GetConfigString() override { return ""; }

    void Run() override
    {
        m_controller.update();
    }

private:
    LinkController m_controller;
};

static ReaperAPI g_api;
static LinkControlSurface *g_surface = nullptr;

extern "C" REAPER_PLUGIN_DLL_EXPORT
int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t *rec)
{
    if (!rec)
    {
        // REAPER is unloading — clean up.
        delete g_surface;
        g_surface = nullptr;
        return 0;
    }

    if (rec->caller_version != REAPER_PLUGIN_VERSION)
        return 0;

    // Resolve the REAPER API functions we need.
    if (!g_api.resolve(rec->GetFunc))
        return 0;

    // Create the control surface (which instantiates the Link node).
    try
    {
        g_surface = new LinkControlSurface(g_api);
    }
    catch (...)
    {
        g_surface = nullptr;
        return 0;
    }

    // Register as a background control surface — no GUI, no dock, no menus.
    rec->Register("csurf_inst", g_surface);

    return 1;
}