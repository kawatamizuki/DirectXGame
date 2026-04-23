#pragma once

#include <string>

namespace Debug
{
    //エラーが発生したことを表示する。エラーの種類で使い分ける
    void Log(const std::string& message);
    void Info(const std::string& message);
    void Warning(const std::string& message);
    void Error(const std::string& message);
    void Assert(bool condition, const std::string& message);
}