#pragma once



class CRingbuffer
{
public:
	char* _buf;
	__int64 _size;

	char* _front;    // 데이터의 시작 위치
	char* _rear;     // 데이터의 마지막 위치 + 1

public:
	CRingbuffer(void);
	CRingbuffer(int size);
	~CRingbuffer();

public:
	/* getter */
	int GetSize(void) { return (int)_size; }
	char* GetFront(void) { return _front; }
	char* GetRear(void) { return _rear; }
	char* GetBufPtr(void) { return _buf; }

	/* get size */
	int GetFreeSize(void);
	int GetUseSize(void);
	int GetDirectFreeSize(void);
	int GetDirectUseSize(void);

	/* enqueue, dequeue */
	int Enqueue(const char* src, int len);
	int Dequeue(char* dest, int len);
	int Peek(char* dest, int len);
	int Peek(char* dest, int len, int useSize, int directUseSize);

	/* pointer control */
	void MoveFront(int dist);
	void MoveRear(int dist);

	/* buffer control */
	void Clear(void);
	int Resize(int size);


};



