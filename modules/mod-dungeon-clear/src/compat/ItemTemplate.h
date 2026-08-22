#ifndef DC_COMPAT_ITEMTEMPLATE_H
#define DC_COMPAT_ITEMTEMPLATE_H

// AzerothCore's ItemTemplate is Penqle's ItemPrototype. Same role, and the
// fields this module reads carry the same names.
#include "Objects/ItemPrototype.h"

using ItemTemplate = ItemPrototype;

#endif
