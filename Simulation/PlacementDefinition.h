#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>

enum class PlacementKind
{
    Building,
    Road
};

struct PlacementDefinition
{
    std::string id;
    std::string displayName;
    PlacementKind kind = PlacementKind::Building;
    std::string modelPath;
    std::string texturePath;
    DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };
    float heightOffset = 0.0f;
    DirectX::XMFLOAT4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 previewTint = { 0.25f, 0.75f, 0.95f, 1.0f };
};

class PlacementCatalog
{
public:
    PlacementCatalog()
    {
        m_definitions =
        {
            { "building.house", "House", PlacementKind::Building,
              "Models/cube.obj", "", { 0.72f, 1.50f, 0.72f }, 0.75f,
              { 0.72f, 0.52f, 0.28f, 1.0f }, { 0.25f, 0.75f, 0.95f, 1.0f } },
            { "building.shop", "Shop", PlacementKind::Building,
              "Models/cube.obj", "", { 0.88f, 1.05f, 0.88f }, 0.525f,
              { 0.35f, 0.58f, 0.82f, 1.0f }, { 0.45f, 0.82f, 1.0f, 1.0f } },
            { "road.asphalt", "Asphalt", PlacementKind::Road,
              "Models/cube.obj", "", { 0.88f, 0.06f, 0.88f }, 0.02f,
              { 0.32f, 0.33f, 0.36f, 1.0f }, { 0.55f, 0.75f, 0.95f, 1.0f } },
            { "road.stone", "Stone", PlacementKind::Road,
              "Models/cube.obj", "", { 0.88f, 0.07f, 0.88f }, 0.025f,
              { 0.52f, 0.50f, 0.46f, 1.0f }, { 0.65f, 0.82f, 0.98f, 1.0f } }
        };
    }

    const std::vector<PlacementDefinition>& GetAll() const { return m_definitions; }
    const PlacementDefinition& Get(int index) const { return m_definitions.at(index); }

    std::vector<int> GetIndices(PlacementKind kind) const
    {
        std::vector<int> result;
        for (int i = 0; i < static_cast<int>(m_definitions.size()); ++i)
        {
            if (m_definitions[i].kind == kind)
            {
                result.push_back(i);
            }
        }
        return result;
    }

private:
    std::vector<PlacementDefinition> m_definitions;
};
