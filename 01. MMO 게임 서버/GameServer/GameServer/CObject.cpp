#include <Windows.h>
#include "CObject.h"


// static 멤버 초기화
INT64 CObject::__sNextClientId = 0;

CObject::CObject()
	: _clientId(0)
{
}

void CObject::Init()
{
	_clientId = InterlockedIncrement64(&__sNextClientId);
}