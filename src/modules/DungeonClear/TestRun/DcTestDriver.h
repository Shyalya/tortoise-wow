#pragma once
#include <string>

class ChatHandler;

namespace DcTestDriver
{
    bool Handle(ChatHandler* handler, std::string const& args);
}
