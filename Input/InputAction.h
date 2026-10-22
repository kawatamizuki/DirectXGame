#pragma once

enum class InputAction
{
    Decide,
    Cancel,
    Pause,
    Rotate, // 配置プレビューを90度回転する(建物配置時に使用)

    // 街づくり(Playing)のカメラ。平面の移動(平行移動)と、ズーム(縮尺。ホイール)の2つの要素。
    CameraForward,  // 奥へ平行移動
    CameraBack,     // 手前へ平行移動
    CameraLeft,     // 左へ平行移動
    CameraRight,    // 右へ平行移動
    CameraFast,     // 押している間、移動を速くする
    CameraDrag      // 押しながらマウスを動かして、地面をつかんで動かす(中ボタン)
};
