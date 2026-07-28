#include "verticalshaft.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"

namespace MWAccessibility
{
    namespace
    {
        bool containsCI(std::string_view haystack, std::string_view needle)
        {
            if (needle.size() > haystack.size())
                return false;
            const auto lower = [](char c) {
                return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            };
            for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
            {
                std::size_t j = 0;
                for (; j < needle.size(); ++j)
                {
                    if (lower(haystack[i + j]) != lower(needle[j]))
                        break;
                }
                if (j == needle.size())
                    return true;
            }
            return false;
        }
    }

    ShaftPieceKind classifyShaftPiece(std::string_view refId)
    {
        // The Telvanni interior kit. "hallshaft" covers the pieces that join a
        // shaft to a horizontal hall; "shaft" the column itself.
        const bool isShaft = containsCI(refId, "_shaft") || containsCI(refId, "shaft_");
        if (!isShaft)
            return ShaftPieceKind::NotShaft;

        // A cap closes the shaft off -- it is the top (or bottom) surface, not a
        // way through. Checked first: "hallshaft_cap" is both a hallshaft and a
        // cap, and the cap-ness is what matters.
        if (containsCI(refId, "cap"))
            return ShaftPieceKind::Cap;

        // Junction pieces are where a floor opens into the shaft: the 6-way is the
        // classic Telvanni floor opening, and "vconnect" joins two vertical
        // sections through a floor. These are the heights you can enter/leave at.
        if (containsCI(refId, "6way") || containsCI(refId, "vconnect") || containsCI(refId, "hallshaft"))
            return ShaftPieceKind::Opening;

        return ShaftPieceKind::Segment;
    }

    std::vector<VerticalShaft> detectShafts(
        const std::vector<ShaftPiece>& pieces, float axisTolerance, int minPieces)
    {
        struct Group
        {
            float mSumX = 0.f;
            float mSumY = 0.f;
            int mCount = 0;
            float mBottom = std::numeric_limits<float>::max();
            float mTop = -std::numeric_limits<float>::max();
            std::vector<float> mOpenings;
        };

        std::vector<Group> groups;

        for (const ShaftPiece& piece : pieces)
        {
            const ShaftPieceKind kind = classifyShaftPiece(piece.mRefId);
            if (kind == ShaftPieceKind::NotShaft)
                continue;

            // Find an existing column within tolerance, comparing against the
            // group's running centroid so a slightly scattered stack still
            // coalesces instead of chaining off in one direction.
            Group* found = nullptr;
            for (Group& g : groups)
            {
                const float cx = g.mSumX / static_cast<float>(g.mCount);
                const float cy = g.mSumY / static_cast<float>(g.mCount);
                const float dx = cx - piece.mPos.x();
                const float dy = cy - piece.mPos.y();
                if (std::sqrt(dx * dx + dy * dy) <= axisTolerance)
                {
                    found = &g;
                    break;
                }
            }
            if (!found)
            {
                groups.emplace_back();
                found = &groups.back();
            }

            found->mSumX += piece.mPos.x();
            found->mSumY += piece.mPos.y();
            ++found->mCount;
            found->mBottom = std::min(found->mBottom, piece.mPos.z());
            found->mTop = std::max(found->mTop, piece.mPos.z());
            // A cap is not a way through, so it bounds the shaft without being an
            // opening.
            if (kind == ShaftPieceKind::Opening)
                found->mOpenings.push_back(piece.mPos.z());
        }

        std::vector<VerticalShaft> out;
        for (const Group& g : groups)
        {
            if (g.mCount < minPieces)
                continue;
            VerticalShaft shaft;
            shaft.mX = g.mSumX / static_cast<float>(g.mCount);
            shaft.mY = g.mSumY / static_cast<float>(g.mCount);
            shaft.mBottom = g.mBottom;
            shaft.mTop = g.mTop;
            shaft.mPieceCount = g.mCount;
            shaft.mOpenings = g.mOpenings;
            std::sort(shaft.mOpenings.begin(), shaft.mOpenings.end());
            // Several kit pieces can sit at one height (a 6way and a vconnect
            // share a floor); collapse those so "openings" reads as floors.
            shaft.mOpenings.erase(std::unique(shaft.mOpenings.begin(), shaft.mOpenings.end(),
                                      [](float a, float b) { return std::abs(a - b) < 32.f; }),
                shaft.mOpenings.end());
            out.push_back(std::move(shaft));
        }

        std::sort(out.begin(), out.end(),
            [](const VerticalShaft& a, const VerticalShaft& b) { return a.mPieceCount > b.mPieceCount; });
        return out;
    }

    const VerticalShaft* bestShaftForTravel(const std::vector<VerticalShaft>& shafts, float fromX, float fromY,
        float fromZ, float toZ, float slack)
    {
        const float lo = std::min(fromZ, toZ);
        const float hi = std::max(fromZ, toZ);

        const VerticalShaft* best = nullptr;
        float bestDist = std::numeric_limits<float>::max();
        for (const VerticalShaft& s : shafts)
        {
            // The shaft must plausibly cover the whole journey; slack allows for
            // flying a little above the topmost piece or below the lowest.
            if (s.mBottom - slack > lo || s.mTop + slack < hi)
                continue;
            const float dx = s.mX - fromX;
            const float dy = s.mY - fromY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = &s;
            }
        }
        return best;
    }

    std::vector<VerticalShaft> collectCellShafts(const MWWorld::Ptr& player)
    {
        MWWorld::CellStore* cell = player.getCell();
        if (!cell)
            return {};

        // Keep the id strings alive for the whole call: ShaftPiece::mRefId is a
        // view, and the views are bound only after `ids` has stopped growing so a
        // reallocation can't leave them dangling.
        std::vector<std::string> ids;
        std::vector<ShaftPiece> pieces;
        ids.reserve(64);
        pieces.reserve(64);
        cell->forEachConst([&](const MWWorld::ConstPtr& ptr) {
            if (ptr.getCellRef().getCount() <= 0)
                return true;
            // toString(), NOT getRefIdString(): the latter THROWS for any RefId
            // that isn't string-backed (generated/index refs do occur among a
            // cell's refs), which would take the game down.
            std::string id = ptr.getCellRef().getRefId().toString();
            if (classifyShaftPiece(id) == ShaftPieceKind::NotShaft)
                return true;
            ids.push_back(std::move(id));
            pieces.push_back(ShaftPiece{ std::string_view(), ptr.getRefData().getPosition().asVec3() });
            return true;
        });
        for (std::size_t i = 0; i < pieces.size(); ++i)
            pieces[i].mRefId = ids[i];

        return detectShafts(pieces);
    }

    float nearestOpening(const VerticalShaft& shaft, float z)
    {
        if (shaft.mOpenings.empty())
            return z;
        float best = shaft.mOpenings.front();
        float bestDist = std::abs(best - z);
        for (const float o : shaft.mOpenings)
        {
            const float d = std::abs(o - z);
            if (d < bestDist)
            {
                bestDist = d;
                best = o;
            }
        }
        return best;
    }
}
