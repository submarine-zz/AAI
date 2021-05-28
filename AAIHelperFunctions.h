// -------------------------------------------------------------------------
// AAI
//
// A skirmish AI for the Spring engine.
// Copyright Alexander Seizinger
//
// Released under GPL license: see LICENSE.html for more information.
// -------------------------------------------------------------------------

#ifndef AAI_HELPER_FUNCTIONS_H
#define AAI_HELPER_FUNCTIONS_H

#include "AAITypes.h"

namespace AAIHelperFunctions
{
	//! @brief Returns the highest threat (value + corresponding target type) from the given array of values for the individual target types
	ThreatByTargetType DetermineHighestThreat(const MobileTargetTypeValues& threatByTargetType);
}

#endif
