#pragma once

#include <iserverentity.h>

#include "RemoveSourceSpecifics.h"

#include "Helpers.h"

// CBaseEntity inherits IServerEntity, but it's impossible to say that through forward-declarations alone.
// Use this empty class to give a bit more info.
class CBaseEntity : public IServerEntity
{
public:
    inline bool IsPlayer() const
    {
        return Cosmo::get_function_from_vtable_index<__attribute__((fastcall)) bool (*)(const CBaseEntity*)>(this,
                                                                                                             81)(this);
    }
};
