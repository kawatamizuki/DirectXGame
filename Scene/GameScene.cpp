#include "GameScene.h"
#include "SceneManager.h"
#include"InputManager.h"
#include"Renderer.h"
#include "Debug.h"

GameScene::GameScene(SceneManager* sceneManager, GameContext* context)
    : Scene(sceneManager,context)
{
}

GameScene::~GameScene()
{
}

void GameScene::Init()
{
    Debug::Log("GameScene::Init");
}

void GameScene::Update()
{
    //Debug::Log("GameScene::Update"); // 毎フレーム出てログパネルが埋まってしまうためコメントアウト
}

void GameScene::Draw()
{
    //Debug::Log("GameScene::Draw"); // 毎フレーム出てログパネルが埋まってしまうためコメントアウト
}

void GameScene::Finalize()
{
    Debug::Log("GameScene::Finalize");
}