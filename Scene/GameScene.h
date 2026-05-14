#pragma once
#include "Scene.h"

class GameScene : public Scene
{
public:
    GameScene(SceneManager* sceneManager, GameContext* context);
    ~GameScene() override;

    void Init() override;
    void Update() override;
    void Draw() override;
    void Finalize() override;
};