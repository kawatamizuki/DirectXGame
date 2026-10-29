#pragma once

// 建物の種類。
// 将来、規模違い(小規模住宅/中規模マンション等)を増やす時もこのenumに値を足すだけで拡張できる。
// 保存データ(ScenarioFile)は、数値ではなく名前で保存するので、値の並びを変えても古いセーブが読める。
enum class BuildingType
{
    House,      // 住宅(住民が住む。1軒に1人)
    Office,     // 職場(仕事の概念は一旦停止中。建てられるが、今は何も起きない)
    Shop,       // コンビニ(日用品。小さい敷地、近くにある利便性)
    Supermarket,// スーパー(日用品。まとめ買い向き)
    Cinema,     // 映画館(娯楽。外出の主目的になる)
    Cafe        // カフェ(娯楽(仮)。外出の主目的/立ち寄り)
};

// BuildingTypeの種類の数(配列の大きさ・繰り返しに使う。種類を足したら、ここも直す)。
constexpr int kBuildingTypeCount = 6;

// 1つの建物種別が持つ性能値をまとめたデータ。
// 施設(コンビニ・スーパー・映画館・カフェ)としての性質(品質・値段・需要の回復量など)は、FacilityData.h にある。
struct BuildingDefinition
{
    const char* name;                 // 表示名
    const char* modelPath;            // 見た目のモデルファイルパス

    // 占有するグリッドセル数(幅x奥行き)。BuildControllerはモデルの実測サイズを
    // この幅x奥行き(ワールド単位ではfootprintCells*cellSize)にちょうど収まるよう
    // 自動でスケールを計算するため、ここを2x2等に変えるだけで大型建物にも対応できる。
    // (マスは WorldScale::kSubdivision 分割されているので、表は「最初の1マス」単位で書き、それに kSubdivision をかけた値が入る)
    int footprintWidthCells = 1;
    int footprintDepthCells = 1;

    int populationRepresented = 0;    // この建物が表す実際の人口(表示用。House用)
    int representativeAgentCount = 0; // 実際に個体シミュレートする住民の人数(House用)
    int capacity = 0;                 // 受け入れ可能な需要数(Office用。仕事の概念は停止中)

    // 建設費(円)。建物を置く時にGameEconomyから払う。ゲームバランスの調整は、この表の数字だけを変えればよい。
    int cost = 0;

    // 見た目の拡大率。1 = 敷地(マス)にちょうど収まる大きさ。1より大きくすると、建物の見た目が、敷地からはみ出して大きくなる
    // (マスの占有・置ける場所は変わらない。大きくしすぎると、隣の建物・道路に、見た目が重なる)。
    float modelScale = 1.0f;

    // trueなら、モデルの正面(モデルのローカル-Z側。Kenney・変換したモデルは、正面が-Z側)を、敷地の正面の辺にそろえて置く。
    // falseなら、モデルを敷地の真ん中に置く(奥行きの浅いモデルは、前後に隙間ができて、道路から遠く見える)。
    // 建物の向きを回すと、敷地と一緒に回るので、正面を道路に向ければ、正面が道路の縁に接する。
    bool alignFrontToFootprint = false;
};

// typeに対応する定義を返す。
const BuildingDefinition& GetBuildingDefinition(BuildingType type);
