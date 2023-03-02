#pragma once

#include <stdlib.h>

template <typename _Kty, typename _Vty>
class CRedBlackTree
{
private:
	enum class eColor
	{
		BLACK = 0,
		RED
	};

	struct Pair
	{
		_Kty first;
		_Vty second;
	};

	struct Node
	{
		Pair pair;
		eColor color;
		Node* pParent;
		Node* pLeft;
		Node* pRight;

		Node(_Kty inKey, _Vty inVal, eColor color, Node* pParent)
			: pair{ inKey, inVal }, color(color), pParent(pParent), pLeft(_pNil), pRight(_pNil)
		{}
	};

	Node* _pRoot;
	size_t _numOfNodes;

	// Nil node
	static Node* _pNil;


public:
	CRedBlackTree();
	~CRedBlackTree();
	
public:
	size_t Size() { return _numOfNodes; }  // size 반환

	bool Insert(_Kty key, _Vty val);  // insert
	
	class iterator;
	iterator Begin();                       // 중위순회기준 첫 노드 iterator 반환
	iterator End();                         // end iterator 반환
	iterator Find(const _Kty& key);         // key에 해당하는 iterator 반환
	iterator Erase(const iterator& iter);   // 노드 삭제

	void Clear();                           // 모든 노드 삭제

	iterator GetRoot() { return iterator(this, _pRoot); }  // root 반환

private:
	Node* _getInorderSuccessor(Node* pNode);    // 중위 후속자 반환
	Node* _delete(Node* pDeleteNode);           // 노드 삭제
	void _leftRotate(Node* pTargetNode);        // 좌회전
	void _rightRotate(Node* pTargetNode);       // 우회전
	void _insertFixUp(Node* pInsertedNode);     // insert 후 밸런스맞추기
	void _deleteFixUp(Node* pTargetNode);       // delete 후 밸런스맞추기
	void _clear(Node* pNode);                   // 모든 노드 삭제



public:
	// 중위순회 iterator
	class iterator {
		friend class CRedBlackTree;

	protected:
		CRedBlackTree* _pOwner;
		Node* _pNode; // iterator가 가르키는 노드. null 인 경우 end iterator

	public:
		iterator()
			: _pOwner(nullptr), _pNode(nullptr)
		{}
		iterator(CRedBlackTree* pOwner, Node* pNode)
			: _pOwner(pOwner), _pNode(pNode)
		{}

	public:
		bool operator == (const iterator& other) { return (_pOwner == _pOwner && _pNode == other._pNode); }
		bool operator != (const iterator& other) { return !(*this == other); }
		const Pair& operator * () const { return _pNode->pair; }     // * 연산자로 <key, data> pair에 접근할 수 있게 한다.
		const Pair* operator -> () const { return &(_pNode->pair); } // -> 연산자로 <key, data> pair에 접근할 수 있게 한다.

		// 중위순회 기준으로 다음 노드로 이동
		iterator& operator ++ () {
			_pNode = _pOwner->_getInorderSuccessor(_pNode);
			return (*this);
		}

	public:
		iterator GetParent() const { return iterator(_pOwner, _pNode->pParent); }
		iterator GetLeftChild() const { return iterator(_pOwner, _pNode->pLeft); }
		iterator GetRightChild() const { return iterator(_pOwner, _pNode->pRight); }

		bool IsRed() const { return _pNode->color == eColor::RED; }
		bool IsBlack() const { return _pNode->color == eColor::BLACK; }
		bool IsNil() const { return _pNode == _pNil; }
		bool IsLeaf() const { return _pNode != _pNil && _pNode->pLeft == _pNil && _pNode->pRight == _pNil; }
		bool IsRoot() const { return _pNode == _pOwner->_pRoot; }
		bool IsLeftChild() const { return _pNode == _pNode->pParent->pLeft; }
		bool IsRightChild() const { return _pNode == _pNode->pParent->pRight; }
	};

};

// static 멤버 초기화
template <typename _Kty, typename _Vty>
typename CRedBlackTree<_Kty, _Vty>::Node* CRedBlackTree<_Kty, _Vty>::_pNil = nullptr;


// 생성자
template <typename _Kty, typename _Vty>
CRedBlackTree<_Kty, _Vty>::CRedBlackTree()
	: _numOfNodes(0)
{
	// Nil 노드 생성
	if(_pNil == nullptr)
	{ 
		_pNil = (Node*)malloc(sizeof(Node));
		_pNil->color = eColor::BLACK;
		_pNil->pParent = nullptr;
		_pNil->pLeft = nullptr;
		_pNil->pRight = nullptr;
	}

	// root 초기화
	_pRoot = _pNil;
}


// 소멸자
template <typename _Kty, typename _Vty>
CRedBlackTree<_Kty, _Vty>::~CRedBlackTree()
{
	Clear();
	delete _pNil;
}



template <typename _Kty, typename _Vty>
bool CRedBlackTree<_Kty, _Vty>::Insert(_Kty key, _Vty val)
{
	// root가 Nil일 경우 root에 삽입
	if (_pRoot == _pNil)
	{
		_pRoot = new Node(key, val, eColor::BLACK, _pNil);
		_numOfNodes++;
		return true;
	}

	Node* pNode = _pRoot;
	while (true)
	{
		// key가 현재노드보다 작을 경우
		if (key < pNode->pair.first)
		{
			// 왼쪽 자식이 Nil일 경우 왼쪽 자식에 삽입한다.
			if (pNode->pLeft == _pNil)
			{
				pNode->pLeft = new Node(key, val, eColor::RED, pNode);
				_numOfNodes++;
				_insertFixUp(pNode->pLeft); // re-balance
				return true;
			}
			// 그렇지 않으면 왼쪽 자식 노드로 이동
			else
			{
				pNode = pNode->pLeft;
			}
		}
		// key가 현재노드보다 클 경우
		else if (key > pNode->pair.first)
		{
			// 오른쪽 자식이 Nil일 경우 오른쪽 자식에 삽입한다.
			if (pNode->pRight == _pNil)
			{
				pNode->pRight = new Node(key, val, eColor::RED, pNode);
				_numOfNodes++;
				_insertFixUp(pNode->pRight); // re-balance
				return true;
			}
			// 그렇지 않으면 오른쪽 자식 노드로 이동
			else
			{
				pNode = pNode->pRight;
			}
		}
		// key가 현재 노드와 같을 경우 값 복사
		else
		{
			pNode->pair.second = val;
			return false;
		}
	}
}




// 중위순회 기준 가장 첫 node를 가르키는 iterator 반환
// 가장 왼쪽 아래 node를 가르키는 iterator를 반환한다.
template<typename _Kty, typename _Vty>
typename CRedBlackTree<_Kty, _Vty>::iterator CRedBlackTree<_Kty, _Vty>::Begin()
{
	Node* pNode = _pRoot;
	if (pNode == _pNil)
	{
		return End();
	}

	while (true)
	{
		if (pNode->pLeft == _pNil)
			break;
		pNode = pNode->pLeft;
	}
	return iterator(this, pNode);
}

// end iterator 반환
template<typename _Kty, typename _Vty>
typename CRedBlackTree<_Kty, _Vty>::iterator CRedBlackTree<_Kty, _Vty>::End()
{
	return iterator(this, nullptr);
}


// tree에서 key값에 해당하는 node를 찾아서 iterator로 반환함.
template<typename _Kty, typename _Vty>
typename CRedBlackTree<_Kty, _Vty>::iterator CRedBlackTree<_Kty, _Vty>::Find(const _Kty& key)
{
	Node* pNode = _pRoot;
	while (true)
	{
		if (pNode == _pNil)
		{
			return iterator(this, nullptr); // 만약 현재 node가 Nil일 경우 end iterator 반환
		}
		else if (key < pNode->pair.first)
		{
			pNode = pNode->pLeft;
		}
		else if (key > pNode->pair.first)
		{
			pNode = pNode->pRight;
		}
		else if (key == pNode->pair.first)
		{
			return iterator(this, pNode); // key 값이 같다면 iterator 반환
		}
	}
}


// 전달받은 iterator에 해당하는 노드를 삭제한다.
template<typename _Kty, typename _Vty>
typename CRedBlackTree<_Kty, _Vty>::iterator CRedBlackTree<_Kty, _Vty>::Erase(const iterator& iter)
{
	if (End() == iter)
		return End();

	Node* pSuccessor = _delete(iter._pNode); // 노드를 삭제하고 삭제한 노드의 중위후속자를 가르키는 노드를 받는다.
	return iterator(this, pSuccessor);
}


// 모든 노드 삭제
template<typename _Kty, typename _Vty>
void CRedBlackTree<_Kty, _Vty>::Clear()
{
	_clear(_pRoot);
	_pRoot = _pNil;
	_numOfNodes = 0;
}





// 전달받은 노드의 중위후속자를 찾는다.
template <typename _Kty, typename _Vty>
typename CRedBlackTree<_Kty, _Vty>::Node* CRedBlackTree<_Kty, _Vty>::_getInorderSuccessor(Node* pNode)
{
	Node* pSuccessor;
	// 오른쪽 자식이 있는 경우, 오른쪽 자식으로 가서 왼쪽 자식이 없을 때까지 내려감
	if (pNode->pRight != _pNil)
	{
		pSuccessor = pNode->pRight;
		while (pSuccessor->pLeft != _pNil)
		{
			pSuccessor = pSuccessor->pLeft;
		}
	}
	// 오른쪽 자식이 없는 경우, 자신이 부모의 왼쪽자식일 때 까지 위로 올라감. 그때 부모가 중위후속자
	else {
		pSuccessor = pNode;
		while (true)
		{
			// 만약 root node까지 올라갔다면 다음 후속자는 없음
			if (pSuccessor->pParent == _pNil)
			{
				pSuccessor = nullptr;
				break;
			}
			// 만약 현재 노드가 부모의 왼쪽 자식이면 부모가 후속자임
			else if (pSuccessor->pParent->pLeft == pSuccessor)
			{
				pSuccessor = pSuccessor->pParent;
				break;
			}
			// 그렇지 않으면 부모 노드로 넘어감
			else
			{
				pSuccessor = pSuccessor->pParent;
			}
		}
	}
	return pSuccessor;
}





// 노드를 삭제하고 삭제한 노드의 중위후속자를 가르키는 노드를 반환한다.
template <typename _Kty, typename _Vty>
typename CRedBlackTree<_Kty, _Vty>::Node* CRedBlackTree<_Kty, _Vty>::_delete(Node* pDeleteNode)
{
	// 삭제시킬 노드의 중위 후속자 노드를 찾아둔다.
	Node* pSuccessor = _getInorderSuccessor(pDeleteNode);

	// 자식이 2개인 경우
	if (pDeleteNode->pLeft != _pNil && pDeleteNode->pRight != _pNil)
	{
		// 삭제될 자리에 중위 후속자의 값을 복사시킨다.
		pDeleteNode->pair = pSuccessor->pair;
		// 중위 후속자 노드를 삭제(fix up은 _delete 함수 내에서 수행됨)
		_delete(pSuccessor);
		// 값을 복사한 노드가 중위 후속자가 됨
		pSuccessor = pDeleteNode;
	}

	// 자식이 1개이고 왼쪽 자식만 있는 경우
	else if (pDeleteNode->pLeft != _pNil)
	{
		// 삭제할 노드가 root인 경우 자식노드를 루트로 만든다.
		if (pDeleteNode == _pRoot)
		{
			_pRoot = pDeleteNode->pLeft;
			_pRoot->pParent = _pNil;
		}
		// 내가 부모의 왼쪽 자식일 경우
		else if (pDeleteNode == pDeleteNode->pParent->pLeft)
		{
			pDeleteNode->pParent->pLeft = pDeleteNode->pLeft;
			pDeleteNode->pLeft->pParent = pDeleteNode->pParent;
		}
		// 내가 부모의 오른쪽 자식일 경우
		else
		{
			pDeleteNode->pParent->pRight = pDeleteNode->pLeft;
			pDeleteNode->pLeft->pParent = pDeleteNode->pParent;
		}

		// 삭제할 노드의 색이 BLACK 이라면 fix up
		if (pDeleteNode->color == eColor::BLACK)
		{
			_deleteFixUp(pDeleteNode->pLeft);
		}
		delete pDeleteNode;
		_numOfNodes--;
	}

	// 자식이 1개이고 오른쪽 자식만 있는 경우
	else if (pDeleteNode->pRight != _pNil)
	{
		// 삭제할 노드가 root인 경우 자식노드를 루트로 만든다.
		if (pDeleteNode == _pRoot)
		{
			_pRoot = pDeleteNode->pRight;
			_pRoot->pParent = _pNil;
		}
		// 내가 부모의 왼쪽 자식일 경우
		else if (pDeleteNode == pDeleteNode->pParent->pLeft)
		{
			pDeleteNode->pParent->pLeft = pDeleteNode->pRight;
			pDeleteNode->pRight->pParent = pDeleteNode->pParent;
		}
		// 내가 부모의 오른쪽 자식일 경우
		else
		{
			pDeleteNode->pParent->pRight = pDeleteNode->pRight;
			pDeleteNode->pRight->pParent = pDeleteNode->pParent;
		}

		// 삭제할 노드의 색이 BLACK 이라면 fix up
		if (pDeleteNode->color == eColor::BLACK)
		{
			_deleteFixUp(pDeleteNode->pRight);
		}
		delete pDeleteNode;
		_numOfNodes--;
	}

	// 자식이 하나도 없는 경우
	else {
		// 삭제할 노드가 root인 경우 root 삭제
		if (pDeleteNode == _pRoot)
		{
			_pRoot = _pNil;
			delete pDeleteNode;
			_numOfNodes--;
			return pSuccessor;
		}
		// 만약 자신이 왼쪽 자식일 경우 부모노드의 왼쪽자식을 Nil로 변경한다
		else if (pDeleteNode == pDeleteNode->pParent->pLeft)
		{
			pDeleteNode->pParent->pLeft = _pNil;
		}
		// 만약 자신이 오른쪽 자식일 경우 부모노드의 오른쪽자식을 Nil로 변경한다
		else
		{
			pDeleteNode->pParent->pRight = _pNil;
		}

		// 삭제할 노드의 색이 BLACK 이라면 fix up
		if (pDeleteNode->color == eColor::BLACK)
		{
			_pNil->pParent = pDeleteNode->pParent;
			_deleteFixUp(_pNil);
		}
		delete pDeleteNode;
		_numOfNodes--;
	}

	return pSuccessor;
}






// target을 기준으로 좌회전한다.
template <typename _Kty, typename _Vty>
void CRedBlackTree<_Kty, _Vty>::_leftRotate(Node* pTargetNode)
{
	Node* pRightSubtree = pTargetNode->pRight;

	// target의 오른쪽 자식을 right subtree의 왼쪽 자식으로 한다.
	pTargetNode->pRight = pRightSubtree->pLeft;
	if (pRightSubtree->pLeft != _pNil)
	{
		pRightSubtree->pLeft->pParent = pTargetNode;
	}
	// right subtree의 부모를 target의 부모로 바꾼다.
	pRightSubtree->pParent = pTargetNode->pParent;

	// target이 root 였다면 right subtree가 root가 됨.
	if (pTargetNode == _pRoot)
	{
		_pRoot = pRightSubtree;
	}
	// 그렇지 않고 target이 왼쪽 자식이었다면 target의 부모의 왼쪽자식은 right subtree가 됨
	else if (pTargetNode == pTargetNode->pParent->pLeft)
	{
		pTargetNode->pParent->pLeft = pRightSubtree;
	}
	// 그렇지 않고 target이 오른쪽 자식이었다면 target의 부모의 오른쪽자식은 right subtree가 됨
	else
	{
		pTargetNode->pParent->pRight = pRightSubtree;
	}

	// right subtree의 왼쪽 자식을 target으로, target의 부모를 right subtree로 만듬
	pRightSubtree->pLeft = pTargetNode;
	pTargetNode->pParent = pRightSubtree;
}





// target을 기준으로 우회전한다.
template <typename _Kty, typename _Vty>
void CRedBlackTree<_Kty, _Vty>::_rightRotate(Node* pTargetNode)
{
	Node* pLeftSubtree = pTargetNode->pLeft;

	// target의 왼쪽 자식을 left subtree의 오른쪽 자식으로 한다.
	pTargetNode->pLeft = pLeftSubtree->pRight;
	if (pLeftSubtree->pRight != _pNil)
	{
		pLeftSubtree->pRight->pParent = pTargetNode;
	}
	// left subtree의 부모를 target의 부모로 바꾼다.
	pLeftSubtree->pParent = pTargetNode->pParent;

	// target이 root 였다면 left subtree가 root가 됨.
	if (pTargetNode == _pRoot)
	{
		_pRoot = pLeftSubtree;
	}
	// 그렇지 않고 target이 왼쪽 자식이었다면 target의 부모의 왼쪽자식은 left subtree가 됨
	else if (pTargetNode == pTargetNode->pParent->pLeft)
	{
		pTargetNode->pParent->pLeft = pLeftSubtree;
	}
	// 그렇지 않고 target이 오른쪽 자식이었다면 target의 부모의 오른쪽자식은 left subtree가 됨
	else
	{
		pTargetNode->pParent->pRight = pLeftSubtree;
	}

	// left subtree의 오른쪽 자식을 target으로, target의 부모를 left subtree로 만듬
	pLeftSubtree->pRight = pTargetNode;
	pTargetNode->pParent = pLeftSubtree;
}





// insert 한 뒤 tree의 밸런스를 맞춘다.
template <typename _Kty, typename _Vty>
void CRedBlackTree<_Kty, _Vty>::_insertFixUp(Node* pInsertedNode)
{
	Node* pTargetNode = pInsertedNode;
	Node* pUncleNode;
	// 부모가 RED 이면 계속 수행함
	while (pTargetNode->pParent->color == eColor::RED)
	{
		// 부모가 조부모의 왼쪽 자식일 경우
		if(pTargetNode->pParent == pTargetNode->pParent->pParent->pLeft)
		{
			// 삼촌 노드를 저장해둠
			pUncleNode = pTargetNode->pParent->pParent->pRight;
			// case 1: 삼촌 노드가 RED임
			if (pUncleNode->color == eColor::RED)
			{
				// 부모, 삼촌, 조부모 노드의 색을 바꿈
				pTargetNode->pParent->color = eColor::BLACK;
				pUncleNode->color = eColor::BLACK;
				pTargetNode->pParent->pParent->color = eColor::RED;
				// target을 조부모로 바꿈
				pTargetNode = pTargetNode->pParent->pParent;
			}
			// case 2: 삼촌 노드가 BLACK 이고, target이 부모와 반대방향임
			else if (pTargetNode == pTargetNode->pParent->pRight)
			{
				// 부모를 기준으로 left rotate
				pTargetNode = pTargetNode->pParent;
				_leftRotate(pTargetNode);
			}
			// case 3: 삼촌 노드가 BLACK 이고, target이 부모와 같은방향임
			else
			{
			// 부모와 조부모의 색을 바꿈
			pTargetNode->pParent->color = eColor::BLACK;
			pTargetNode->pParent->pParent->color = eColor::RED;
			// 조부모를 기준으로 right rotate
			_rightRotate(pTargetNode->pParent->pParent);
			}
		}
		// 부모가 조부모의 오른쪽 자식일 경우
		else
		{
		// 삼촌 노드를 저장해둠
		pUncleNode = pTargetNode->pParent->pParent->pLeft;
		// case 1: 삼촌 노드가 RED임
		if (pUncleNode->color == eColor::RED)
		{
			// 부모, 삼촌, 조부모 노드의 색을 바꿈
			pTargetNode->pParent->color = eColor::BLACK;
			pUncleNode->color = eColor::BLACK;
			pTargetNode->pParent->pParent->color = eColor::RED;
			// target을 조부모로 바꿈
			pTargetNode = pTargetNode->pParent->pParent;
		}
		// case 2: 삼촌 노드가 BLACK 이고, target이 부모와 반대방향임
		else if (pTargetNode == pTargetNode->pParent->pLeft)
		{
			// 부모를 기준으로 right rotate
			pTargetNode = pTargetNode->pParent;
			_rightRotate(pTargetNode);
		}
		// case 3: 삼촌 노드가 BLACK 이고, target이 부모와 같은방향임
		else
		{
			// 부모와 조부모의 색을 바꿈
			pTargetNode->pParent->color = eColor::BLACK;
			pTargetNode->pParent->pParent->color = eColor::RED;
			// 조부모를 기준으로 left rotate
			_leftRotate(pTargetNode->pParent->pParent);
		}
		}
	}

	_pRoot->color = eColor::BLACK;
}


// delete 한 뒤 tree의 밸런스를 맞춘다.
template <typename _Kty, typename _Vty>
void CRedBlackTree<_Kty, _Vty>::_deleteFixUp(Node* pTargetNode)
{
	Node* pTarget = pTargetNode;
	Node* pParent;
	Node* pSibling;

	// case 1. target이 레드
	if (pTarget->color == eColor::RED)
	{
		pTarget->color = eColor::BLACK;
		return;
	}

	if (pTarget->pParent == _pNil)
		return;

	// case 2.1 target이 블랙이고 형제가 레드임
	pParent = pTarget->pParent;
	if (pTarget == pParent->pLeft)
	{
		pSibling = pParent->pRight;
		if (pSibling->color == eColor::RED)
		{
			pParent->color = eColor::RED;
			pSibling->color = eColor::BLACK;
			_leftRotate(pParent);
			pSibling = pParent->pRight;
		}
	}
	else
	{
		pSibling = pParent->pLeft;
		if (pSibling->color == eColor::RED)
		{
			pParent->color = eColor::RED;
			pSibling->color = eColor::BLACK;
			_rightRotate(pParent);
			pSibling = pParent->pLeft;
		}
	}

	// case 2.2 target이 블랙이고 형제가 블랙이고 형제의 양쪽 자식이 모두 블랙
	if (pParent->color == eColor::BLACK
		&& pSibling->color == eColor::BLACK
		&& pSibling->pLeft->color == eColor::BLACK
		&& pSibling->pRight->color == eColor::BLACK)
	{
		pSibling->color = eColor::RED;
		_deleteFixUp(pParent);
		return;
	}


	// case 2.3 target의 부모가 레드이고 형제가 블랙이고 형제의 왼쪽자식이 블랙, 오른쪽자식은 블랙
	if (pParent->color == eColor::RED
		&& pSibling->color == eColor::BLACK
		&& pSibling->pLeft->color == eColor::BLACK
		&& pSibling->pRight->color == eColor::BLACK)
	{
		pSibling->color = eColor::RED;
		pParent->color = eColor::BLACK;
		return;
	}


	// case 2.4 target의 형제가 블랙, 형제의 왼쪽 자식이 레드, 형제의 오른쪽 자식이 블랙
	if (pTarget == pParent->pLeft
		&& pSibling->color == eColor::BLACK
		&& pSibling->pLeft->color == eColor::RED
		&& pSibling->pRight->color == eColor::BLACK)
	{
		pSibling->color = eColor::RED;
		pSibling->pLeft->color = eColor::BLACK;
		_rightRotate(pSibling);
		pSibling = pParent->pRight;
	}
	else if (pTarget == pParent->pRight
		&& pSibling->color == eColor::BLACK
		&& pSibling->pRight->color == eColor::RED
		&& pSibling->pLeft->color == eColor::BLACK)
	{
		pSibling->color = eColor::RED;
		pSibling->pRight->color = eColor::BLACK;
		_leftRotate(pSibling);
		pSibling = pParent->pLeft;
	}

	// case 2.5 target의 형제가 블랙, 형제의 오른쪽 자식이 레드
	if (pTarget == pParent->pLeft
		&& pSibling->color == eColor::BLACK
		&& pSibling->pRight->color == eColor::RED)
	{
		pSibling->color = pParent->color;
		pParent->color = eColor::BLACK;
		pSibling->pRight->color = eColor::BLACK;
		_leftRotate(pParent);
	}
	else if (pTarget == pParent->pRight
		&& pSibling->color == eColor::BLACK
		&& pSibling->pLeft->color == eColor::RED)
	{
		pSibling->color = pParent->color;
		pParent->color = eColor::BLACK;
		pSibling->pLeft->color = eColor::BLACK;
		_rightRotate(pParent);
	}

}




// 모든 노드 삭제
template<typename _Kty, typename _Vty>
void CRedBlackTree<_Kty, _Vty>::_clear(Node* pNode)
{
	if (pNode == _pNil)
		return;

	_clear(pNode->pLeft);
	_clear(pNode->pRight);
	delete pNode;
}
