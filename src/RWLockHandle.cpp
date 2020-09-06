#include "stdafx.h"
#include "RWLockHandle.h"
#include <Windows.h>


RWLockHandle::RWLockHandle()
{
	InitializeSRWLock(&lock);
}

void RWLockHandle::acquireRWLockShared()
{
	AcquireSRWLockShared(&lock);
	
}

void RWLockHandle::acquireRWLockExclusive()
{
	AcquireSRWLockExclusive(&lock);
}

void RWLockHandle::releaseRWLockExclusive()
{
	ReleaseSRWLockExclusive(&lock);
}

void RWLockHandle::releaseRWLockShared()
{
	ReleaseSRWLockShared(&lock);
}


RWLockHandle::~RWLockHandle()
{
}
