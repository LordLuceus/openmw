#ifndef GAME_MWACCESSIBILITY_MARKREMAP_H
#define GAME_MWACCESSIBILITY_MARKREMAP_H

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace MWAccessibility
{
    /// Result of translating a mark's stored content-file index into the current
    /// load order.
    enum class RemapResult
    {
        /// The index needed no change (or could not be checked, for a sidecar
        /// written before plugin names were recorded). Use it as-is.
        Unchanged,
        /// The plugin moved; use the returned index instead.
        Remapped,
        /// The plugin is no longer loaded, so the marked object does not exist
        /// in this game. The mark must be DROPPED, never kept -- keeping it
        /// would point at whichever unrelated mod now occupies that slot.
        Orphaned,
    };

    /// An object mark is stored as (index, contentFile), where contentFile is a
    /// position in the load order that was active when the mark was made. That
    /// position is not stable: adding or removing ANY mod ahead of a plugin
    /// shifts every later index, which silently repoints marks at the wrong
    /// plugin and makes them look deleted.
    ///
    /// Translate one stored index by looking up the plugin NAME it referred to
    /// and finding where that same name sits now. `savedPlugins` maps
    /// stored-index -> plugin name, as recorded beside the marks; `contentFiles`
    /// is the current load order. Comparison is case-insensitive because plugin
    /// names reach us from config files and vary in case.
    ///
    /// A stored index that is absent from `savedPlugins` is left Unchanged: that
    /// is the pre-manifest sidecar case, where we have no name to match on and
    /// guessing would be worse than doing nothing.
    RemapResult remapContentFileIndex(int32_t storedIndex, const std::map<int32_t, std::string>& savedPlugins,
        const std::vector<std::string>& contentFiles, int32_t& outIndex);

    /// Find `name` in `contentFiles`, case-insensitively. Returns -1 if absent.
    int32_t findContentFileIndex(std::string_view name, const std::vector<std::string>& contentFiles);
}

#endif
