#include "Debug.h"
#include <Windows.h>

namespace
{
    void Output(const std::string& prefix, const std::string& message)
    {
#ifdef _DEBUG
        std::string text = prefix + message + "\n";
        OutputDebugStringA(text.c_str());
#endif
    }
}

namespace Debug
{
    void Log(const std::string& message)
    {
        Output("[LOG] ", message);
    }

    void Info(const std::string& message)
    {
        Output("[INFO] ", message);
    }

    void Warning(const std::string& message)
    {
        Output("[WARN] ", message);
    }

    void Error(const std::string& message)
    {
#ifdef _DEBUG
        Output("[ERROR] ", message);
#endif
    }

    void Assert(bool condition, const std::string& message)
    {
#ifdef _DEBUG
        if (!condition)
        {
            Output("[ASSERT] ", message);
            __debugbreak();
        }
#endif
    }
}