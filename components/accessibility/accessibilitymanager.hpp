#ifndef OPENMW_COMPONENTS_ACCESSIBILITY_ACCESSIBILITYMANAGER_H
#define OPENMW_COMPONENTS_ACCESSIBILITY_ACCESSIBILITYMANAGER_H

#include <string>
#include <string_view>

// Forward declarations of opaque Prism types so we don't pull <prism.h>
// into every translation unit that wants to talk to the manager.
struct PrismContext;
struct PrismBackend;

namespace Accessibility
{
    /// Singleton-style facade around the Prism screen-reader / TTS library.
    ///
    /// Lifetime is owned by the OpenMW engine: call init() once during engine
    /// startup (before any UI exists) and shutdown() during engine teardown.
    /// All other calls are no-ops when the manager is not initialised, so
    /// callers can speak freely without null checks.
    class AccessibilityManager
    {
    public:
        /// Returns the process-wide manager. Always safe to call.
        static AccessibilityManager& instance();

        /// Initialise Prism and pick the best available backend.
        /// Returns true on success. Failures are logged but never throw.
        /// Repeated calls are no-ops.
        bool init();

        /// Release the backend and Prism context. Safe to call when not initialised.
        void shutdown();

        bool isInitialised() const { return mBackend != nullptr; }

        /// Speak \p text via the active backend.
        /// \p interrupt: when true, cancel any in-progress speech first.
        /// No-op (returns false) when no backend is active.
        bool speak(std::string_view text, bool interrupt = true);

        /// Stop any in-progress speech. Safe to call when not initialised.
        void stop();

        /// Human-readable name of the chosen backend, or "<none>".
        const char* backendName() const;

    private:
        AccessibilityManager() = default;
        ~AccessibilityManager();
        AccessibilityManager(const AccessibilityManager&) = delete;
        AccessibilityManager& operator=(const AccessibilityManager&) = delete;

        PrismContext* mContext = nullptr;
        PrismBackend* mBackend = nullptr;
    };
}

#endif
