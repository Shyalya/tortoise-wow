#pragma once

#include "BaseAuthorizer.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

namespace HttpApi
{
    class ApiKeyAuthorizer final : public BaseAuthorizer
    {
    public:
        ApiKeyAuthorizer(const std::string& key) : _key(key) {}

        static bool IsStrongKey(const std::string& key)
        {
            constexpr std::size_t MinimumKeyLength = 32;

            if (key.size() < MinimumKeyLength)
                return false;

            // API keys are sent in an HTTP header. Reject whitespace and control
            // characters so a configured key cannot be malformed in transit.
            if (!std::all_of(key.begin(), key.end(), [](unsigned char character)
                {
                    return character >= 0x21 && character <= 0x7e;
                }))
            {
                return false;
            }

            // A long repeated string is not a useful secret.
            std::string distinctCharacters;
            for (const char character : key)
            {
                if (distinctCharacters.find(character) == std::string::npos)
                    distinctCharacters.push_back(character);
            }

            return distinctCharacters.size() >= 8;
        }

        bool IsAuthorized(const httplib::Request& res, httplib::Response& resp) const override
        {
            std::string apiKey = res.get_header_value("X-API-Key");
            if (IsStrongKey(_key) && ConstantTimeEquals(apiKey, _key))
                return true;

            resp.status = 401;
            resp.set_header("WWW-Authenticate", "ApiKey");
            resp.set_content("Unauthorized.", "text/plain");
            return false;
        }

    private:
        static bool ConstantTimeEquals(const std::string& left, const std::string& right)
        {
            if (left.size() != right.size())
                return false;

            unsigned char difference = 0;
            for (std::size_t index = 0; index < left.size(); ++index)
                difference |= static_cast<unsigned char>(static_cast<unsigned char>(left[index]) ^ static_cast<unsigned char>(right[index]));

            return difference == 0;
        }

        std::string _key;
    };
}
