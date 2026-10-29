#include "SimTest.h"
#include <algorithm>

std::vector<uint32_t> SelectTestTargets(const std::vector<SimTestResidentPoint>& residents, const SimTestCommand& command,
                                        uint32_t selectedResidentId, bool* truncated)
{
    if (truncated)
    {
        *truncated = false;
    }
    std::vector<uint32_t> targets;
    switch (command.target)
    {
    case SimTestTarget::Selected:
        if (selectedResidentId != 0)
        {
            // 住民の一覧にいる時だけ(消えた住民は、対象にしない)。
            for (const SimTestResidentPoint& resident : residents)
            {
                if (resident.residentId == selectedResidentId) { targets.push_back(selectedResidentId); break; }
            }
        }
        break;
    case SimTestTarget::Rect:
    {
        const float minX = (std::min)(command.rectMinX, command.rectMaxX);
        const float maxX = (std::max)(command.rectMinX, command.rectMaxX);
        const float minZ = (std::min)(command.rectMinZ, command.rectMaxZ);
        const float maxZ = (std::max)(command.rectMinZ, command.rectMaxZ);
        for (const SimTestResidentPoint& resident : residents)
        {
            if (resident.x >= minX && resident.x <= maxX && resident.z >= minZ && resident.z <= maxZ)
            {
                targets.push_back(resident.residentId);
            }
        }
        break;
    }
    case SimTestTarget::All:
        for (const SimTestResidentPoint& resident : residents) { targets.push_back(resident.residentId); }
        break;
    }

    const size_t limit = static_cast<size_t>((std::max)(1, (std::min)(command.maxCount, kSimTestMaxBatch)));
    if (targets.size() > limit)
    {
        targets.resize(limit);
        if (truncated)
        {
            *truncated = true;
        }
    }
    return targets;
}
