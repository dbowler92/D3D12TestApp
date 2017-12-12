#pragma once

class IScene
{
public:
	IScene() {};
	virtual ~IScene() = 0 {};

	virtual bool OnInitScene() = 0;
	virtual int OnCloseScene() = 0;

	virtual void OnUpdate(float dt) = 0;
	virtual void OnRender() = 0;
};