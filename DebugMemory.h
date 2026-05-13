#pragma once

#ifdef _DEBUG

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

class DebugMemory
{
public:
    static void EnableLeakCheck()
    {
        _CrtSetDbgFlag(
            _CRTDBG_ALLOC_MEM_DF |
            _CRTDBG_LEAK_CHECK_DF
        );
    }
};

#endif