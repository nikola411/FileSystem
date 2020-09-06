#pragma once
#include <Windows.h>
class RWLockHandle
{
private:
	SRWLOCK lock;

public:
	RWLockHandle();
	
	void acquireRWLockShared();
	void acquireRWLockExclusive();
	void releaseRWLockExclusive();
	void releaseRWLockShared();
	
	~RWLockHandle();
};

