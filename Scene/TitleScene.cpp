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
    
    if (m_context->input->IsActionPressed(InputAction::Decide))
    {
        m_sceneManager->ChangeScene(SceneName::Game);
    }
 

}

void TitleScene::Draw()
{

}

void TitleScene::Finalize()
{
    Debug::Log("TitleScene::Finalize");
}