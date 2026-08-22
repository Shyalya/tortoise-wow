#ifndef DC_COMPAT_POSITION_H
#define DC_COMPAT_POSITION_H

// AzerothCore keeps Position in its own header; here it lives in
// SharedDefines.h. A forwarder only - the type itself is the core's, extended
// there with the AzerothCore accessors this module needs. Do not define a
// Position here: this header ends up ahead of the core's own includes in some
// translation units, and a second definition breaks Object.h.
#include "SharedDefines.h"

#endif
