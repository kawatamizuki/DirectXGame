#include "ScenarioFile.h"
#include <cmath>

using namespace DirectX;

namespace
{
    Json ToJson(const XMFLOAT3& v)
    {
        Json array = Json::MakeArray();
        array.Push(v.x);
        array.Push(v.y);
        array.Push(v.z);
        return array;
    }

    // [x,y,z]を読む。要素数が足りない・数値でない・有限でない場合はfalse。
    bool ReadVec3(const Json& json, XMFLOAT3& out)
    {
        if (!json.IsArray() || json.Size() != 3)
        {
            return false;
        }
        float values[3];
        for (size_t i = 0; i < 3; ++i)
        {
            const Json& element = json.At(i);
            if (!element.IsNumber() || !std::isfinite(element.AsNumber()))
            {
                return false;
            }
            values[i] = element.AsFloat();
        }
        out = XMFLOAT3(values[0], values[1], values[2]);
        return true;
    }

    // objectのkeyの項目を[x,y,z]として読む。項目が無い・不正ならfalse。
    bool ReadVec3Key(const Json& object, const char* key, XMFLOAT3& out)
    {
        const Json* found = object.Find(key);
        return found && ReadVec3(*found, out);
    }

    Json ToJson(const Transform& transform)
    {
        Json object = Json::MakeObject();
        object.Set("position", ToJson(transform.position));
        object.Set("rotation", ToJson(transform.rotation));
        object.Set("scale", ToJson(transform.scale));
        return object;
    }

    // 有限の数値か(NaN/無限大を弾く)。
    bool IsFiniteNumber(const Json* json)
    {
        return json && json->IsNumber() && std::isfinite(json->AsNumber());
    }
}

namespace ScenarioFile
{
    const char* RoadTypeKey(RoadType type)
    {
        switch (type)
        {
        case RoadType::Narrow: return "Narrow";
        case RoadType::Normal: return "Normal";
        case RoadType::Large:  return "Large";
        }
        return "Normal";
    }

    bool RoadTypeFromKey(const std::string& key, RoadType& out)
    {
        if (key == "Narrow") { out = RoadType::Narrow; return true; }
        if (key == "Normal") { out = RoadType::Normal; return true; }
        if (key == "Large")  { out = RoadType::Large;  return true; }
        return false;
    }

    const char* RoadDrawModeKey(RoadDrawMode mode)
    {
        switch (mode)
        {
        case RoadDrawMode::Straight: return "Straight";
        case RoadDrawMode::Curve:    return "Curve";
        }
        return "Straight";
    }

    bool RoadDrawModeFromKey(const std::string& key, RoadDrawMode& out)
    {
        if (key == "Straight") { out = RoadDrawMode::Straight; return true; }
        if (key == "Curve")    { out = RoadDrawMode::Curve;    return true; }
        return false;
    }

    const char* BuildingTypeKey(BuildingType type)
    {
        switch (type)
        {
        case BuildingType::House:  return "House";
        case BuildingType::Office: return "Office";
        case BuildingType::Shop:   return "Shop";
        }
        return "House";
    }

    bool BuildingTypeFromKey(const std::string& key, BuildingType& out)
    {
        if (key == "House")  { out = BuildingType::House;  return true; }
        if (key == "Office") { out = BuildingType::Office; return true; }
        if (key == "Shop")   { out = BuildingType::Shop;   return true; }
        return false;
    }

    Json SnapshotToJson(const WorldSnapshot& snapshot)
    {
        Json root = Json::MakeObject();
        root.Set("version", snapshot.version);
        root.Set("state", snapshot.state);

        Json roads = Json::MakeArray();
        for (const RoadSegment& road : snapshot.roads)
        {
            Json entry = Json::MakeObject();
            entry.Set("start", ToJson(road.start));
            entry.Set("end", ToJson(road.end));
            entry.Set("width", road.width);
            entry.Set("type", RoadTypeKey(road.type));
            entry.Set("drawMode", RoadDrawModeKey(road.drawMode));
            roads.Push(std::move(entry));
        }
        root.Set("roads", std::move(roads));

        Json buildings = Json::MakeArray();
        for (const SnapshotBuilding& building : snapshot.buildings)
        {
            const BuildingPlacement& p = building.placement;

            Json footprint = Json::MakeObject();
            footprint.Set("origin", ToJson(p.footprintRect.origin));
            footprint.Set("yaw", p.footprintRect.yaw);
            footprint.Set("width", p.footprintRect.width);
            footprint.Set("depth", p.footprintRect.depth);

            Json frame = Json::MakeObject();
            frame.Set("origin", ToJson(p.frameOrigin));
            frame.Set("yaw", p.frameYaw);

            Json localCell = Json::MakeArray();
            localCell.Push(p.localCellX);
            localCell.Push(p.localCellZ);
            Json localSize = Json::MakeArray();
            localSize.Push(p.localWidthCells);
            localSize.Push(p.localDepthCells);

            Json entry = Json::MakeObject();
            entry.Set("type", BuildingTypeKey(building.type));
            entry.Set("transform", ToJson(building.transform));
            entry.Set("footprint", std::move(footprint));
            entry.Set("frame", std::move(frame));
            entry.Set("localCell", std::move(localCell));
            entry.Set("localSize", std::move(localSize));
            buildings.Push(std::move(entry));
        }
        root.Set("buildings", std::move(buildings));

        Json cells = Json::MakeArray();
        for (const SnapshotOrientationCell& cell : snapshot.orientationCells)
        {
            Json entry = Json::MakeObject();
            entry.Set("origin", ToJson(cell.origin));
            entry.Set("yaw", cell.yaw);
            Json index = Json::MakeArray();
            index.Push(cell.cellX);
            index.Push(cell.cellZ);
            entry.Set("cell", std::move(index));
            cells.Push(std::move(entry));
        }
        root.Set("orientationCells", std::move(cells));

        return root;
    }

    bool JsonToSnapshot(const Json& root, WorldSnapshot& snapshot, std::vector<std::string>& warnings, std::string& error)
    {
        error.clear();
        if (!root.IsObject())
        {
            error = "セーブデータの一番外側がオブジェクトではありません";
            return false;
        }

        const Json* versionJson = root.Find("version");
        if (!versionJson || !versionJson->IsNumber())
        {
            error = "セーブデータに版番号(version)がありません";
            return false;
        }
        int version = versionJson->AsInt(0);
        if (version < 1)
        {
            error = "セーブデータの版番号が不正です: " + std::to_string(version);
            return false;
        }
        if (version > WorldSnapshot::kCurrentVersion)
        {
            error = "このゲームより新しい版のセーブデータです(版 " + std::to_string(version) +
                    "、このゲームは版 " + std::to_string(WorldSnapshot::kCurrentVersion) + " まで対応)";
            return false;
        }
        // (将来、版1より古い形式を読み替える場合は、ここで version に応じてrootを変換する)

        WorldSnapshot result;
        result.version = version;

        // ---- 道路 ----
        const Json* roads = root.Find("roads");
        if (roads && roads->IsArray())
        {
            for (size_t i = 0; i < roads->Size(); ++i)
            {
                const Json& entry = roads->At(i);
                RoadSegment road;
                if (!entry.IsObject() || !ReadVec3Key(entry, "start", road.start) ||
                    !ReadVec3Key(entry, "end", road.end))
                {
                    error = "roads[" + std::to_string(i) + "] の始点・終点が不正です";
                    return false;
                }

                if (!IsFiniteNumber(entry.Find("width")) || entry.GetFloat("width") <= 0.0f)
                {
                    error = "roads[" + std::to_string(i) + "] の幅(width)が不正です";
                    return false;
                }
                road.width = entry.GetFloat("width");

                // 種類・描き方の名前が未知なら既定値にして続行する(道路の並びを変えないため、
                // 道路を飛ばすことはしない。住民の経路が道路の添字で道路を指しているので)。
                std::string typeKey = entry.GetString("type", "Normal");
                if (!RoadTypeFromKey(typeKey, road.type))
                {
                    warnings.push_back("roads[" + std::to_string(i) + "] の種類 \"" + typeKey + "\" は不明なため Normal にしました");
                    road.type = RoadType::Normal;
                }
                std::string modeKey = entry.GetString("drawMode", "Straight");
                if (!RoadDrawModeFromKey(modeKey, road.drawMode))
                {
                    warnings.push_back("roads[" + std::to_string(i) + "] の描き方 \"" + modeKey + "\" は不明なため Straight にしました");
                    road.drawMode = RoadDrawMode::Straight;
                }
                road.debugLoad = 0.0f;
                result.roads.push_back(road);
            }
        }
        else if (roads)
        {
            warnings.push_back("roads が配列ではないため無視しました");
        }

        // ---- 建物 ----
        const Json* buildings = root.Find("buildings");
        if (buildings && buildings->IsArray())
        {
            for (size_t i = 0; i < buildings->Size(); ++i)
            {
                const Json& entry = buildings->At(i);
                std::string prefix = "buildings[" + std::to_string(i) + "] ";
                if (!entry.IsObject())
                {
                    error = prefix + "がオブジェクトではありません";
                    return false;
                }

                SnapshotBuilding building;
                std::string typeKey = entry.GetString("type", "");
                if (!BuildingTypeFromKey(typeKey, building.type))
                {
                    // 建物を飛ばすと、建物のuid(配列の添字)がずれて、住民の家・職場の指す先が変わってしまう。
                    // ゲームに存在しない建物の種類は、読み込みを中止する。
                    error = prefix + "の種類 \"" + typeKey + "\" はこのゲームにありません";
                    return false;
                }

                const Json* transform = entry.Find("transform");
                if (!transform || !transform->IsObject() ||
                    !ReadVec3Key(*transform, "position", building.transform.position) ||
                    !ReadVec3Key(*transform, "rotation", building.transform.rotation) ||
                    !ReadVec3Key(*transform, "scale", building.transform.scale))
                {
                    error = prefix + "の transform(位置・回転・拡大)が不正です";
                    return false;
                }
                building.transform.SetRotationEuler(building.transform.rotation);

                const Json* footprint = entry.Find("footprint");
                const Json* frame = entry.Find("frame");
                if (!footprint || !footprint->IsObject() || !frame || !frame->IsObject() ||
                    !ReadVec3Key(*footprint, "origin", building.placement.footprintRect.origin) ||
                    !ReadVec3Key(*frame, "origin", building.placement.frameOrigin) ||
                    !IsFiniteNumber(footprint->Find("yaw")) || !IsFiniteNumber(footprint->Find("width")) ||
                    !IsFiniteNumber(footprint->Find("depth")) || !IsFiniteNumber(frame->Find("yaw")))
                {
                    error = prefix + "の footprint / frame が不正です";
                    return false;
                }
                building.placement.footprintRect.yaw = footprint->GetFloat("yaw");
                building.placement.footprintRect.width = footprint->GetFloat("width");
                building.placement.footprintRect.depth = footprint->GetFloat("depth");
                building.placement.frameYaw = frame->GetFloat("yaw");

                const Json* localCell = entry.Find("localCell");
                const Json* localSize = entry.Find("localSize");
                if (!localCell || !localCell->IsArray() || localCell->Size() != 2 ||
                    !localSize || !localSize->IsArray() || localSize->Size() != 2)
                {
                    error = prefix + "の localCell / localSize が不正です";
                    return false;
                }
                building.placement.localCellX = localCell->At(0).AsInt();
                building.placement.localCellZ = localCell->At(1).AsInt();
                building.placement.localWidthCells = localSize->At(0).AsInt(1);
                building.placement.localDepthCells = localSize->At(1).AsInt(1);
                if (building.placement.localWidthCells < 1 || building.placement.localDepthCells < 1)
                {
                    error = prefix + "の localSize が不正です";
                    return false;
                }

                result.buildings.push_back(building);
            }
        }
        else if (buildings)
        {
            warnings.push_back("buildings が配列ではないため無視しました");
        }

        // ---- 青マス ----
        const Json* cells = root.Find("orientationCells");
        if (cells && cells->IsArray())
        {
            for (size_t i = 0; i < cells->Size(); ++i)
            {
                const Json& entry = cells->At(i);
                SnapshotOrientationCell cell;
                const Json* index = entry.Find("cell");
                if (!entry.IsObject() ||
                    !ReadVec3Key(entry, "origin", cell.origin) ||
                    !IsFiniteNumber(entry.Find("yaw")) ||
                    !index || !index->IsArray() || index->Size() != 2)
                {
                    error = "orientationCells[" + std::to_string(i) + "] が不正です";
                    return false;
                }
                cell.yaw = entry.GetFloat("yaw");
                cell.cellX = index->At(0).AsInt();
                cell.cellZ = index->At(1).AsInt();
                result.orientationCells.push_back(cell);
            }
        }
        else if (cells)
        {
            warnings.push_back("orientationCells が配列ではないため無視しました");
        }

        // ---- システムごとの状態の区画 ----
        // 中身の解釈は各ISaveableに任せるので、ここでは区画をそのまま持つ。
        const Json* state = root.Find("state");
        if (state && state->IsObject())
        {
            result.state = *state;
        }
        else if (state)
        {
            warnings.push_back("state がオブジェクトではないため無視しました");
        }

        // ルートの知らない項目は警告だけ(将来の版で足された項目や、手書きの注釈)。
        for (const auto& member : root.Members())
        {
            const std::string& key = member.first;
            if (key != "version" && key != "state" && key != "roads" && key != "buildings" && key != "orientationCells")
            {
                warnings.push_back("知らない項目 \"" + key + "\" を無視しました");
            }
        }

        snapshot = std::move(result);
        return true;
    }
}
