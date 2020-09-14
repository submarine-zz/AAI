// -------------------------------------------------------------------------
// AAI
//
// A skirmish AI for the Spring engine.
// Copyright Alexander Seizinger
//
// Released under GPL license: see LICENSE.html for more information.
// -------------------------------------------------------------------------

#ifndef AAI_ATTACK_H
#define AAI_ATTACK_H

#include <set>
#include "AAITypes.h"

class AAI;
class AAISector;
class AAIGroup;

class AAIAttack
{
public:
	AAIAttack(AAI *ai);
	~AAIAttack(void);

	//! @brief Adds group to the attack, returns whether it has been successful
	bool AddGroup(AAIGroup *group);

	//! @brief Removes group from the attack (e.g. if group is deleted)
	void RemoveGroup(AAIGroup *group);

	//! @brief Returns true if attack is considered to have failed
	bool CheckIfFailed();

	//! @brief Checks if units have sufficient combat power against mobile enemy units assumed to be at destination
	bool SufficientCombatPowerAt(const AAISector *sector, float aggressiveness) const;

	//! @brief Checks if units in given combat unit groups have sufficient attack power against enemy stationary defences
	bool SufficientCombatPowerToAttackSector(const AAISector *sector, float aggressiveness) const;

	//! @brief Orders units to attack specidied sector
	void AttackSector(const AAISector *sector);

	//! @brief Orders all units involved to retreat
	void StopAttack();

	//! @brief Returns the movement types of the units participating in this attack
	AAIMovementType GetMovementTypeOfAssignedUnits() const;

	//! @brief Determines how many units of which target type participate in attack
	void DetermineTargetTypeOfInvolvedUnits(AAIValuesForMobileTargetTypes& targetTypesOfUnits) const;

	//! The target sector
	const AAISector* m_attackDestination;

	//! tick when last attack order has been given (to prevent overflow when unit gets stuck and sends "unit idel" all the time)
	int m_lastAttackOrderInFrame;

	//! combat unit groups that are part of this attack
	std::set<AAIGroup*> m_combatUnitGroups;
private:
	//! anti air unit groups that are part of this attack
	std::set<AAIGroup*> m_antiAirUnitGroups;
	
	AAI *ai;
};

#endif
