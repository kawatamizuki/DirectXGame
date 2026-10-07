#include "RoadNode.h"

RoadNodeType RoadNode::GetType() const
{
    switch (connectedSegmentIndices.size())
    {
    case 0:
    case 1:
        return RoadNodeType::DeadEnd;
    case 2:
        return RoadNodeType::Through;
    case 3:
        return RoadNodeType::TJunction;
    default:
        return RoadNodeType::Crossroad;
    }
}
