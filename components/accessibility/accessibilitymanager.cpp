#include "accessibilitymanager.hpp"

#include <prism.h>

#include <components/debug/debuglog.hpp>

#include <string>

namespace Accessibility
{
    AccessibilityManager& AccessibilityManager::instance()
    {
        static AccessibilityManager sInstance;
        return sInstance;
    }

    AccessibilityManager::~AccessibilityManager()
    {
        shutdown();
    }

    bool AccessibilityManager::init()
    {
        if (mBackend)
            return true;

        PrismConfig cfg = prism_config_init();
        mContext = prism_init(&cfg);
        if (!mContext)
        {
            Log(Debug::Warning) << "Accessibility: prism_init() returned null; "
                                   "screen-reader output disabled.";
            return false;
        }

        mBackend = prism_registry_acquire_best(mContext);
        if (!mBackend)
        {
            Log(Debug::Warning) << "Accessibility: no Prism backend available; "
                                   "screen-reader output disabled.";
            prism_shutdown(mContext);
            mContext = nullptr;
            return false;
        }

        const PrismError err = prism_backend_initialize(mBackend);
        // ALREADY_INITIALIZED is benign for backends that proxy an external
        // process (NVDA, JAWS, etc.) -- the screen reader is already running.
        if (err != PRISM_OK && err != PRISM_ERROR_ALREADY_INITIALIZED)
        {
            Log(Debug::Warning) << "Accessibility: failed to initialise backend '"
                                << (prism_backend_name(mBackend) ? prism_backend_name(mBackend) : "<unknown>")
                                << "': " << prism_error_string(err);
            prism_backend_free(mBackend);
            mBackend = nullptr;
            prism_shutdown(mContext);
            mContext = nullptr;
            return false;
        }

        Log(Debug::Info) << "Accessibility: using TTS backend '" << backendName() << "'.";
        return true;
    }

    void AccessibilityManager::shutdown()
    {
        if (mBackend)
        {
            // Best-effort stop; ignore errors.
            (void)prism_backend_stop(mBackend);
            prism_backend_free(mBackend);
            mBackend = nullptr;
        }
        if (mContext)
        {
            prism_shutdown(mContext);
            mContext = nullptr;
        }
    }

    bool AccessibilityManager::speak(std::string_view text, bool interrupt)
    {
        if (!mBackend || text.empty())
            return false;

        // Prism takes a null-terminated C string; std::string_view is not
        // guaranteed to be null-terminated, so copy into a temp std::string.
        const std::string buf(text);
        const PrismError err = prism_backend_speak(mBackend, buf.c_str(), interrupt);
        if (err != PRISM_OK)
        {
            Log(Debug::Warning) << "Accessibility: speak failed: " << prism_error_string(err);
            return false;
        }
        return true;
    }

    void AccessibilityManager::stop()
    {
        if (mBackend)
            (void)prism_backend_stop(mBackend);
    }

    const char* AccessibilityManager::backendName() const
    {
        if (!mBackend)
            return "<none>";
        const char* name = prism_backend_name(mBackend);
        return name ? name : "<unnamed>";
    }
}
