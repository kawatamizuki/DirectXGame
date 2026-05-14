#pragma once
#include "Scene.h"

class TitleScene : public Scene
{
public:
    TitleScene(SceneManager* sceneManager, GameContext* context);
    ~TitleScene() override;

    void Init() override;
    void Update() override;
    void Draw() override;
    void Finalize() override;
};