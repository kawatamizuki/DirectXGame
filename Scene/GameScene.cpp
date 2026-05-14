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
    Debug::Log("GameScene::Update");
}

void GameScene::Draw()
{
    Debug::Log("GameScene::Draw");
}

void GameScene::Finalize()
{
    Debug::Log("GameScene::Finalize");
}