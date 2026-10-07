#pragma once

// 建物の種類。
// 将来、規模違い(小規模住宅/中規模マンション等)を増やす時もこのenumに値を足すだけで拡張できる。
enum class BuildingType
{
    House,  // 住宅(需要を発生させる)
    Office, // 職場(需要を受け入れる)
    Shop    // 商店(今はまだ需要ロジック未接続)
};

// 1つの建物種別が持つ性能値をまとめたデータ。
struct BuildingDefinition
{
    const char* name;                 // 表示名
    const char* modelPath;            // 見た目のモデルファイルパス

    // 占有するグリッドセル数(幅x奥行き)。BuildControllerはモデルの実測サイズを
    // この幅x奥行き(ワールド単位ではfootprintCells*cellSize)にちょうど収まるよう
    // 自動でスケールを計算するため、ここを2x2等に変えるだけで大型建物にも対応できる。
    int footprintWidthCells = 1;
    int footprintDepthCells = 1;

    int populationRepresented = 0;    // この建物が表す実際の人口(表示用。House用)
    int representativeAgentCount = 0; // 実際に個体シミュレートする代表住民の人数(House用)
    int capacity = 0;                 // 受け入れ可能な需要数(Office用)
};

// typeに対応する定義を返す。
const BuildingDefinition& GetBuildingDefinition(BuildingType type);
