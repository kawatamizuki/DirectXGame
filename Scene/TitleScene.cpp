#include "TitleScene.h"
#include "SceneManager.h"
#include"InputManager.h"
#include"Renderer.h"
#include "Debug.h"

TitleScene::TitleScene(SceneManager* sceneManager, GameContext* context)
    : Scene(sceneManager, context)
{
}

TitleScene::~TitleScene()
{
}

void TitleScene::Init()
{
    Debug::Log("TitleScene::Init");
    
}

void TitleScene::Update()
{
    // タイトル画面のメニュー(スタート・終了)と、背景の街・カメラは、ゲーム本体(Game::UpdateTitle)が処理する。
    // (メニューの描画に、UIRenderer・スキンが要るため。以前は、どこをクリックしても始まっていたが、ボタンを押した時だけ始まる)
}

void TitleScene::Draw()
{

}

void TitleScene::Finalize()
{
    Debug::Log("TitleScene::Finalize");
}