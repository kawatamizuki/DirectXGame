#pragma once
#include "Model.h"
#include "Transform.h"

class GameObject
{
public:
    Model* model = nullptr;
    Transform transform;
};
