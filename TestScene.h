#pragma once

#include "IScene.h"

class TestScene : public IScene
{
public:
	TestScene();
	~TestScene();

	bool OnInitScene() override;
	int OnCloseScene() override;

	void OnUpdate(float dt) override;
	void OnRender() override;
};