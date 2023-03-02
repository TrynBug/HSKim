#pragma once

class CObject
{
private:
	static INT64 __sNextClientId;

public:
	INT64 _clientId;   // object 고유 번호

public:
	CObject();
	virtual ~CObject() {}

public:
	void Init();
	virtual void Update() = 0;
};

