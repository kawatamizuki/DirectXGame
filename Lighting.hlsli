// 光の計算をここ1か所にまとめる(SimpleShader.hlslのピクセルシェーダーから呼ぶ)。
//
// 将来、画風を変える(トゥーン調・明るさの段階化によるドット絵風など)時や、影
// (シャドウマップ)の項を足す時は、この ApplyLighting だけを触ればよい。
// メッシュ・頂点シェーダー・定数バッファの受け渡し側は変えずに済む。
//
// 影を足す時のために、定数バッファは b3、テクスチャは t1、サンプラーは s1 を空けてある。

// C++側の LightBuffer(Renderer.cpp)と対応している。register(b2)。
// float3の後ろのfloatは、16バイト境界に合わせるための詰め物。
cbuffer LightBuffer : register(b2)
{
    float3 directionToLight; // 光源(昼は太陽、夜は月)へ向かう向き(正規化済み)
    float lightPad0;
    float3 lightColor;       // 光の色(強さ込み)
    float lightPad1;
    float3 ambientSky;       // 上向きの面に届く環境光
    float lightPad2;
    float3 ambientGround;    // 下向きの面に届く環境光
    float lightPad3;
};

// baseColor(テクスチャ色または頂点色)に、光による明暗をかけた色を返す。
// worldNormalはワールド空間の法線(補間で長さが変わるのでここで正規化し直す)。
float3 ApplyLighting(float3 baseColor, float3 worldNormal)
{
    float3 n = normalize(worldNormal);

    // 半球環境光: 上向きの面ほど空の色、下向きの面ほど地面の色に近づける。
    float skyAmount = n.y * 0.5f + 0.5f;
    float3 ambient = lerp(ambientGround, ambientSky, skyAmount);

    // 光源に向いている面ほど明るく(ランバート拡散)。
    float diffuse = saturate(dot(n, directionToLight));

    return baseColor * (ambient + lightColor * diffuse);
}
