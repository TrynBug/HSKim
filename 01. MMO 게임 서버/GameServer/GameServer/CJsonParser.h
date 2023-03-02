#pragma once

#include <string>
#include <vector>
#include <unordered_map>


/*******************************************
Json 데이터를 나타내기 위한 CJsonObject 부모 클래스와 CJsonObject 클래스를 상속받는 데이터 클래스들
각 Json 데이터 타입 마다 하나의 클래스가 있다.
Key로 하위 Object 검색,
Key:Value 쌍 데이터 저장,
클래스 객체로부터 c++ 기본 자료형의 데이터 얻기 등의 기능이 있다.
********************************************/

#define ExceptionGetInvalidValue() RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL)

enum class JSON_OBJECT_TYPE
{
	OBJECT,   // 최상위 추상타입
	STRUCT,   // {}
	ARRAY,    // []
	STRING,   // 문자열
	NUMBER,   // 숫자
	NONE,     // 값없음
	END
};


// 다형성 유지를 위한 부모 클래스
class CJsonObject
{
public:
	virtual JSON_OBJECT_TYPE GetObjectType() { return JSON_OBJECT_TYPE::OBJECT; }   // 객체 타입
	virtual UINT Size() { return 0; }                                               // 현재 객체가 CJsonStruct나 CJsonArray 클래스일 경우 element 수를 리턴함

public:
	virtual CJsonObject& Key(const std::wstring& _key);     // CJsonStruct 클래스만 구현함. key에 해당하는 객체를 리턴함. 기본적으로는 CJsonNone 클래스 객체를 리턴함.
	virtual CJsonObject& Index(int _index);                 // CJsonArray 클래스만 구현함. index 위치의 객체를 리턴함. 기본적으로는 CJsonNone 클래스 객체를 리턴함.

	virtual int Int() { ExceptionGetInvalidValue(); return 0; }             // CJsonNumber 클래스만 구현함. 자신의 float 멤버를 int로 리턴함.
	virtual float Float() { ExceptionGetInvalidValue(); return 0; }         // CJsonNumber 클래스만 구현함. 자신의 float를 리턴함.
	virtual const std::wstring& Str() { ExceptionGetInvalidValue(); return nullptr; }   // CJsonString 클래스만 구현함. 자신의 wstring 멤버를 리턴함.
	virtual WCHAR Char() { ExceptionGetInvalidValue(); return 0; }          // CJsonString 클래스만 구현함. 자신의 wstring 멤버의 첫 번째 문자만 리턴함.

	virtual bool Empty() = 0;    // 배열이 비어있거나, 값이 없으면 true 리턴
	virtual void Print() = 0;    // print

public:
	// [] 연산자 오버로딩
	CJsonObject& operator[] (const std::wstring& _key) { return Key(_key); }
	CJsonObject& operator[] (UINT _index) { return Index(_index); }

public:
	virtual ~CJsonObject() {};
};




// json 구조체. json text 에서 { } 에 해당한다.
// Key:Value 쌍을 저장할 수 있는 map을 가지고 있다.
class CJsonStruct : public CJsonObject
{
private:
	std::unordered_map<std::wstring, CJsonObject*> m_mapObjects;

public:
	virtual JSON_OBJECT_TYPE GetObjectType() { return JSON_OBJECT_TYPE::STRUCT; }
	virtual UINT Size() { return (UINT)m_mapObjects.size(); }

public:
	// key를 입력받아 해당하는 객체를 리턴함
	virtual CJsonObject& Key(const std::wstring& _key);

	virtual bool Empty();
	virtual void Print();

public:
	// map에 Key:Value 쌍을 삽입함
	void Insert(std::wstring& _key, CJsonObject* _value);

public:
	CJsonStruct();
	virtual ~CJsonStruct();
};


// json array. json text 에서 [ ] 에 해당한다.
// 객체를 넣을 수 있는 vector를 가지고 있다.
class CJsonArray : public CJsonObject
{
private:
	std::vector<CJsonObject*> m_vecObjects;
public:
	virtual JSON_OBJECT_TYPE GetObjectType() { return JSON_OBJECT_TYPE::ARRAY; }
	virtual UINT Size() { return (UINT)m_vecObjects.size(); }

public:
	// 전달받은 index 위치의 객체를 리턴한다.
	virtual CJsonObject& Index(int _index);

	virtual bool Empty();
	virtual void Print();

public:
	// vector에 객체를 삽입한다.
	void Insert(CJsonObject* _object);

public:
	CJsonArray();
	virtual ~CJsonArray();
};


// 문자열 저장 클래스. json text 에서 문자열 데이터에 해당함.
class CJsonString : public CJsonObject
{
private:
	std::wstring m_value;

public:
	virtual JSON_OBJECT_TYPE GetObjectType() { return JSON_OBJECT_TYPE::STRING; }

public:
	virtual const std::wstring& Str() { return m_value; }
	virtual WCHAR Char() { return m_value.front(); }

	virtual bool Empty() { return false; }
	virtual void Print() { wprintf(L"%s", m_value.c_str()); }

public:
	CJsonString(std::wstring& _str);
	virtual ~CJsonString();
};


// 숫자 저장 클래스. json text 에서 숫자 데이터에 해당함.
class CJsonNumber : public CJsonObject
{
private:
	float m_value;

public:
	virtual JSON_OBJECT_TYPE GetObjectType() { return JSON_OBJECT_TYPE::NUMBER; }

public:
	virtual int Int() { return (int)m_value; }
	virtual float Float() { return m_value; }

	virtual bool Empty() { return false; }
	virtual void Print() { wprintf(L"%f", m_value); }

public:
	CJsonNumber(std::wstring& _strNumber);
	virtual ~CJsonNumber();
};


// None 값에 해당하는 클래스
// 잘못된 Key로 검색하거나, 잘못된 index로 검색했을 때 global CJsonNone 객체인 g_jsonNone 가 리턴됨
// 이 클래스 객체는 다른 곳에서 생성하지 않음.
class CJsonNone : public CJsonObject
{
public:
	virtual JSON_OBJECT_TYPE GetObjectType() { return JSON_OBJECT_TYPE::NONE; }

public:
	virtual bool Empty() { return true; }
	virtual void Print() { wprintf(L"None"); }

public:
	CJsonNone();
	virtual ~CJsonNone();
};













/*********************************
json text parsing 클래스
클래스 객체를 생성한 뒤 Parse 함수에 json text를 전달해주면 parsing 한다.
parsing 한 뒤 Key 함수로 원하는 Key에 대한 Value를 얻을 수 있다.
**********************************/
class CJsonParser
{
public:


private:
	const WCHAR* m_szJsonText;           // json text
	CJsonStruct* m_pRootObject;          // 최상위 object
	std::vector<std::wstring> m_vecTokens;   // 토큰

	// Parse 함수 내부적으로 사용되는 변수
	CJsonObject* _m_pParentObject;
	size_t _m_curIndex;

public:
	// json parsing
	void ParseJsonFile(const WCHAR* _szJsonFilePath);
	void Parse(const WCHAR* _szJsonText);

	// Parse 한 뒤 Key 함수 또는 [] 연산자로 Key에 해당하는 object에 접근함
	CJsonObject& Key(const std::wstring& _key) { return m_pRootObject->Key(_key); }
	CJsonObject& operator[] (const std::wstring& _key) { return Key(_key); }

	// print
	void PrintJson() { PrintJson(m_pRootObject); }
	void PrintJson(CJsonObject* _jsonObject) { _jsonObject->Print(); };
	void PrintTokens();
private:
	// json text를 token으로 분리함
	void GetTokens();

	// parsing function
	void ParseObject(std::wstring _escapeToken);
	void ParseStruct();
	void ParseKey();
	void ParseArray();
	void ParseString();
	void ParseNumber();

	// clear, init
public:
	void Clear();
	void Init();

public:
	CJsonParser();
	~CJsonParser();
};









