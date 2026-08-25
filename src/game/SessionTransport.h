#pragma once

// Generic transport capability for network-backed and synthetic sessions.
// The core exposes transport capability, not the identity or purpose of the
// code driving a synthetic session.
enum class SessionTransport : unsigned char
{
    Network,
    Headless,
};
