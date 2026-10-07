#include "GameScene.h"
#include "SceneManager.h"
#include"InputManager.h"
#include"Renderer.h"
#include "Debug.h"
#include "GameContext.h"
#include "ScenarioManager.h"

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

    // ゲーム開始時の初期マップ(Scenarios/tutorial.json)があれば、その街から始める。無ければ空の街。
    if (m_context && m_context->scenario)
    {
        m_context->scenario->LoadStartScenario();
    }
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