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

    /** Forward Link enable/disable to the controller. */
    void setLinkEnabled(bool enabled) { m_controller.setEnabled(enabled); }
    bool isLinkEnabled() const { return m_controller.isEnabled(); }

private:
    LinkController m_controller;
};

static ReaperAPI g_api;
static LinkControlSurface *g_surface = nullptr;

// Command IDs for the registered REAPER actions (assigned at load time).
static int g_cmdEnableLink = 0;
static int g_cmdDisableLink = 0;
static int g_cmdToggleLink = 0;

/**
 * hookcommand callback — runs before every main-section action.
 * Returns true if we handled the command (preventing the action from running).
 */
static bool onCommand(int command, int flag)
{
    (void)flag;

    if (command == g_cmdEnableLink && g_surface)
    {
        g_surface->setLinkEnabled(true);
        g_api.RefreshToolbar(g_cmdToggleLink);
        return true;
    }
    if (command == g_cmdDisableLink && g_surface)
    {
        g_surface->setLinkEnabled(false);
        g_api.RefreshToolbar(g_cmdToggleLink);
        return true;
    }
    if (command == g_cmdToggleLink && g_surface)
    {
        g_surface->setLinkEnabled(!g_surface->isLinkEnabled());
        g_api.RefreshToolbar(g_cmdToggleLink);
        return true;
    }
    return false;
}

/**
 * toggleaction callback — lets REAPER show on/off state (checkmark in the
 * Actions list, pressed toolbar button) for the Toggle Link connection action.
 * Returns: -1 = not our action, 0 = off, 1 = on.
 */
static int onToggleAction(int command)
{
    if (command == g_cmdToggleLink)
        return g_surface && g_surface->isLinkEnabled() ? 1 : 0;
    return -1;
}

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

    // Register the Enable/Disable Link connection actions.
    int cmdEnable = rec->Register("command_id", "REAPLETONLINK_ENABLE_LINK");
    g_cmdEnableLink = cmdEnable;
    static gaccel_register_t gaccelEnable;
    gaccelEnable.accel.fVirt = 0;
    gaccelEnable.accel.key = 0;
    gaccelEnable.accel.cmd = static_cast<unsigned short>(cmdEnable);
    gaccelEnable.desc = "ReapletonLink: Enable Link connection";
    rec->Register("gaccel", &gaccelEnable);

    int cmdDisable = rec->Register("command_id", "REAPLETONLINK_DISABLE_LINK");
    g_cmdDisableLink = cmdDisable;
    static gaccel_register_t gaccelDisable;
    gaccelDisable.accel.fVirt = 0;
    gaccelDisable.accel.key = 0;
    gaccelDisable.accel.cmd = static_cast<unsigned short>(cmdDisable);
    gaccelDisable.desc = "ReapletonLink: Disable Link connection";
    rec->Register("gaccel", &gaccelDisable);

    int cmdToggle = rec->Register("command_id", "REAPLETONLINK_TOGGLE_LINK");
    g_cmdToggleLink = cmdToggle;
    static gaccel_register_t gaccelToggle;
    gaccelToggle.accel.fVirt = 0;
    gaccelToggle.accel.key = 0;
    gaccelToggle.accel.cmd = static_cast<unsigned short>(cmdToggle);
    gaccelToggle.desc = "ReapletonLink: Toggle Link connection";
    rec->Register("gaccel", &gaccelToggle);

    // Dispatch our commands to the control surface.
    rec->Register("hookcommand", (void *)onCommand);
    // Provide on/off state for the toggle action (toolbar button / Actions list).
    rec->Register("toggleaction", (void *)onToggleAction);

    return 1;
}