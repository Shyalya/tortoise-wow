#pragma once

// Generic transport capability for network-backed and synthetic sessions.
// PlayerBots is one consumer; the core does not assign bot meaning to it.
enum class SessionTransport : unsigned char
{
    Network,
    Headless,
};
