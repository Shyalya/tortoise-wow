#ifndef DC_COMPAT_OPTIONAL_H
#define DC_COMPAT_OPTIONAL_H

// AzerothCore aliases std::optional as Optional.
#include <optional>

template<class T> using Optional = std::optional<T>;

#endif
