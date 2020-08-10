// -------------------------------------------------------------------------
// AAI
//
// A skirmish AI for the Spring engine.
// Copyright Alexander Seizinger
//
// Released under GPL license: see LICENSE.html for more information.
// -------------------------------------------------------------------------

#ifndef AAI_ATTACKMANAGER_H
#define AAI_ATTACKMANAGER_H

#include "aidef.h"
#include <set>
#include <list>
#include <vector>

using namespace std;

class AAI;
class AAIBrain;
class AAIBuildTable;
class AAIMap;
class AAIAttack;
class AAISector;


class AAIAttackManager
{
public:
	AAIAttackManager(AAI *ai, int continents);
	~AAIAttackManager(void);

	void CheckAttack(AAIAttack *attack);

	// true if units have sufficient combat power to face mobile units in dest
	bool SufficientCombatPowerAt(const AAISector *dest, set<AAIGroup*> *combat_groups, float aggressiveness);

	// true if combat groups have suffiecient attack power to face stationary defences
	bool SufficientAttackPowerVS(AAISector *dest, set<AAIGroup*> *combat_groups, float aggressiveness);

	void GetNextDest(AAIAttack *attack);

	void Update();
private:

	void LaunchAttack();
	void StopAttack(AAIAttack *attack);

	list<AAIAttack*> attacks;

	// array stores number of combat groups per category (for SufficientAttackPowerVS(..) )
	vector<int> available_combat_cat;

	vector< list<AAIGroup*> > available_combat_groups_continent;
	vector< list<AAIGroup*> > available_aa_groups_continent;

	list<AAIGroup*> available_combat_groups_global;
	list<AAIGroup*> available_aa_groups_global;

	// stores total attack power for different unit types on each continent
	vector< vector<float> > attack_power_continent;
	vector<float> attack_power_global;

	AAI *ai;
};

#endif
