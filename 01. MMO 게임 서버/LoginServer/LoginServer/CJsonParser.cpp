#include <Windows.h>
#include <map>

#include "CJsonParser.h"



/*******************************************
Json 데이터를 나타내기 위한 CJsonObject 부모 클래스와 CJsonObject 클래스를 상속받는 데이터 클래스들
각 Json 데이터 타입 마다 하나의 클래스가 있다.
Key로 하위 Object 검색,
Key:Value 쌍 데이터 저장,
클래스 객체로부터 c++ 기본 자료형의 데이터 얻기 등의 기능이 있다.
********************************************/

// 유효하지 않은 key 또는 index로 검색했을 때 리턴할 None 객체
CJsonNone g_jsonNone;


/* CJsonObject */
// (virtual) CJsonStruct 클래스만 구현함. key에 해당하는 객체를 리턴함. 기본적으로는 CJsonNone 클래스 객체를 리턴함.
CJsonObject& CJsonObject::Key(const std::wstring& _key)
{ 
	return g_jsonNone; 
} 

// (virtual) CJsonArray 클래스만 구현함. index 위치의 객체를 리턴함. 기본적으로는 CJsonNone 클래스 객체를 리턴함.
CJsonObject& CJsonObject::Index(int _index) 
{
	return g_jsonNone; 
}




/* CJsonStruct */
CJsonStruct::CJsonStruct()
{
}

// (virtual)
CJsonStruct::~CJsonStruct()
{
	for (auto iter = m_mapObjects.begin(); iter != m_mapObjects.end(); ++iter)
	{
		delete iter->second;
	}
}

// (virtual) key를 입력받아 해당하는 객체를 리턴함
CJsonObject& CJsonStruct::Key(const std::wstring& _key)
{
	std::wstring key = L'"' + _key + L'"';
	auto iter = m_mapObjects.find(key);
	if (iter == m_mapObjects.end())
		return g_jsonNone;

	return *iter->second;
}

// (virtual)
bool CJsonStruct::Empty()
{
	if (Size() == 0)
		return true;
	else
		return false;
}

// (virtual)
void CJsonStruct::Print()
{
	wprintf(L"{");
	for (auto iter = m_mapObjects.begin(); iter != m_mapObjects.end(); ++iter)
	{
		wprintf(L"%s: ", iter->first.c_str());
		iter->second->Print();
		wprintf(L", ");
	}
	wprintf(L" }");
}


// map에 Key:Value 쌍을 삽입함
void CJsonStruct::Insert(std::wstring& _key, CJsonObject* _value)
{
	m_mapObjects.insert(std::make_pair(_key, _value));
}



/* CJsonArray */
CJsonArray::CJsonArray()
{
}

// (virtual)
CJsonArray::~CJsonArray()
{
	for (size_t i = 0; i < m_vecObjects.size(); i++)
	{
		delete m_vecObjects[i];
	}
}

// (virtual) 전달받은 index 위치의 객체를 리턴한다.
CJsonObject& CJsonArray::Index(int _index)
{
	if (_index >= m_vecObjects.size() || _index < 0)
		return g_jsonNone;

	return *m_vecObjects[_index];
}

// (virtual)
bool CJsonArray::Empty()
{
	if (Size() == 0)
		return true;
	else
		return false;
}

// (virtual)
void CJsonArray::Print()
{
	wprintf(L"[");
	for (size_t i = 0; i < m_vecObjects.size(); i++)
	{
		m_vecObjects[i]->Print();
		wprintf(L", ");
	}
	wprintf(L" ]");
}

// vector에 객체를 삽입한다.
void CJsonArray::Insert(CJsonObject* _object)
{
	m_vecObjects.push_back(_object);
}



/* CJsonString */
CJsonString::CJsonString(std::wstring& _str)
{
	m_value = _str.substr(1, _str.size() - 2);
}
// (virtual)
CJsonString::~CJsonString()
{
}


/* CJsonNumber */
CJsonNumber::CJsonNumber(std::wstring& _strNumber)
{
	m_value = std::stof(_strNumber);
}
// (virtual)
CJsonNumber::~CJsonNumber()
{
}


/* CJsonNone */
CJsonNone::CJsonNone()
{
}
// (virtual)
CJsonNone::~CJsonNone()
{
}












/*********************************
json text parsing 클래스
클래스 객체를 생성한 뒤 Parse 함수에 json text를 전달해주면 parsing 한다.
parsing 한 뒤 Key 함수로 원하는 Key에 대한 Value를 얻을 수 있다.
**********************************/

CJsonParser::CJsonParser()
	: m_pRootObject(nullptr), m_szJsonText(nullptr), _m_pParentObject(nullptr), _m_curIndex(0)
{
}

CJsonParser::~CJsonParser()
{
	Clear();
}

// 파일로부터 parsing
void CJsonParser::ParseJsonFile(const WCHAR* _szJsonFilePath)
{
	FILE* fJson;
	_wfopen_s(&fJson, _szJsonFilePath, L"r");
	if (fJson == nullptr)
	{
		wprintf(L"Json File doesn't exists : %s", _szJsonFilePath);
		return;
	}

	// .json 파일 읽기
	fseek(fJson, 0, SEEK_END);
	int sizeFile = ftell(fJson);
	WCHAR* szJsonText = (WCHAR*)malloc(sizeFile);
	rewind(fJson);
	fread_s(szJsonText, sizeFile, 1, sizeFile, fJson);
	fclose(fJson);

	// parse
	Parse(szJsonText);
	
	// 메모리해제
	free(szJsonText);
}


// Parsing
void CJsonParser::Parse(const WCHAR* _szJsonText)
{
	Init();

	m_szJsonText = _szJsonText;
	// json text를 토큰으로 분리함
	GetTokens();

	// parent object를 root object로 설정
	_m_pParentObject = m_pRootObject;
	// 만약 첫 토큰이 { 가 아니라면 구문 오류
	if (m_vecTokens[0] != L"{")
	{
		RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}

	// parsing 시작
	_m_curIndex = 1;
	ParseObject(L"}");

	// parse가 끝난 뒤에는 전체 텍스트는 더이상 필요없음
	m_szJsonText = nullptr;
}




// 토큰 출력
void CJsonParser::PrintTokens()
{
	wprintf(L"\n");
	for (size_t i = 0; i < m_vecTokens.size(); i++)
	{
		wprintf(L"%s, ", m_vecTokens[i].c_str());
	}
	wprintf(L"\n");
}


// json text를 토큰으로 분리
// 모든 토큰은 wstring 타입으로 저장되며, 문자열 토큰은 양옆에 " 가 붙음.
void CJsonParser::GetTokens()
{
	const WCHAR* json = m_szJsonText;
	std::wstring token;
	while (*json != L'\0')
	{
		if (*json == L'{' || *json == L'}' || *json == L'[' || *json == L']' || *json == L':' || *json == L',')
		{
			token.clear();
			token.append(1, *json);
			m_vecTokens.push_back(token);
			json++;
		}
		else if (*json == L'"')
		{
			json++;
			int len = 2;
			while (*json != '"')
			{
				len++;
				json++;				
			}
			json++;
			token.clear();
			token.append(json - len, len);
			m_vecTokens.push_back(token);
		}
		else if ((*json >= L'0' && *json <= L'9') || *json == L'-')
		{
			json++;
			int len = 1;
			while ((*json >= L'0' && *json <= L'9') || *json == L'.')
			{
				len++;
				json++;
			}
			token.clear();
			token.append(json - len, len);
			m_vecTokens.push_back(token);
		}
		else
		{
			json++;
		}
	}
}



// 모든 토큰을 순회하며 json 데이터를 parsing 함
// 만약 현재 토큰이 _escapeToken 이라면 종료됨
// 최초 호출 시 _escapeToken 은 L"}" 이어야 함
void CJsonParser::ParseObject(std::wstring _escapeToken)
{
	while (m_vecTokens[_m_curIndex] != _escapeToken)
	{
		// 현재 토큰이 { 이면 json 구조체 시작
		if (m_vecTokens[_m_curIndex] == L"{")
		{
			ParseStruct();
		}
		// 현재 토큰이 [ 이면 json array 시작
		else if (m_vecTokens[_m_curIndex] == L"[")
		{
			ParseArray();
		}
		// 현재 토큰이 문자열("로시작) 이고, 다음 토큰이 : 이라면 현재 토큰은 key임
		else if (m_vecTokens[_m_curIndex].front() == L'"' && m_vecTokens[_m_curIndex+1] == L":")
		{
			ParseKey();
		}
		// 현재 토큰이 문자열("로시작)임
		else if (m_vecTokens[_m_curIndex].front() == L'"')
		{
			ParseString();
		}
		// 현재 토큰이 숫자임
		else if (  (m_vecTokens[_m_curIndex].front() >= L'0' && m_vecTokens[_m_curIndex].front() <= L'9') 
			     || m_vecTokens[_m_curIndex].front() == L'-')
		{
			ParseNumber();
		}
		else {
			_m_curIndex++;
			if (_m_curIndex >= m_vecTokens.size())
			{
				RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL);
			}
		}
	}
	return;
}

// 현재 토큰이 key 인지 확인함
void CJsonParser::ParseKey()
{
	// key 인지 확인하기 위해 토큰의 시작과 끝이 " 인지 확인함
	WCHAR front = m_vecTokens[_m_curIndex].front();
	WCHAR back = m_vecTokens[_m_curIndex].back();
	if (front != L'"' || back != L'"')
	{
		RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}

	// 현재 토큰이 key인 경우 다음 token은 반드시 : 이고 그다음 토큰은 반드시 value 이어야 함
	// value에 대한 처리는 여기서 하지 않음
	_m_curIndex += 2;

	return;
}

// 현재 토큰이 숫자이므로 CJsonNumber 객체를 생성하고 등록함
void CJsonParser::ParseNumber()
{
	// 만약 부모가 struct 라면 앞 토큰에 key가 있어야함
	if (_m_pParentObject->GetObjectType() == JSON_OBJECT_TYPE::STRUCT && m_vecTokens[_m_curIndex - 1] == L":")
	{
		CJsonNumber* jsonNumber = new CJsonNumber(m_vecTokens[_m_curIndex]);
		reinterpret_cast<CJsonStruct*>(_m_pParentObject)->Insert(m_vecTokens[_m_curIndex - 2], jsonNumber);  // 부모 struct에 key:value 등록
	}
	// 만약 부모가 array 라면 앞 토큰에 key가 없어야함
	else if (_m_pParentObject->GetObjectType() == JSON_OBJECT_TYPE::ARRAY && m_vecTokens[_m_curIndex - 1] != L":")
	{
		CJsonNumber* jsonNumber = new CJsonNumber(m_vecTokens[_m_curIndex]);
		reinterpret_cast<CJsonArray*>(_m_pParentObject)->Insert(jsonNumber);  // 부모 array에 객체 등록
	}
	else
	{
		RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
	_m_curIndex++;
	return;
}

// 현재 토큰이 문자열이므로 CJsonString 객체를 생성하고 등록함
void CJsonParser::ParseString()
{
	// 만약 부모가 struct 라면 앞 토큰에 key가 있어야함
	if (_m_pParentObject->GetObjectType() == JSON_OBJECT_TYPE::STRUCT && m_vecTokens[_m_curIndex - 1] == L":")
	{
		CJsonString* jsonString = new CJsonString(m_vecTokens[_m_curIndex]);
		reinterpret_cast<CJsonStruct*>(_m_pParentObject)->Insert(m_vecTokens[_m_curIndex - 2], jsonString);  // 부모 struct에 key:value 등록
	}
	// 만약 부모가 array 라면 앞 토큰에 key가 없어야함
	else if (_m_pParentObject->GetObjectType() == JSON_OBJECT_TYPE::ARRAY && m_vecTokens[_m_curIndex - 1] != L":")
	{
		CJsonString* jsonString = new CJsonString(m_vecTokens[_m_curIndex]);
		reinterpret_cast<CJsonArray*>(_m_pParentObject)->Insert(jsonString);  // 부모 array에 객체 등록
	}
	else
	{
		RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
	_m_curIndex++;
	return;
}



// 현재 토큰이 { 이므로 CJsonStruct 객체를 생성하고 등록함
void CJsonParser::ParseStruct()
{
	// 현재 parent object를 백업함. struct 객체를 생성할 경우 이 다음 오는 토큰들은 현재 struct 객체가 부모가 됨
	CJsonObject* pParentObjectBackup = _m_pParentObject;
	// 만약 부모가 struct 라면 앞 토큰에 key가 있어야함
	if (_m_pParentObject->GetObjectType() == JSON_OBJECT_TYPE::STRUCT && m_vecTokens[_m_curIndex - 1] == L":")
	{
		CJsonStruct* jsonStruct = new CJsonStruct();
		reinterpret_cast<CJsonStruct*>(_m_pParentObject)->Insert(m_vecTokens[_m_curIndex - 2], jsonStruct);  // 부모 struct에 key:value 등록
		_m_pParentObject = jsonStruct;
	}
	// 만약 부모가 array 라면 앞 토큰에 key가 없어야함
	else if (_m_pParentObject->GetObjectType() == JSON_OBJECT_TYPE::ARRAY && m_vecTokens[_m_curIndex - 1] != L":")
	{
		CJsonStruct* jsonStruct = new CJsonStruct();
		reinterpret_cast<CJsonArray*>(_m_pParentObject)->Insert(jsonStruct);  // 부모 array에 객체 등록
		_m_pParentObject = jsonStruct;
	}
	else
	{
		RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}

	// 다음 _escapeToken을 } 로 설정하여 계속해서 parsing
	_m_curIndex++;
	ParseObject(L"}");

	// 현재 위치로 돌아왔을 때 현재 토큰이 } 이어야 함
	if (m_vecTokens[_m_curIndex] != L"}")
	{
		RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
	// parent object를 되돌린다.
	_m_pParentObject = pParentObjectBackup;
	_m_curIndex++;
	return;
}


// 현재 토큰이 [ 이므로 CJsonArray 객체를 생성하고 등록함
void CJsonParser::ParseArray()
{
	// 현재 parent object를 백업함. array 객체를 생성할 경우 이 다음 오는 토큰들은 현재 array 객체가 부모가 됨
	CJsonObject* pParentObjectBackup = _m_pParentObject;
	// 만약 부모가 struct 라면 앞 토큰에 key가 있어야함
	if (_m_pParentObject->GetObjectType() == JSON_OBJECT_TYPE::STRUCT && m_vecTokens[_m_curIndex - 1] == L":")
	{
		CJsonArray* jsonArray = new CJsonArray();
		reinterpret_cast<CJsonStruct*>(_m_pParentObject)->Insert(m_vecTokens[_m_curIndex - 2], jsonArray);  // 부모 struct에 key:value 등록
		_m_pParentObject = jsonArray;
	}
	// 만약 부모가 array 라면 앞 토큰에 key가 없어야함
	else if (_m_pParentObject->GetObjectType() == JSON_OBJECT_TYPE::ARRAY && m_vecTokens[_m_curIndex - 1] != L":")
	{
		CJsonArray* jsonArray = new CJsonArray();
		reinterpret_cast<CJsonArray*>(_m_pParentObject)->Insert(jsonArray);  // 부모 array에 객체 등록
		_m_pParentObject = jsonArray;
	}
	else
	{
		RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
	
	// 다음 _escapeToken을 ] 로 설정하여 계속해서 parsing
	_m_curIndex++;
	ParseObject(L"]");

	// 현재 위치로 돌아왔을 때 현재 토큰이 } 이어야 함
	if (m_vecTokens[_m_curIndex] != L"]")
	{
		RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
	// parent object를 되돌린다.
	_m_pParentObject = pParentObjectBackup;
	_m_curIndex++;
	return;
}



// 모든 객체 삭제
void CJsonParser::Clear()
{
	m_szJsonText = nullptr;

	if(m_pRootObject != nullptr)
		delete m_pRootObject;
	m_pRootObject = nullptr;
	_m_pParentObject = nullptr;

	m_vecTokens.clear();
}

// 초기화. 최상위 object를 생성한다.
void CJsonParser::Init()
{
	Clear();
	m_pRootObject = new CJsonStruct;
	_m_pParentObject = m_pRootObject;
}








