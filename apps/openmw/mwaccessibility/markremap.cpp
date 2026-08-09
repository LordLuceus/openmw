#include "markremap.hpp"

#include <components/misc/strings/algorithm.hpp>

namespace MWAccessibility
{
    int32_t findContentFileIndex(std::string_view name, const std::vector<std::string>& contentFiles)
    {
        for (size_t i = 0; i < contentFiles.size(); ++i)
        {
            if (Misc::StringUtils::ciEqual(contentFiles[i], name))
                return static_cast<int32_t>(i);
        }
        return -1;
    }

    RemapResult remapContentFileIndex(int32_t storedIndex, const std::map<int32_t, std::string>& savedPlugins,
        const std::vector<std::string>& contentFiles, int32_t& outIndex)
    {
        outIndex = storedIndex;

        const auto it = savedPlugins.find(storedIndex);
        if (it == savedPlugins.end())
        {
            // No recorded name for this index: either a sidecar written before
            // the manifest existed, or a mark with no content file at all. We
            // cannot verify it, so leave it exactly as it was.
            return RemapResult::Unchanged;
        }

        const int32_t nowAt = findContentFileIndex(it->second, contentFiles);
        if (nowAt < 0)
            return RemapResult::Orphaned;

        if (nowAt == storedIndex)
            return RemapResult::Unchanged;

        outIndex = nowAt;
        return RemapResult::Remapped;
    }
}
