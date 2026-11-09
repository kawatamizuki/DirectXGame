#pragma once
#include <vector>
#include "CommercialArea.h"

// 商業エリアの区画を、住民のシミュレーション(DemandSystem)から読むための、小さな読み取り専用の窓口(P2-C1)。
//  ・シミュレーションは、区画の一覧と設定だけを見る。区画の操作(置く・動かす・周知する)は、できない。
//  ・所属(どの施設がどの区画か)は、窓口からは受け取らず、シミュレーションが、自分の施設一覧から、P2-A の純粋な関数(ComputeAreaMembers)で求める。
//    ゲームのフレーム・描画に依存せず、耐久試験・再現テスト(画面を描かずに進める)でも、同じ結果になる。
//  ・実体は CommercialAreaManager。将来、別の実装(テスト用の固定データなど)に差し替えられる。
class IAreaSource
{
public:
    virtual ~IAreaSource() = default;

    // 区画の一覧(非公開の仮置きも含む。公開状態は、CommercialArea::published)。
    virtual const std::vector<CommercialArea>& GetAreas() const = 0;

    // 区画の設定(所属のしきい値など)。
    virtual const CommercialAreaSettings& GetAreaSettings() const = 0;
};
