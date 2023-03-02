#pragma once

#include <Windows.h>
#include <vector>

namespace game_netserver
{

template <typename _Ty>
class CMemoryPoolTLS
{
private:
	class Chunk
	{
		friend class CMemoryPoolTLS;

		struct BlockNode
		{
			Chunk* _pChunk;
			unsigned __int64 _leftVerifyCode;
			_Ty _object;
			unsigned __int64 _rightVerifyCode;
		};

		Chunk* _pNext;
		BlockNode* _arrNode;
		int _allocIndex;
		long _freeCount;
		bool _isNew;

		Chunk(int chunkSize, unsigned __int64 leftVerifyCode, unsigned __int64 rightVerifyCode)
			: _pNext(nullptr), _allocIndex(0), _freeCount(0), _isNew(true)
		{
			_arrNode = (BlockNode*)malloc(sizeof(BlockNode) * chunkSize);
			for (int i = 0; i < chunkSize; i++)
			{
				_arrNode[i]._pChunk = this;
				_arrNode[i]._leftVerifyCode = leftVerifyCode;
				_arrNode[i]._rightVerifyCode = rightVerifyCode;
			}
		}
		~Chunk()
		{
			free(_arrNode);
		}
	};

	class ThreadChunkManager
	{
		friend class CMemoryPoolTLS;

		Chunk* _pChunk;
		int    _objectAllocCount;

		ThreadChunkManager()
			:_pChunk(nullptr), _objectAllocCount(0)
		{}
	};

private:
	int _initPoolSize;      // 초기 pool size
	bool _bPlacementNew;    // true 면 Alloc, Free 할 때 마다 객체의 생성자, 소멸자 호출. 
							// false면 객체가 처음 생성될 때 한 번만 생성자 호출, 메모리풀이 소멸될 때 모든 객체에서 소멸자 호출

	int _chunkSize;          // Chunk 하나에 들어있는 객체 수
	long _numFreeChunk;      // pool 내의 Chunk 수 (이 멤버는 _srwlPool 내에서만 변경되어야함)
	long _numAllocChunk;     // Alloc 된 Chunk 수 (이 멤버는 _srwlPool 내에서만 변경되어야함)

	SRWLOCK _srwlPool;
	DWORD _tlsIndex;

	std::vector<ThreadChunkManager*> _vecThreadMgr;
	SRWLOCK _srwlVecThreadMgr;

	unsigned __int64 _leftVerifyCode;
	unsigned __int64 _rightVerifyCode;


	Chunk* _pFreeChunk;  // 현재 free chunk (이 멤버는 _srwlPool 내에서만 변경되어야함)

public:
	CMemoryPoolTLS(int initPoolSize, bool bPlacementNew, int chunkSize);
	~CMemoryPoolTLS();

public:
	int GetFreeCount() { return _numFreeChunk * _chunkSize; }
	int GetAllocCount() { return _numAllocChunk * _chunkSize; }
	int GetActualAllocCount();
	int GetPoolSize() { return (_numFreeChunk + _numAllocChunk) * _chunkSize; }

	_Ty* Alloc();
	void Free(_Ty* ptr);

private:
	Chunk* AllocChunk();
	void FreeChunk(Chunk* pChunk);

	void Crash();

};



template <typename _Ty>
CMemoryPoolTLS<_Ty>::CMemoryPoolTLS(int initPoolSize, bool bPlacementNew, int chunkSize)
	:_initPoolSize(initPoolSize), _bPlacementNew(bPlacementNew), _chunkSize(chunkSize)
	, _pFreeChunk(nullptr), _numAllocChunk(0)
{
	InitializeSRWLock(&_srwlPool);
	InitializeSRWLock(&_srwlVecThreadMgr);
	_tlsIndex = TlsAlloc();
	if (_tlsIndex == TLS_OUT_OF_INDEXES)
	{
		int* p = 0;
		*p = 0;
	}

	// 검증코드 생성
	_leftVerifyCode =
		(unsigned __int64)rand() << 60
		| (unsigned __int64)rand() << 45
		| (unsigned __int64)rand() << 30
		| (unsigned __int64)rand() << 15
		| (unsigned __int64)rand();

	_rightVerifyCode =
		(unsigned __int64)rand() << 60
		| (unsigned __int64)rand() << 45
		| (unsigned __int64)rand() << 30
		| (unsigned __int64)rand() << 15
		| (unsigned __int64)rand();

	// 초기 청크 수 결정
	if (_initPoolSize % _chunkSize == 0)
		_numFreeChunk = _initPoolSize / _chunkSize;
	else
		_numFreeChunk = _initPoolSize / _chunkSize + 1;

	// 초기 청크 생성
	Chunk* pNewChunk;
	Chunk* pPrevChunk;
	if (_numFreeChunk > 0)
	{
		// 첫 청크 생성 및 초기화
		pNewChunk = new Chunk(_chunkSize, _leftVerifyCode, _rightVerifyCode);
		_pFreeChunk = pNewChunk;
		pPrevChunk = pNewChunk;

		// 모든 청크 생성 및 초기화
		for (int i = 1; i < _numFreeChunk; i++)
		{
			pNewChunk = new Chunk(_chunkSize, _leftVerifyCode, _rightVerifyCode);
			pPrevChunk->_pNext = pNewChunk;
			pPrevChunk = pNewChunk;
		}
		pNewChunk->_pNext = nullptr;
	}
}





template <typename _Ty>
CMemoryPoolTLS<_Ty>::~CMemoryPoolTLS()
{
	AcquireSRWLockExclusive(&_srwlPool);
	Chunk* pChunk = _pFreeChunk;
	Chunk* pNextChunk = _pFreeChunk;
	// pool 내의 모든 노드를 삭제
	for (int iCnt = 0; iCnt < _numFreeChunk; iCnt++)
	{
		pChunk = pNextChunk;
		if (_bPlacementNew == false)
		{
			// 소멸자 호출
			if (pChunk->_isNew == false)
			{
				for (int n = 0; n < _chunkSize; n++)
					(pChunk->_arrNode[n]._object).~_Ty();
			}
			else
			{
				for (int n = 0; n < pChunk->_allocIndex; n++)
					(pChunk->_arrNode[n]._object).~_Ty();
			}
		}
		pNextChunk = pChunk->_pNext;
		delete pChunk;
	}
	ReleaseSRWLockExclusive(&_srwlPool);

	// 스레드에 할당된 모든 매니저와 노드를 삭제
	AcquireSRWLockExclusive(&_srwlVecThreadMgr);
	ThreadChunkManager* pMgr;
	for (int iCnt = 0; iCnt < _vecThreadMgr.size(); iCnt++)
	{
		pMgr = _vecThreadMgr[iCnt];
		pChunk = pMgr->_pChunk;
		if (pChunk != nullptr)
		{
			if (_bPlacementNew == false)
			{
				// 소멸자 호출
				for (int n = 0; n < pChunk->_allocIndex; n++)
					(pChunk->_arrNode[n]._object).~_Ty();
			}
			delete pChunk;
		}
		delete pMgr;
	}
	_vecThreadMgr.clear();
	ReleaseSRWLockExclusive(&_srwlVecThreadMgr);

	// tls free
	TlsFree(_tlsIndex);
}



template <typename _Ty>
int CMemoryPoolTLS<_Ty>::GetActualAllocCount()
{
	int objectAllocCount = 0;
	AcquireSRWLockShared(&_srwlVecThreadMgr);
	for (int i = 0; i < _vecThreadMgr.size(); i++)
	{
		objectAllocCount += _vecThreadMgr[i]->_objectAllocCount;
	}
	ReleaseSRWLockShared(&_srwlVecThreadMgr);
	return objectAllocCount;
}



template <typename _Ty>
_Ty* CMemoryPoolTLS<_Ty>::Alloc()
{
	// 스레드 청크 매니저 가져오기
	ThreadChunkManager* pMgr = (ThreadChunkManager*)TlsGetValue(_tlsIndex);

	// 스레드에 매니저가 없다면 생성함
	if (pMgr == nullptr)
	{
		pMgr = new ThreadChunkManager;
		AcquireSRWLockExclusive(&_srwlVecThreadMgr);
		_vecThreadMgr.push_back(pMgr);
		ReleaseSRWLockExclusive(&_srwlVecThreadMgr);
		TlsSetValue(_tlsIndex, pMgr);
	}
	// 매니저에 청크가 없다면 할당받음
	if (pMgr->_pChunk == nullptr)
		pMgr->_pChunk = AllocChunk();
	Chunk* pChunk = pMgr->_pChunk;

	// 리턴할 객체 얻기
	_Ty* pObj = &pChunk->_arrNode[pChunk->_allocIndex]._object;

	// placementNew == true 이거나, placementNew == false 이지만 최초로 생성된 객체일 경우 생성자 호출
	if (_bPlacementNew == true
		|| (pChunk->_isNew == true && _bPlacementNew == false))
	{
		new (pObj) _Ty;
	}

	// 청크의 allocIndex를 증가시키고 끝까지 증가시켰다면 청크를 매니저에게서 떼어냄
	pChunk->_allocIndex++;
	if (pChunk->_allocIndex == _chunkSize)
		pMgr->_pChunk = nullptr;

	// 매니저의 object alloc count 증가
	pMgr->_objectAllocCount++;
	return pObj;
}


template <typename _Ty>
typename CMemoryPoolTLS<_Ty>::Chunk* CMemoryPoolTLS<_Ty>::AllocChunk()
{
	Chunk* pChunk;
	AcquireSRWLockExclusive(&_srwlPool);
	if (_numFreeChunk > 0)
	{
		pChunk = _pFreeChunk;
		_pFreeChunk = _pFreeChunk->_pNext;
		_numFreeChunk--;
		_numAllocChunk++;
		ReleaseSRWLockExclusive(&_srwlPool);

		pChunk->_pNext = nullptr;
		pChunk->_allocIndex = 0;
		pChunk->_freeCount = 0;
	}
	else
	{
		_numAllocChunk++;
		ReleaseSRWLockExclusive(&_srwlPool);
		pChunk = new Chunk(_chunkSize, _leftVerifyCode, _rightVerifyCode);
	}

	return pChunk;
}




template <typename _Ty>
void CMemoryPoolTLS<_Ty>::Free(_Ty* ptr)
{
	// 코드 검증
	typename Chunk::BlockNode* pNode = (typename Chunk::BlockNode*)((char*)ptr - (unsigned __int64)(&((typename Chunk::BlockNode*)nullptr)->_object));
	if (pNode->_leftVerifyCode != _leftVerifyCode
		|| pNode->_rightVerifyCode != _rightVerifyCode)
	{
		Crash();
	}

	// placement new = true 일 경우 소멸자 호출
	if (_bPlacementNew)
	{
		(pNode->_object).~_Ty();
	}

	// 노드가 포함된 청크의 freeCount 증가
	long freeCount = InterlockedIncrement(&pNode->_pChunk->_freeCount);

	// 청크의 freeCount가 모두 증가하였다면 청크를 Free함
	if (freeCount == _chunkSize)
		FreeChunk(pNode->_pChunk);


	// 스레드 청크 매니저 가져오기
	ThreadChunkManager* pMgr = (ThreadChunkManager*)TlsGetValue(_tlsIndex);

	// 스레드에 매니저가 없다면 생성함
	if (pMgr == nullptr)
	{
		pMgr = new ThreadChunkManager;
		AcquireSRWLockExclusive(&_srwlVecThreadMgr);
		_vecThreadMgr.push_back(pMgr);
		ReleaseSRWLockExclusive(&_srwlVecThreadMgr);
		TlsSetValue(_tlsIndex, pMgr);
	}

	// 스레드의 object alloc count 감소
	pMgr->_objectAllocCount--;

}




template <typename _Ty>
void CMemoryPoolTLS<_Ty>::FreeChunk(Chunk* pChunk)
{
	pChunk->_isNew = false;

	Chunk* pNextChunk;
	AcquireSRWLockExclusive(&_srwlPool);
	pNextChunk = _pFreeChunk;
	_pFreeChunk = pChunk;
	pChunk->_pNext = pNextChunk;

	_numFreeChunk++;
	_numAllocChunk--;
	ReleaseSRWLockExclusive(&_srwlPool);
}




template <typename _Ty>
void CMemoryPoolTLS<_Ty>::Crash()
{
	int* p = nullptr;
	*p = 0;
}





}