// ------------------------------------------------------------------------
// AAI
//
// A skirmish AI for the Spring engine.
// Copyright Alexander Seizinger
//
// Released under GPL license: see LICENSE.html for more information.
// ------------------------------------------------------------------------

#include "AAISector.h"
#include "AAI.h"
#include "AAIBuildTable.h"
#include "AAIBrain.h"
#include "AAIConfig.h"
#include "AAIMap.h"

#include "LegacyCpp/IGlobalAICallback.h"
#include "LegacyCpp/UnitDef.h"
using namespace springLegacyAI;


AAISector::AAISector()
{
}

AAISector::~AAISector(void)
{
	attacked_by_this_game.clear();
	attacked_by_learned.clear();

	my_stat_combat_power.clear();
	enemy_stat_combat_power.clear();

	my_mobile_combat_power.clear();
	enemy_mobile_combat_power.clear();

	m_enemyCombatUnits.clear();

	m_ownBuildingsOfCategory.clear();

	combats_learned.clear();
	combats_this_game.clear();
}

void AAISector::Init(AAI *ai, int x, int y, int left, int right, int top, int bottom)
{
	this->ai = ai;

	// set coordinates of the corners
	this->x = x;
	this->y = y;

	this->left = left;
	this->right = right;
	this->top = top;
	this->bottom = bottom;

	// determine map border distance
	const int xEdgeDist = std::min(x, ai->Getmap()->xSectors - 1 - x);
	const int yEdgeDist = std::min(y, ai->Getmap()->ySectors - 1 - y);

	m_minSectorDistanceToMapEdge = std::min(xEdgeDist, yEdgeDist);

	const float3 center = GetCenter();
	continent = ai->Getmap()->GetContinentID(center);

	// init all kind of stuff
	freeMetalSpots = false;
	interior = false;
	distance_to_base = -1;
	last_scout = 1;
	rally_points = 0;

	// nothing sighted in that sector
	enemy_structures = 0.0f;
	enemies_on_radar = 0;
	allied_structures = 0.0f;
	failed_defences = 0;

	int categories = ai->Getbt()->assault_categories.size();

	combats_learned.resize(categories, 0);
	combats_this_game.resize(categories, 0);

	importance_this_game = 1.0f + (rand()%5)/20.0f;

	attacked_by_this_game.resize(categories, 0);
	attacked_by_learned.resize(categories, 0);

	my_stat_combat_power.resize(categories, 0);
	enemy_stat_combat_power.resize(categories, 0);

	my_mobile_combat_power.resize(categories+1, 0);
	enemy_mobile_combat_power.resize(categories+1, 0);

	m_enemyCombatUnits.resize(AAICombatUnitCategory::numberOfCombatUnitCategories, 0.0f);

	m_ownBuildingsOfCategory.resize(AAIUnitCategory::numberOfUnitCategories, 0);
}

void AAISector::AddMetalSpot(AAIMetalSpot *spot)
{
	metalSpots.push_back(spot);
	freeMetalSpots = true;
}

bool AAISector::SetBase(bool base)
{
	if(base)
	{
		// check if already occupied (may happen if two coms start in same sector)
		if(ai->Getmap()->team_sector_map[x][y] >= 0)
		{
			ai->Log("\nTeam %i could not add sector %i,%i to base, already occupied by ally team %i!\n\n",ai->GetAICallback()->GetMyTeam(), x, y, ai->Getmap()->team_sector_map[x][y]);
			return false;
		}

		distance_to_base = 0;

		// if free metal spots in this sectors, base has free spots
		for(list<AAIMetalSpot*>::iterator spot = metalSpots.begin(); spot != metalSpots.end(); ++spot)
		{
			if(!(*spot)->occupied)
			{
				ai->Getbrain()->m_freeMetalSpotsInBase = true;
				break;
			}
		}

		// increase importance
		importance_this_game += 1;

		ai->Getmap()->team_sector_map[x][y] = ai->GetAICallback()->GetMyTeam();

		if(importance_this_game > cfg->MAX_SECTOR_IMPORTANCE)
			importance_this_game = cfg->MAX_SECTOR_IMPORTANCE;

		return true;
	}
	else	// remove from base
	{
		distance_to_base = 1;

		ai->Getmap()->team_sector_map[x][y] = -1;

		return true;
	}
}

void AAISector::ResetLocalCombatPower() 
{
	allied_structures = 0;
	std::fill(my_mobile_combat_power.begin(), my_mobile_combat_power.end(), 0.0f);
	std::fill(my_stat_combat_power.begin(),   my_stat_combat_power.end(),   0.0f);
}

void AAISector::Update()
{
	// decrease values (so the ai "forgets" values from time to time)...
	//ground_threat *= 0.995;
	//air_threat *= 0.995;
	m_lostUnits    *= 0.95f;
	m_lostAirUnits *= 0.95f;
}

AAIMetalSpot* AAISector::GetFreeMetalSpot()
{
	// look for the first unoccupied metalspot
	for(list<AAIMetalSpot*>::iterator i = metalSpots.begin(); i != metalSpots.end(); ++i)
	{
		// if metalspot is occupied, try next one
		if(!(*i)->occupied)
			return *i;
	}


	return 0;
}
void AAISector::FreeMetalSpot(float3 pos, const UnitDef *extractor)
{
	float3 spot_pos;

	// get metalspot according to position
	for(list<AAIMetalSpot*>::iterator spot = metalSpots.begin(); spot != metalSpots.end(); ++spot)
	{
		// only check occupied spots
		if((*spot)->occupied)
		{
			// compare positions
			spot_pos = (*spot)->pos;
			ai->Getmap()->Pos2FinalBuildPos(&spot_pos, extractor);

			if(pos.x == spot_pos.x && pos.z == spot_pos.z)
			{
				(*spot)->occupied = false;
				(*spot)->extractor = -1;
				(*spot)->extractor_def = -1;

				freeMetalSpots = true;

				// if part of the base, tell the brain that the base has now free spots again
				if(distance_to_base == 0)
					ai->Getbrain()->m_freeMetalSpotsInBase = true;

				return;
			}
		}
	}
}

void AAISector::AddExtractor(int unit_id, int def_id, float3 *pos)
{
	float3 spot_pos;

	// get metalspot according to position
	for(list<AAIMetalSpot*>::iterator spot = metalSpots.begin(); spot != metalSpots.end(); ++spot)
	{
		// only check occupied spots
		if((*spot)->occupied)
		{
			// compare positions
			spot_pos = (*spot)->pos;
			ai->Getmap()->Pos2FinalBuildPos(&spot_pos, &ai->Getbt()->GetUnitDef(def_id));

			if(pos->x == spot_pos.x && pos->z == spot_pos.z)
			{
				(*spot)->extractor = unit_id;
				(*spot)->extractor_def = def_id;
			}
		}
	}
}

float3 AAISector::GetCenter()
{
	float3 pos;
	pos.x = (left + right)/2.0;
	pos.z = (top + bottom)/2.0;

	return pos;
}

/*float3 AAISector::GetCenterRallyPoint()
{

	return ZeroVector;
}*/


float3 AAISector::FindBuildsite(int building, bool water) const
{
	int xStart, xEnd, yStart, yEnd;

	DetermineBuildsiteRectangle(&xStart, &xEnd, &yStart, &yEnd);

	return ai->Getmap()->GetBuildSiteInRect(&ai->Getbt()->GetUnitDef(building), xStart, xEnd, yStart, yEnd, water);
}

float3 AAISector::GetDefenceBuildsite(UnitDefId buildingDefId, const AAIUnitCategory& category, float terrainModifier, bool water) const
{
	float3 best_pos = ZeroVector, pos;
	
	int my_team = ai->GetAICallback()->GetMyAllyTeam();

	float my_rating, best_rating = -10000;

	list<Direction> directions;

	// get possible directions
	if(category.isAirCombat() && !cfg->AIR_ONLY_MOD)
	{
		directions.push_back(CENTER);
	}
	else
	{
		if(distance_to_base > 0)
			directions.push_back(CENTER);
		else
		{
			// filter out frontiers to other base sectors
			if(x > 0 && ai->Getmap()->sector[x-1][y].distance_to_base > 0 && ai->Getmap()->sector[x-1][y].allied_structures < 100 && ai->Getmap()->team_sector_map[x-1][y] != my_team )
				directions.push_back(WEST);

			if(x < ai->Getmap()->xSectors-1 && ai->Getmap()->sector[x+1][y].distance_to_base > 0 && ai->Getmap()->sector[x+1][y].allied_structures < 100 && ai->Getmap()->team_sector_map[x+1][y] != my_team)
				directions.push_back(EAST);

			if(y > 0 && ai->Getmap()->sector[x][y-1].distance_to_base > 0 && ai->Getmap()->sector[x][y-1].allied_structures < 100 && ai->Getmap()->team_sector_map[x][y-1] != my_team)
				directions.push_back(NORTH);

			if(y < ai->Getmap()->ySectors-1 && ai->Getmap()->sector[x][y+1].distance_to_base > 0 && ai->Getmap()->sector[x][y+1].allied_structures < 100 && ai->Getmap()->team_sector_map[x][y+1] != my_team)
				directions.push_back(SOUTH);
		}
	}

	int xStart = 0;
	int xEnd = 0;
	int yStart = 0;
	int yEnd = 0;

	// check possible directions
	for(list<Direction>::iterator dir =directions.begin(); dir != directions.end(); ++dir)
	{
		// get area to perform search
		if(*dir == CENTER)
		{
			xStart = x * ai->Getmap()->xSectorSizeMap;
			xEnd = (x+1) * ai->Getmap()->xSectorSizeMap;
			yStart = y * ai->Getmap()->ySectorSizeMap;
			yEnd = (y+1) * ai->Getmap()->ySectorSizeMap;
		}
		else if(*dir == WEST)
		{
			xStart = x * ai->Getmap()->xSectorSizeMap;
			xEnd = x * ai->Getmap()->xSectorSizeMap + ai->Getmap()->xSectorSizeMap/4.0f;
			yStart = y * ai->Getmap()->ySectorSizeMap;
			yEnd = (y+1) * ai->Getmap()->ySectorSizeMap;
		}
		else if(*dir == EAST)
		{
			xStart = (x+1) * ai->Getmap()->xSectorSizeMap - ai->Getmap()->xSectorSizeMap/4.0f;
			xEnd = (x+1) * ai->Getmap()->xSectorSizeMap;
			yStart = y * ai->Getmap()->ySectorSizeMap;
			yEnd = (y+1) * ai->Getmap()->ySectorSizeMap;
		}
		else if(*dir == NORTH)
		{
			xStart = x * ai->Getmap()->xSectorSizeMap;
			xEnd = (x+1) * ai->Getmap()->xSectorSizeMap;
			yStart = y * ai->Getmap()->ySectorSizeMap;
			yEnd = y * ai->Getmap()->ySectorSizeMap + ai->Getmap()->ySectorSizeMap/4.0f;
		}
		else if(*dir == SOUTH)
		{
			xStart = x * ai->Getmap()->xSectorSizeMap ;
			xEnd = (x+1) * ai->Getmap()->xSectorSizeMap;
			yStart = (y+1) * ai->Getmap()->ySectorSizeMap - ai->Getmap()->ySectorSizeMap/4.0f;
			yEnd = (y+1) * ai->Getmap()->ySectorSizeMap;
		}

		//
		const UnitDef *def = &ai->Getbt()->GetUnitDef(buildingDefId.id);
		my_rating = ai->Getmap()->GetDefenceBuildsite(&pos, def, xStart, xEnd, yStart, yEnd, category, terrainModifier, water);

		if(my_rating > best_rating)
		{
			best_pos = pos;
			best_rating = my_rating;
		}
	}

	return best_pos;
}

float3 AAISector::GetCenterBuildsite(int building, bool water)
{
	int xStart, xEnd, yStart, yEnd;

	DetermineBuildsiteRectangle(&xStart, &xEnd, &yStart, &yEnd);

	return ai->Getmap()->GetCenterBuildsite(&ai->Getbt()->GetUnitDef(building), xStart, xEnd, yStart, yEnd, water);
}

float3 AAISector::GetRadarArtyBuildsite(int building, float range, bool water)
{
	int xStart, xEnd, yStart, yEnd;

	DetermineBuildsiteRectangle(&xStart, &xEnd, &yStart, &yEnd);

	return ai->Getmap()->GetRadarArtyBuildsite(&ai->Getbt()->GetUnitDef(building), xStart, xEnd, yStart, yEnd, range, water);
}

float3 AAISector::GetHighestBuildsite(int building)
{
	if(building < 1)
	{
		ai->Log("ERROR: Invalid building def id %i passed to AAISector::GetRadarBuildsite()\n", building);
		return ZeroVector;
	}

	int xStart, xEnd, yStart, yEnd;

	DetermineBuildsiteRectangle(&xStart, &xEnd, &yStart, &yEnd);

	return ai->Getmap()->GetHighestBuildsite(&ai->Getbt()->GetUnitDef(building), xStart, xEnd, yStart, yEnd);
}

float3 AAISector::GetRandomBuildsite(int building, int tries, bool water)
{
	if(building < 1)
	{
		ai->Log("ERROR: Invalid building def id %i passed to AAISector::GetRadarBuildsite()\n", building);
		return ZeroVector;
	}

	int xStart, xEnd, yStart, yEnd;

	DetermineBuildsiteRectangle(&xStart, &xEnd, &yStart, &yEnd);

	return ai->Getmap()->GetRandomBuildsite(&ai->Getbt()->GetUnitDef(building), xStart, xEnd, yStart, yEnd, tries, water);
}

void AAISector::DetermineBuildsiteRectangle(int *xStart, int *xEnd, int *yStart, int *yEnd) const
{
	*xStart = x * ai->Getmap()->xSectorSizeMap;
	*xEnd = *xStart + ai->Getmap()->xSectorSizeMap;

	if(*xStart == 0)
		*xStart = 8;

	*yStart = y * ai->Getmap()->ySectorSizeMap;
	*yEnd = *yStart + ai->Getmap()->ySectorSizeMap;

	if(*yStart == 0)
		*yStart = 8;

	// reserve buildspace for def. buildings
	if(x > 0 && ai->Getmap()->sector[x-1][y].distance_to_base > 0 )
		*xStart += ai->Getmap()->xSectorSizeMap/8;

	if(x < ai->Getmap()->xSectors-1 && ai->Getmap()->sector[x+1][y].distance_to_base > 0)
		*xEnd -= ai->Getmap()->xSectorSizeMap/8;

	if(y > 0 && ai->Getmap()->sector[x][y-1].distance_to_base > 0)
		*yStart += ai->Getmap()->ySectorSizeMap/8;

	if(y < ai->Getmap()->ySectors-1 && ai->Getmap()->sector[x][y+1].distance_to_base > 0)
		*yEnd -= ai->Getmap()->ySectorSizeMap/8;
}

// converts unit positions to cell coordinates
void AAISector::Pos2SectorMapPos(float3 *pos, const UnitDef* def)
{
	// get cell index of middlepoint
	pos->x = ((int) pos->x/SQUARE_SIZE)%ai->Getmap()->xSectorSizeMap;
	pos->z = ((int) pos->z/SQUARE_SIZE)%ai->Getmap()->ySectorSizeMap;

	// shift to the leftmost uppermost cell
	pos->x -= def->xsize/2;
	pos->z -= def->zsize/2;

	// check if pos is still in that scetor, otherwise retun 0
	if(pos->x < 0 && pos->z < 0)
		pos->x = pos->z = 0;
}

void AAISector::SectorMapPos2Pos(float3 *pos, const UnitDef *def)
{
	// shift to middlepoint
	pos->x += def->xsize/2;
	pos->z += def->zsize/2;

	// get cell position on complete map
	pos->x += x * ai->Getmap()->xSectorSizeMap;
	pos->z += y * ai->Getmap()->ySectorSizeMap;

	// back to unit coordinates
	pos->x *= SQUARE_SIZE;
	pos->z *= SQUARE_SIZE;
}

UnitCategory AAISector::GetWeakestCategory()
{
	UnitCategory weakest = UNKNOWN;
	float importance, most_important = 0;

	float learned = 60000 / (ai->GetAICallback()->GetCurrentFrame() + 30000) + 0.5;
	float current = 2.5 - learned;

	if(interior)
	{
		weakest = AIR_ASSAULT;
	}
	else
	{
		for(list<UnitCategory>::iterator cat = ai->Getbt()->assault_categories.begin(); cat != ai->Getbt()->assault_categories.end(); ++cat)
		{
			importance = GetThreatBy(*cat, learned, current) / ( 0.1f + GetMyDefencePowerAgainstAssaultCategory(*cat));

			if(importance > most_important)
			{
				most_important = importance;
				weakest = *cat;
			}
		}
	}

	return weakest;
}

float AAISector::GetThreatBy(UnitCategory category, float learned, float current)
{
	if(category == GROUND_ASSAULT)
		return 1.0f + (learned * attacked_by_learned[0] + current * attacked_by_this_game[0] ) / (learned + current);
	if(category == AIR_ASSAULT)
		return 1.0f + (learned * attacked_by_learned[1] + current * attacked_by_this_game[1] ) / (learned + current);
	if(category == HOVER_ASSAULT)
		return 1.0f + (learned * attacked_by_learned[2] + current * attacked_by_this_game[2] ) / (learned + current);
	if(category == SEA_ASSAULT)
		return 1.0f + (learned * attacked_by_learned[3] + current * attacked_by_this_game[3] ) / (learned + current);
	if(category == SUBMARINE_ASSAULT)
		return 1.0f + (learned * attacked_by_learned[4] + current * attacked_by_this_game[4] ) / (learned + current);
	else
		return -1;
}

float AAISector::GetThreatByID(int combat_cat_id, float learned, float current)
{
	return (learned * attacked_by_learned[combat_cat_id] + current * attacked_by_this_game[combat_cat_id] ) / (learned + current);
}

float AAISector::GetMyCombatPower(float ground, float air, float hover, float sea, float submarine)
{
	return (ground * my_mobile_combat_power[0] + air * my_mobile_combat_power[1] + hover * my_mobile_combat_power[2] + sea * my_mobile_combat_power[3] + submarine * my_mobile_combat_power[4]);
}

float AAISector::GetEnemyCombatPower(float ground, float air, float hover, float sea, float submarine)
{
	return (ground * enemy_mobile_combat_power[0] + air * enemy_mobile_combat_power[1] + hover * enemy_mobile_combat_power[2] + sea * enemy_mobile_combat_power[3] + submarine * enemy_mobile_combat_power[4]);
}

float AAISector::GetMyCombatPowerAgainstCombatCategory(int combat_category)
{
	return my_mobile_combat_power[combat_category];
}

float AAISector::GetEnemyCombatPowerAgainstCombatCategory(int combat_category)
{
	return enemy_stat_combat_power[combat_category];
}

float AAISector::GetMyDefencePower(float ground, float air, float hover, float sea, float submarine)
{
	return (ground * my_stat_combat_power[0] + air * my_stat_combat_power[1] + hover * my_stat_combat_power[2] + sea * my_stat_combat_power[3] + submarine * my_stat_combat_power[4]);
}

float AAISector::GetEnemyDefencePower(const CombatPower& combatCategoryWeigths) const
{
	return (combatCategoryWeigths.vsGround  * (enemy_stat_combat_power[0] + enemy_mobile_combat_power[0])
		+ combatCategoryWeigths.vsAir       * (enemy_stat_combat_power[1] + enemy_mobile_combat_power[1])
		+ combatCategoryWeigths.vsHover     * (enemy_stat_combat_power[2] + enemy_mobile_combat_power[2])
		+ combatCategoryWeigths.vsSea       * (enemy_stat_combat_power[3] + enemy_mobile_combat_power[3])
		+ combatCategoryWeigths.vsSubmarine * (enemy_stat_combat_power[4] + enemy_mobile_combat_power[4]) );
}

float AAISector::GetMyDefencePowerAgainstAssaultCategory(int assault_category)
{
	return my_stat_combat_power[assault_category];
}

float AAISector::GetEnemyDefencePowerAgainstAssaultCategory(int assault_category)
{
	return enemy_stat_combat_power[assault_category];
}

float AAISector::getEnemyThreatToMovementType(const AAIMovementType& movementType) const
{
	EMovementType moveType = movementType.GetMovementType();
	switch( moveType )
	{
		case EMovementType::MOVEMENT_TYPE_AMPHIBIOUS: // fallthorugh intended
		case EMovementType::MOVEMENT_TYPE_GROUND:
		{
			return enemy_stat_combat_power[0] + enemy_mobile_combat_power[0];
		}
		case EMovementType::MOVEMENT_TYPE_AIR:
		{
			return enemy_stat_combat_power[1] + enemy_mobile_combat_power[1];
		}
		case EMovementType::MOVEMENT_TYPE_HOVER:
		{
			return enemy_stat_combat_power[2] + enemy_mobile_combat_power[2];
		}
		case EMovementType::MOVEMENT_TYPE_SEA_FLOATER:
		{
			return enemy_stat_combat_power[3] + enemy_mobile_combat_power[3];
		}
		case EMovementType::MOVEMENT_TYPE_SEA_SUBMERGED:
		{
			return enemy_stat_combat_power[4] + enemy_mobile_combat_power[4];
		}
		default:
		{
			return 0.0f;
		}
	}
}

float AAISector::GetEnemyAreaCombatPowerVs(int combat_category, float neighbour_importance) const
{
	float result = enemy_mobile_combat_power[combat_category];

	// take neighbouring sectors into account (if possible)
	if(x > 0)
		result += neighbour_importance * ai->Getmap()->sector[x-1][y].enemy_mobile_combat_power[combat_category];

	if(x < ai->Getmap()->xSectors-1)
		result += neighbour_importance * ai->Getmap()->sector[x+1][y].enemy_mobile_combat_power[combat_category];

	if(y > 0)
		result += neighbour_importance * ai->Getmap()->sector[x][y-1].enemy_mobile_combat_power[combat_category];

	if(y < ai->Getmap()->ySectors-1)
		result += neighbour_importance * ai->Getmap()->sector[x][y+1].enemy_mobile_combat_power[combat_category];

	return result;
}

float AAISector::GetOverallThreat(float learned, float current)
{
	return (learned * (attacked_by_learned[0] + attacked_by_learned[1] + attacked_by_learned[2] + attacked_by_learned[3])
		+ current *	(attacked_by_this_game[0] + attacked_by_this_game[1] + attacked_by_this_game[2] + attacked_by_this_game[3]))
		/(learned + current);
}

float AAISector::GetWaterRatio()
{
	float water_ratio = 0;

	for(int xPos = x * ai->Getmap()->xSectorSizeMap; xPos < (x+1) * ai->Getmap()->xSectorSizeMap; ++xPos)
	{
		for(int yPos = y * ai->Getmap()->ySectorSizeMap; yPos < (y+1) * ai->Getmap()->ySectorSizeMap; ++yPos)
		{
			if(ai->Getmap()->buildmap[xPos + yPos * ai->Getmap()->xMapSize] == 4)
				water_ratio +=1;
		}
	}

	return water_ratio / ((float)(ai->Getmap()->xSectorSizeMap * ai->Getmap()->ySectorSizeMap));
}

float AAISector::GetFlatRatio()
{
	// get number of cliffy tiles
	float flat_ratio = ai->Getmap()->GetCliffyCells(left/SQUARE_SIZE, top/SQUARE_SIZE, ai->Getmap()->xSectorSizeMap, ai->Getmap()->ySectorSizeMap);

	// get number of flat tiles
	flat_ratio = (float)(ai->Getmap()->xSectorSizeMap * ai->Getmap()->ySectorSizeMap) - flat_ratio;

	flat_ratio /= (float)(ai->Getmap()->xSectorSizeMap * ai->Getmap()->ySectorSizeMap);

	return flat_ratio;
}

void AAISector::UpdateThreatValues(UnitDefId destroyedDefId, UnitDefId attackerDefId)
{
	const AAIUnitCategory& destroyedCategory = ai->s_buildTree.GetUnitCategory(destroyedDefId);
	const AAIUnitCategory& attackerCategory  = ai->s_buildTree.GetUnitCategory(attackerDefId);

	// if lost unit is a building, increase attacked_by
	if(destroyedCategory.isBuilding() == true)
	{
		if(attackerCategory.isCombatUnit() == true)
		{
			const float increment = interior ? 0.3f : 1.0f;
			AAICombatUnitCategory category(attackerCategory);
			attacked_by_this_game[category.GetArrayIndex()] += increment;
		}
	}
	else // unit was lost
	{
		if(attackerCategory.isCombatUnit() == true)
		{
			AAICombatUnitCategory category(attackerCategory);
			combats_this_game[category.GetArrayIndex()] += 1.0f;
		}

		if(ai->s_buildTree.GetMovementType(destroyedDefId).IsAir())
			m_lostAirUnits += 1.0f;
		else
			m_lostUnits += 1.0f;
	}
}

bool AAISector::PosInSector(const float3& pos) const
{
	if( (pos.x < left) || (pos.x > right) || (pos.z < top) || (pos.z > bottom) )
		return false;
	else
		return true;
}

bool AAISector::ConnectedToOcean()
{
	if(water_ratio < 0.2)
		return false;

	// find water cell
	int x_cell = (left + right) / 16.0f;
	int y_cell = (top + bottom) / 16.0f;

	// get continent
	int cont = ai->Getmap()->GetContinentID(x_cell, y_cell);

	if(ai->Getmap()->continents[cont].water)
	{
		if(ai->Getmap()->continents[cont].size > 1200 && ai->Getmap()->continents[cont].size > 0.5f * (float)ai->Getmap()->avg_water_continent_size )
			return true;
	}

	return false;
}

bool AAISector::determineMovePos(float3 *pos)
{
	int x,y;
	*pos = ZeroVector;

	// try to get random spot
	for(int i = 0; i < 6; ++i)
	{
		pos->x = left + ai->Getmap()->xSectorSize * (0.2f + 0.06f * (float)(rand()%11) );
		pos->z = top + ai->Getmap()->ySectorSize * (0.2f + 0.06f * (float)(rand()%11) );

		// check if blocked by  building
		x = (int) (pos->x / SQUARE_SIZE);
		y = (int) (pos->z / SQUARE_SIZE);

		if(ai->Getmap()->buildmap[x + y * ai->Getmap()->xMapSize] != 1)
			return true;
	}

	// search systematically
	for(int i = 0; i < ai->Getmap()->xSectorSizeMap; i += 8)
	{
		for(int j = 0; j < ai->Getmap()->ySectorSizeMap; j += 8)
		{
			pos->x = left + i * SQUARE_SIZE;
			pos->z = top + j * SQUARE_SIZE;

			// get cell index of middlepoint
			x = (int) (pos->x / SQUARE_SIZE);
			y = (int) (pos->z / SQUARE_SIZE);

			if(ai->Getmap()->buildmap[x + y * ai->Getmap()->xMapSize] != 1)
				return true;
		}
	}

	// no free cell found (should not happen)
	*pos = ZeroVector;
	return false;
}

bool AAISector::determineMovePosOnContinent(float3 *pos, int continent)
{
	int x,y;
	*pos = ZeroVector;

	// try to get random spot
	for(int i = 0; i < 6; ++i)
	{
		pos->x = left + ai->Getmap()->xSectorSize * (0.2f + 0.06f * (float)(rand()%11) );
		pos->z = top + ai->Getmap()->ySectorSize * (0.2f + 0.06f * (float)(rand()%11) );

		// check if blocked by  building
		x = (int) (pos->x / SQUARE_SIZE);
		y = (int) (pos->z / SQUARE_SIZE);

		if(ai->Getmap()->buildmap[x + y * ai->Getmap()->xMapSize] != 1)
		{
			//check continent
			if(ai->Getmap()->GetContinentID(*pos) == continent)
				return true;
		}
	}

	// search systematically
	for(int i = 0; i < ai->Getmap()->xSectorSizeMap; i += 8)
	{
		for(int j = 0; j < ai->Getmap()->ySectorSizeMap; j += 8)
		{
			pos->x = left + i * SQUARE_SIZE;
			pos->z = top + j * SQUARE_SIZE;

			// get cell index of middlepoint
			x = (int) (pos->x / SQUARE_SIZE);
			y = (int) (pos->z / SQUARE_SIZE);

			if(ai->Getmap()->buildmap[x + y * ai->Getmap()->xMapSize] != 1)
			{
				if(ai->Getmap()->GetContinentID(*pos) == continent)
					return true;
			}
		}
	}

	*pos = ZeroVector;
	return false;
}
