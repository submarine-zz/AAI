// -------------------------------------------------------------------------
// AAI
//
// A skirmish AI for the Spring engine.
// Copyright Alexander Seizinger
//
// Released under GPL license: see LICENSE.html for more information.
// -------------------------------------------------------------------------

#include "AAIHelperFunctions.h"

namespace AAIHelperFunctions
{
	ThreatByTargetType DetermineHighestThreat(const MobileTargetTypeValues& threatByTargetType)
	{
		ThreatByTargetType highestThreat(0.0f, ETargetType::UNKNOWN);

		for(const auto& targetType : AAITargetType::m_mobileTargetTypes)
		{
			if(threatByTargetType[targetType] > highestThreat.Threat())
			{
				highestThreat.Threat()     = threatByTargetType[targetType];
				highestThreat.TargetType() = targetType;
			}
		}

		return highestThreat;
	}
}
