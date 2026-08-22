#ifndef DC_COMPAT_GAMEOBJECTDATA_H
#define DC_COMPAT_GAMEOBJECTDATA_H

// Spawn data lives with the GameObject class here.
#include "Objects/GameObject.h"

// AzerothCore calls the template GameObjectTemplate; here it is
// GameObjectInfo.
using GameObjectTemplate = GameObjectInfo;

#endif
