// -------------------------------------------------------------------------
// AAI
//
// A skirmish AI for the Spring engine.
// Copyright Alexander Seizinger
//
// Released under GPL license: see LICENSE.html for more information.
// -------------------------------------------------------------------------

#include "AAI.h"
#include "AAIBrain.h"
#include "AAIBuildTable.h"
#include "AAIExecute.h"
#include "AAIUnitTable.h"
#include "AAIAirForceManager.h"
#include "AAIConfig.h"
#include "AAIMap.h"
#include "AAIGroup.h"
#include "AAISector.h"
#include "AAIHelperFunctions.h"

#include <unordered_map>

#include "LegacyCpp/UnitDef.h"
using namespace springLegacyAI;

AttackedByRatesPerGamePhase AAIBrain::s_attackedByRates;
MobileTargetTypeValues      AAIBrain::s_enemyThreatByMap;

AAIBrain::AAIBrain(AAI *ai, int maxSectorDistanceToBase) :
	m_baseFlatLandRatio(0.0f),
	m_baseWaterRatio(0.0f),
	m_centerOfBase(0, 0),
	m_metalAvailable(AAIConstants::incomeSamplePoints),
	m_energyAvailable(AAIConstants::incomeSamplePoints),
	m_metalIncome(AAIConstants::incomeSamplePoints),
	m_energyIncome(AAIConstants::incomeSamplePoints),
	m_metalSurplus(AAIConstants::incomeSamplePoints),
	m_energySurplus(AAIConstants::incomeSamplePoints),
	m_estimatedPressureByEnemies(0.0f)
{
	this->ai = ai;

	m_sectorsInDistToBase.resize(maxSectorDistanceToBase);

	//-----------------------------------------------------------------------------------------------------------------
	// Determine threat by target type based on map -ranges between 0 (no threat to be expected) and 0.3
	//-----------------------------------------------------------------------------------------------------------------

	s_enemyThreatByMap.Fill(AAIConstants::defaultEnemyThreatByTerrain);

	s_enemyThreatByMap[ETargetType::SURFACE]   *= (1.0f - AAIMap::s_waterTilesRatio);
	s_enemyThreatByMap[ETargetType::FLOATER]   *= AAIMap::s_waterTilesRatio;
	s_enemyThreatByMap[ETargetType::SUBMERGED] *= AAIMap::s_waterTilesRatio;

	const AAIMapType& mapType = ai->Map()->GetMapType();

	if(mapType.IsLand())
	{
		s_enemyThreatByMap[ETargetType::SURFACE] += AAIConstants::defaultEnemyThreatByMapType;
	}
	else if(mapType.IsLandWater())
	{
		s_enemyThreatByMap[ETargetType::SURFACE]   += AAIConstants::defaultEnemyThreatByMapType;
		s_enemyThreatByMap[ETargetType::FLOATER]   += AAIConstants::defaultEnemyThreatByMapType;
		s_enemyThreatByMap[ETargetType::SUBMERGED] += AAIConstants::defaultEnemyThreatByMapType;
	}
	else if(mapType.IsWater())
	{
		s_enemyThreatByMap[ETargetType::FLOATER]   += AAIConstants::defaultEnemyThreatByMapType;
		s_enemyThreatByMap[ETargetType::SUBMERGED] += AAIConstants::defaultEnemyThreatByMapType;
	}
}

AAIBrain::~AAIBrain(void)
{
}

void AAIBrain::InitAttackedByRates(const AttackedByRatesPerGamePhase& attackedByRates)
{
	s_attackedByRates = attackedByRates;
}

bool AAIBrain::RessourcesForConstr(int /*unit*/, int /*wokertime*/)
{
	//! @todo check metal and energy

	return true;
}

void AAIBrain::AssignSectorToBase(AAISector *sector, bool addToBase)
{
	const bool successful = sector->AddToBase(addToBase);

	if(successful)
	{
		if(addToBase)
			m_sectorsInDistToBase[0].push_back(sector);
		else
			m_sectorsInDistToBase[0].remove(sector);
	}

	// update base land/water ratio
	m_baseFlatLandRatio = 0.0f;
	m_baseWaterRatio    = 0.0f;

	if(m_sectorsInDistToBase[0].size() > 0)
	{
		//for(auto sector = m_sectorsInDistToBase[0].begin(); sector != m_sectorsInDistToBase[0].end(); ++sector)
		for(auto sector : m_sectorsInDistToBase[0])
		{
			m_baseFlatLandRatio += sector->GetFlatTilesRatio();
			m_baseWaterRatio    += sector->GetWaterTilesRatio();
		}

		m_baseFlatLandRatio /= static_cast<float>(m_sectorsInDistToBase[0].size());
		m_baseWaterRatio    /= static_cast<float>(m_sectorsInDistToBase[0].size());
	}

	ai->Map()->UpdateNeighbouringSectors(m_sectorsInDistToBase);

	UpdateCenterOfBase();
}

void AAIBrain::DefendCommander(int /*attacker*/)
{
//	float3 pos = ai->Getcb()->GetUnitPos(ai->Getut()->cmdr);
	//float importance = 120;
	Command c;

	// evacuate cmdr
	// TODO: FIXME: check/fix?/implement me?
	/*if(ai->cmdr->task != BUILDING)
	{
		AAISector *sector = GetSafestSector();

		if(sector != 0)
		{
			pos = sector->GetCenter();

			if(pos.x > 0 && pos.z > 0)
			{
				pos.y = ai->Getcb()->GetElevation(pos.x, pos.z);
				ai->Getexecute()->MoveUnitTo(ai->cmdr->unit_id, &pos);
			}
		}
	}*/
}

void AAIBrain::UpdateCenterOfBase()
{
	m_centerOfBase.x = 0;
	m_centerOfBase.y = 0;

	if(m_sectorsInDistToBase[0].size() > 0)
	{
		for(const auto sector : m_sectorsInDistToBase[0])
		{
			const SectorIndex& index = sector->GetSectorIndex();
			m_centerOfBase.x += index.x;
			m_centerOfBase.y += index.y;
		}

		m_centerOfBase.x *= AAIMap::xSectorSizeMap;
		m_centerOfBase.y *= AAIMap::ySectorSizeMap;

		m_centerOfBase.x /= m_sectorsInDistToBase[0].size();
		m_centerOfBase.y /= m_sectorsInDistToBase[0].size();

		m_centerOfBase.x += AAIMap::xSectorSizeMap/2;
		m_centerOfBase.y += AAIMap::ySectorSizeMap/2;
	}
}

bool AAIBrain::IsCommanderAllowedForConstructionInSector(const AAISector *sector) const
{
	// commander is always allowed in base
	if(sector->IsOccupiedByEnemies())
		return false;

	if(sector->GetDistanceToBase() <= 0)
		return true;
	// allow construction close to base for small bases
	else if(m_sectorsInDistToBase[0].size() < 3 && sector->GetDistanceToBase() <= 1)
		return true;
	else
		return false;
}

struct SectorForBaseExpansion
{
	SectorForBaseExpansion(AAISector* _sector, float _distance, float _totalAttacks) :
		sector(_sector), distance(_distance), totalAttacks(_totalAttacks) { }

	AAISector* sector;
	float      distance;
	float      totalAttacks;
};

void AAIBrain::ExpandBaseAtStartup()
{
	if(m_sectorsInDistToBase[0].size() == 0)
	{
		ai->Log("ERROR: Failed to expand initial base - no starting sector set!\n");
		return;
	}

	AAISector* sector = *m_sectorsInDistToBase[0].begin();

	const bool preferSafeSector = (sector->GetEdgeDistance() > 0) ? true : false;

	ExpandBase( ai->Map()->GetMapType(), preferSafeSector);
}

bool AAIBrain::ExpandBase(const AAIMapType& sectorType, bool preferSafeSector)
{
	if(m_sectorsInDistToBase[0].size() >= cfg->MAX_BASE_SIZE)
		return false;

	// if aai is looking for a water sector to expand into ocean, allow greater search_dist
	const bool expandLandBaseInWater = sectorType.IsWater() && (m_baseWaterRatio < 0.1f);
	const int  maxSearchDistance     = expandLandBaseInWater ? 3 : 1;

	//-----------------------------------------------------------------------------------------------------------------
	// assemble a list of potential sectors for base expansion
	//-----------------------------------------------------------------------------------------------------------------
	std::list<SectorForBaseExpansion> expansionCandidateList;
	StatisticalData sectorDistances;
	StatisticalData sectorAttacks;

	for(int distanceToBase = 1; distanceToBase <= maxSearchDistance; ++distanceToBase)
	{
		for(auto sector : m_sectorsInDistToBase[distanceToBase])
		{
			if(sector->IsSectorSuitableForBaseExpansion() )
			{
				const SectorIndex& index = sector->GetSectorIndex();

				float sectorDistance(0.0f);
				for(auto sectorInBase : m_sectorsInDistToBase[0]) 
				{
					const SectorIndex& indexBaseSector = sectorInBase->GetSectorIndex();

					const int deltaX = index.x - indexBaseSector.x;
					const int deltaY = index.y - indexBaseSector.y;
					sectorDistance += static_cast<float>(deltaX * deltaX + deltaY * deltaY); // try squared distances, use fastmath::apxsqrt() otherwise
				}

				sectorDistances.AddValue(sectorDistance);

				const float totalAttacks = sector->GetTotalAttacksInThisGame() + sector->GetTotalAttacksInPreviousGames();
				sectorAttacks.AddValue(totalAttacks);

				expansionCandidateList.push_back( SectorForBaseExpansion(sector, sectorDistance, totalAttacks) );
			}
		}
	}

	sectorDistances.Finalize();
	sectorAttacks.Finalize();

	//-----------------------------------------------------------------------------------------------------------------
	// select best sector from the list
	//-----------------------------------------------------------------------------------------------------------------
	AAISector *selectedSector(nullptr);
	float highestRating(0.0f);

	for(auto candidate = expansionCandidateList.begin(); candidate != expansionCandidateList.end(); ++candidate)
	{
		// prefer sectors that result in more compact bases, with more metal spots, that are safer (i.e. less attacks in the past)
		float rating = static_cast<float>( candidate->sector->GetNumberOfMetalSpots() );
						+ 4.0f * sectorDistances.GetDeviationFromMax(candidate->distance);

		if(preferSafeSector)
		{
			rating += 4.0f * sectorAttacks.GetDeviationFromMax(candidate->totalAttacks);
			rating += 4.0f / static_cast<float>( candidate->sector->GetEdgeDistance() + 1 );
		}
		else
		{
			rating += std::min(static_cast<float>( candidate->sector->GetEdgeDistance() ), 4.0f);
		}

		if(sectorType.IsLand())
		{
			// prefer flat sectors
			rating += 3.0f * candidate->sector->GetFlatTilesRatio();
		}
		else if(sectorType.IsWater())
		{
			// check for continent size (to prevent AAI to expand into little ponds instead of big ocean)
			if( candidate->sector->ConnectedToOcean() )
				rating += 3.0f * candidate->sector->GetWaterTilesRatio();
		}
		else // LAND_WATER_SECTOR
			rating += 3.0f * (candidate->sector->GetFlatTilesRatio() + candidate->sector->GetWaterTilesRatio());

		if(rating > highestRating)
		{
			highestRating  = rating;
			selectedSector = candidate->sector;
		}			
	}

	//-----------------------------------------------------------------------------------------------------------------
	// assign selected sector to base
	//-----------------------------------------------------------------------------------------------------------------
	if(selectedSector)
	{
		AssignSectorToBase(selectedSector, true);

		const SectorIndex& index = selectedSector->GetSectorIndex();
	
		std::string sectorTypeString = sectorType.IsLand() ? "land" : "water";
		ai->Log("\nAdding %s sector %i,%i to base; base size: " _STPF_, sectorTypeString.c_str(), index.x, index.y, m_sectorsInDistToBase[0].size());
		ai->Log("\nNew land : water ratio within base: %f : %f\n\n", m_baseFlatLandRatio, m_baseWaterRatio);
		return true;
	}

	return false;
}

void AAIBrain::UpdateResources(springLegacyAI::IAICallback* cb)
{
	const float energyIncome = cb->GetEnergyIncome();
	const float metalIncome  = cb->GetMetalIncome();

	float energySurplus = energyIncome - cb->GetEnergyUsage();
	float metalSurplus  = metalIncome  - cb->GetMetalUsage();

	// cap surplus at 0
	if(energySurplus < 0.0f)
		energySurplus = 0.0f;

	if(metalSurplus < 0.0f)
		metalSurplus = 0.0f;

	m_metalAvailable.AddValue(cb->GetMetal());
	m_energyAvailable.AddValue(cb->GetEnergy());

	m_energyIncome.AddValue(energyIncome);
	m_metalIncome.AddValue(metalIncome);

	m_energySurplus.AddValue(energySurplus);
	m_metalSurplus.AddValue(metalSurplus);
}

void AAIBrain::PowerPlantFinished(UnitDefId powerPlant)
{
	const float energyIncome  = m_energyIncome.GetAverageValue()  + ai->s_buildTree.GetPrimaryAbility(powerPlant);
	const float energySurplus = m_energySurplus.GetAverageValue() + 0.5f * ai->s_buildTree.GetPrimaryAbility(powerPlant);
	
	m_energyIncome.FillBuffer(energyIncome);
	m_energySurplus.FillBuffer(energySurplus);
}

void AAIBrain::UpdateMaxCombatUnitsSpotted(const MobileTargetTypeValues& spottedCombatUnits)
{
	m_maxSpottedCombatUnitsOfTargetType.MultiplyValues(0.996f);

	for(const auto& targetType : AAITargetType::m_mobileTargetTypes)
	{
		// check for new max values
		const float value = spottedCombatUnits[targetType];

		if(value > m_maxSpottedCombatUnitsOfTargetType[targetType])
			m_maxSpottedCombatUnitsOfTargetType[targetType] = value;
	}
}

void AAIBrain::UpdateAttackedByValues()
{
	m_recentlyAttackedByRates.MultiplyValues(0.985f);
}

void AAIBrain::AttackedBy(const AAITargetType& attackerTargetType)
{
	const GamePhase gamePhase(ai->GetAICallback()->GetCurrentFrame());

	// update counter for current game
	m_recentlyAttackedByRates[attackerTargetType] += 1.0f;
	
	// update counter for memory dependent on playtime
	s_attackedByRates.AddAttack(gamePhase, attackerTargetType);
}

//! Helper function that adds the given combat power to mobile combat power vs the appropriate target type (depending on unit type & category of combat unit)
void AddToMobileCombatPower(MobileTargetTypeValues& mobileCombatPower, const TargetTypeValues& combatPower, const AAIUnitType&  unitType, const AAIUnitCategory& category)
{
	if(unitType.IsAssaultUnit())
	{
		switch(category.GetUnitCategory())
		{
			case EUnitCategory::GROUND_COMBAT:
				mobileCombatPower[ETargetType::SURFACE]   += combatPower[ETargetType::SURFACE];
				break;
			case EUnitCategory::HOVER_COMBAT:
				mobileCombatPower[ETargetType::SURFACE]   += combatPower[ETargetType::SURFACE];
				mobileCombatPower[ETargetType::FLOATER]   += combatPower[ETargetType::FLOATER];
				break;
			case EUnitCategory::SEA_COMBAT:
				mobileCombatPower[ETargetType::SURFACE]   += combatPower[ETargetType::SURFACE];
				mobileCombatPower[ETargetType::FLOATER]   += combatPower[ETargetType::FLOATER];
				mobileCombatPower[ETargetType::SUBMERGED] += combatPower[ETargetType::SUBMERGED];
				break;
			case EUnitCategory::SUBMARINE_COMBAT:
				mobileCombatPower[ETargetType::FLOATER]   += combatPower[ETargetType::FLOATER];
				mobileCombatPower[ETargetType::SUBMERGED] += combatPower[ETargetType::SUBMERGED];
				break;
		}
	}
	else if(unitType.IsAntiAir())
	{
		mobileCombatPower[ETargetType::AIR] += combatPower[ETargetType::AIR];
	}
}

void AAIBrain::UpdateDefenceCapabilities()
{
	m_totalMobileCombatPower.Fill(0.0f);

	for(const auto category : AAIUnitCategory::m_combatUnitCategories)
	{
		for(const auto group : ai->GetUnitGroupsList(category))
		{
			TargetTypeValues groupCombatPower = ai->s_buildTree.GetCombatPower(group->GetUnitDefIdOfGroup());
			groupCombatPower.MultiplyValues( static_cast<float>(group->GetCurrentSize()) );

			const AAIUnitType&     groupUnitType = group->GetUnitTypeOfGroup();
			const AAIUnitCategory& groupCategory = group->GetUnitCategoryOfGroup();

			AddToMobileCombatPower(m_totalMobileCombatPower, groupCombatPower, groupUnitType, groupCategory);
		}
	}
}

void AAIBrain::AddDefenceCapabilities(UnitDefId unitDefId)
{
	const TargetTypeValues& combatPower = ai->s_buildTree.GetCombatPower(unitDefId);
	const AAIUnitType&      unitType = ai->s_buildTree.GetUnitType(unitDefId);
	const AAIUnitCategory&  category = ai->s_buildTree.GetUnitCategory(unitDefId);

	AddToMobileCombatPower(m_totalMobileCombatPower, combatPower, unitType, category);	
}

float AAIBrain::Affordable()
{
	return 25.0f / (ai->GetAICallback()->GetMetalIncome() + 5.0f);
}

bool IsRandomNumberBelow(float threshold)
{
	// determine random float in [0:1]
	const float randomValue = 0.01f * static_cast<float>(std::rand()%101);
	return randomValue < threshold;
}

void AAIBrain::BuildUnits()
{
	// Determine urgency to counter each of the different combat categories
	const TargetTypeValues      combatPowerVsTargetType = DetermineCombatPowerVsTargetType();

	// Order construction of units according to determined threat/own defence capabilities
	const UnitSelectionCriteria unitSelectionCriteria   = DetermineCombatUnitSelectionCriteria();

	std::vector<float> factoryUtilization(ai->s_buildTree.GetNumberOfFactories(), 0.0f);
	ai->BuildTable()->DetermineFactoryUtilization(factoryUtilization, true);

	const GamePhase gamePhase(ai->GetAICallback()->GetCurrentFrame());

	for(int i = 0; i < ai->Execute()->GetUnitProductionRate(); ++i)
	{
		const AAIMovementType moveType = DetermineMovementTypeForCombatUnitConstruction(gamePhase);
		const bool urgent(false);

		TargetTypeValues finalCombatPower(combatPowerVsTargetType);

		// special setting for air units: adjust combat power to prefer bombers if enemy pressure is low and many bombing run targets are available
		if(moveType.IsAir())
		{
			finalCombatPower[ETargetType::SUBMERGED] = 0.0f;

			// bomber preference ratio between 0 (no targets or high enemy pressure) and 0.9 (low enemy pressure and many possible targets for bombing run) 
			const float bomberRatio = std::max(ai->AirForceMgr()->GetNumberOfBombTargets() - m_estimatedPressureByEnemies - 0.1f, 0.0f);

			//ai->Log("Air selected - bomb targets: %f      enemy pressure: %f   bomber ratio: %f\n", ai->AirForceMgr()->GetNumberOfBombTargets(), m_estimatedPressureByEnemies, bomberRatio); 

			if(IsRandomNumberBelow(bomberRatio))
			{
				ai->Log("bomber selected\n"); 
				finalCombatPower[ETargetType::SURFACE] = 0.0f;
				finalCombatPower[ETargetType::FLOATER] = 0.0f;
				finalCombatPower[ETargetType::AIR]     = 0.0f;
				finalCombatPower[ETargetType::STATIC]  = 1.0f;
			}
		}

		ai->Execute()->BuildCombatUnitOfCategory(moveType, finalCombatPower, unitSelectionCriteria, factoryUtilization, urgent);
	}
}

MobileTargetTypeValues AAIBrain::DetermineThreatByTargetType() const
{
	//-----------------------------------------------------------------------------------------------------------------
	// Calculate threat by and defence vs. the different combat categories
	//-----------------------------------------------------------------------------------------------------------------

	MobileTargetTypeValues attackedByCategory;
	StatisticalData attackedByCatStatistics;
	StatisticalData unitsSpottedStatistics;
	StatisticalData defenceStatistics;

	const GamePhase gamePhase(ai->GetAICallback()->GetCurrentFrame());

	for(const auto& targetType : AAITargetType::m_mobileTargetTypes)
	{
		attackedByCategory[targetType] = GetAttacksBy(targetType, gamePhase);

		attackedByCatStatistics.AddValue( attackedByCategory[targetType] );
		unitsSpottedStatistics.AddValue( m_maxSpottedCombatUnitsOfTargetType[targetType] );
		defenceStatistics.AddValue( m_totalMobileCombatPower[targetType] );
	}

	attackedByCatStatistics.Finalize();
	unitsSpottedStatistics.Finalize();
	defenceStatistics.Finalize();

	//-----------------------------------------------------------------------------------------------------------------
	// Calculate urgency to counter each target category (attack pressure by this target vs. defence power agaibnst this target type)
	//-----------------------------------------------------------------------------------------------------------------

	MobileTargetTypeValues threatByTargetType;

	for(const auto& targetType : AAITargetType::m_mobileTargetTypes)
	{
		// sum can be between 0 and 2.5
		const float sum =   s_enemyThreatByMap[targetType]
						  + 1.1f * attackedByCatStatistics.GetDeviationFromZero( attackedByCategory[targetType] ) 
						  + 1.1f * unitsSpottedStatistics.GetDeviationFromZero( m_maxSpottedCombatUnitsOfTargetType[targetType] );

		// threat between 0 (no perceived threat) and 25 (highest perceived threat) 
		const float threat = sum / (0.1f + defenceStatistics.GetDeviationFromZero(m_totalMobileCombatPower[targetType]));
		threatByTargetType[targetType] = threat;

		//ai->Log("%s: threat: %f  def: %f   ", AAITargetType(targetType).GetName().c_str(), sum, defenceStatistics.GetDeviationFromMax(m_totalMobileCombatPower[targetType]));
	}

	return threatByTargetType;
}

TargetTypeValues AAIBrain::DetermineCombatPowerVsTargetType() const
{
	//-----------------------------------------------------------------------------------------------------------------
	// determine highest threat
	//-----------------------------------------------------------------------------------------------------------------
	const MobileTargetTypeValues threatByTargetType = DetermineThreatByTargetType();

	const ThreatByTargetType highestEnemyThreat     = AAIHelperFunctions::DetermineHighestThreat(threatByTargetType);

	//-----------------------------------------------------------------------------------------------------------------
	// set desired combat power depending on highest threat
	//-----------------------------------------------------------------------------------------------------------------

	TargetTypeValues combatPowerVsTargetType(0.0f);

	switch(highestEnemyThreat.TargetType())
	{
		case ETargetType::SURFACE:
		{
			combatPowerVsTargetType[ETargetType::SURFACE]   = threatByTargetType[ETargetType::SURFACE];
			break;
		}
		case ETargetType::AIR:
		{
			combatPowerVsTargetType[ETargetType::AIR]       = threatByTargetType[ETargetType::AIR];
			break;
		}
		case ETargetType::FLOATER: // fallthrough intended
			[[fallthrough]];
		case ETargetType::SUBMERGED:
		{
			combatPowerVsTargetType[ETargetType::FLOATER]   = threatByTargetType[ETargetType::FLOATER];
			combatPowerVsTargetType[ETargetType::SUBMERGED] = threatByTargetType[ETargetType::SUBMERGED];
			break;
		}
	}

	//! @todo Refine criteria - for now set combat power vs static units (i.e. enemy defences) based on current pressure
	combatPowerVsTargetType[ETargetType::STATIC] = (combatPowerVsTargetType[ETargetType::SURFACE] + combatPowerVsTargetType[ETargetType::FLOATER]) * (1.0f - m_estimatedPressureByEnemies);

	/*for(const auto& targetType : AAITargetType::m_targetTypes)
		ai->Log("%s: -> %f   ", AAITargetType(targetType).GetName().c_str(), combatPowerVsTargetType[targetType] );
	ai->Log("\n");*/

	return combatPowerVsTargetType;
}

AAIMovementType AAIBrain::DetermineMovementTypeForCombatUnitConstruction(const GamePhase& gamePhase) const
{
	AAIMovementType moveType;

	// boost air craft ratio if many possible targets for bombing run identified (boost factor between 0.75 and 1.5)
	const float dynamicAirCraftRatio = cfg->AIRCRAFT_RATIO * (0.75f * (1.0f + ai->AirForceMgr()->GetNumberOfBombTargets()));

	if( IsRandomNumberBelow(dynamicAirCraftRatio) && !gamePhase.IsStartingPhase())
	{
		moveType.SetMovementType(EMovementType::MOVEMENT_TYPE_AIR);
	}
	else
	{
		moveType.AddMovementType(EMovementType::MOVEMENT_TYPE_HOVER);

		int enemyBuildingsOnLand, enemyBuildingsOnSea;
		ai->Map()->DetermineSpottedEnemyBuildingsOnContinentType(enemyBuildingsOnLand, enemyBuildingsOnSea);

		const float totalBuildings = enemyBuildingsOnLand + enemyBuildingsOnSea;
		const float offshoreBuildingRatio = (totalBuildings > 0) ? static_cast<float>(enemyBuildingsOnSea) / static_cast<float>(totalBuildings) : 0.5f;

		// ratio of sea units is determined: 40% by  water ratio on map, 60 % ratio of enemy buildings on sea
		float waterUnitRatio = 0.4f * AAIMap::s_waterTilesRatio + 0.6f * offshoreBuildingRatio;

		if(IsRandomNumberBelow(waterUnitRatio) )
		{
			moveType.AddMovementType(EMovementType::MOVEMENT_TYPE_SEA_FLOATER);
			moveType.AddMovementType(EMovementType::MOVEMENT_TYPE_SEA_SUBMERGED);
		}
		else
		{
			moveType.AddMovementType(EMovementType::MOVEMENT_TYPE_AMPHIBIOUS);

			if(IsRandomNumberBelow(1.0f - waterUnitRatio))
				moveType.AddMovementType(EMovementType::MOVEMENT_TYPE_GROUND);
		}
	}
	
	return moveType;
}

UnitSelectionCriteria AAIBrain::DetermineCombatUnitSelectionCriteria() const
{
	UnitSelectionCriteria unitSelectionCriteria;

	// income factor ranges from 1.0 (no metal income) to 0.0 (high metal income)
	const float metalIncome  = m_metalIncome.GetAverageValue();
	const float incomeFactor = 1.0f / (0.01f * metalIncome*metalIncome + 1.0f);

	// cost ranges from 0.5 (excess metal, low threat level) to 2.5 (low metal)
	unitSelectionCriteria.cost       = 0.5f + 2.0f * incomeFactor;

	// power ranges from 1.0 (low income) to 2.5 (high income, high enemy pressure)
	unitSelectionCriteria.power      = 1.0f + 1.0f * (1.0f - incomeFactor) + 0.5f * m_estimatedPressureByEnemies;

	// efficiency ranges form 0.25 (high income, low threat level) to 1.5 (low income, high threat level)
	unitSelectionCriteria.efficiency = 0.25f + 0.5f * m_estimatedPressureByEnemies + 0.75f * incomeFactor;

	unitSelectionCriteria.factoryUtilization = 1.5f;

	const GamePhase gamePhase(ai->GetAICallback()->GetCurrentFrame());

	if(gamePhase.IsStartingPhase())
	{
		unitSelectionCriteria.speed      = 0.35f;
		unitSelectionCriteria.range      = 0.25f;
	}
	else
	{
		if( IsRandomNumberBelow(cfg->FAST_UNITS_RATIO) )
		{
			// speed in 0.5 to 1.5
			const float speed = static_cast<float>(rand()%6);
			unitSelectionCriteria.speed = 0.5f + 0.2f * speed;
		}
		else
		{
			// speed in 0.1 to 0.5
			const float speed = static_cast<float>(rand()%5);
			unitSelectionCriteria.speed = 0.1f + 0.1f * speed;
		}

		if( IsRandomNumberBelow(cfg->HIGH_RANGE_UNITS_RATIO) )
		{
			// range in 0.5 to 1.5
			const float range = static_cast<float>(rand()%6);
			unitSelectionCriteria.range = 0.5f + 0.2f * range;
		}
		else
		{
			// range in 0.1 to 0.5
			const float range = static_cast<float>(rand()%5);
			unitSelectionCriteria.range = 0.1f + 0.1f * range;
		}
	}

	return unitSelectionCriteria;
}

MobileTargetTypeValues AAIBrain::GetAttacks(const GamePhase& gamePhase) const
{
	return MobileTargetTypeValues(0.3f, s_attackedByRates.GetAttackedByRates(gamePhase), 0.7f, m_recentlyAttackedByRates);
}

float AAIBrain::GetAttacksBy(const AAITargetType& targetType, const GamePhase& gamePhase) const
{
	return (  0.3f * s_attackedByRates.GetAttackedByRate(gamePhase, targetType) 
	        + 0.7f * m_recentlyAttackedByRates[targetType] );
}

void AAIBrain::UpdatePressureByEnemy(const SectorMap& sectors)
{
	int sectorsOccupiedByEnemies(0);
	int sectorsNearBaseOccupiedByEnemies(0);

	for(size_t x = 0; x < sectors.size(); ++x)
	{
		for(size_t y = 0; y < sectors[x].size(); ++y)
		{
			if(sectors[x][y].IsOccupiedByEnemies())
			{
				++sectorsOccupiedByEnemies;

				if(sectors[x][y].GetDistanceToBase() < 2)
					++sectorsNearBaseOccupiedByEnemies;
			}
		}
	}

	const float sectorsWithEnemiesRatio         = static_cast<float>(sectorsOccupiedByEnemies)         / static_cast<float>(AAIMap::xSectors * AAIMap::ySectors);
	const float sectorsNearBaseWithEnemiesRatio = static_cast<float>(sectorsNearBaseOccupiedByEnemies) / static_cast<float>( m_sectorsInDistToBase[0].size() + m_sectorsInDistToBase[1].size() );

	m_estimatedPressureByEnemies = 2.0f * sectorsWithEnemiesRatio + 2.0f * sectorsNearBaseWithEnemiesRatio;

	if(m_estimatedPressureByEnemies > 1.0f)
		m_estimatedPressureByEnemies = 1.0f;

	//ai->Log("Current enemy pressure: %f  - map: %i - %f    near base: %i - %f\n", m_estimatedPressureByEnemies, sectorsOccupiedByEnemies , sectorsWithEnemiesRatio, sectorsNearBaseOccupiedByEnemies, sectorsNearBaseWithEnemiesRatio);
}

float AAIBrain::GetAveragePowerSurplus() const
{
	const AAIUnitStatistics& unitStatistics      = ai->s_buildTree.GetUnitStatistics(ai->GetSide());
	const StatisticalData&   generatedPowerStats = unitStatistics.GetUnitPrimaryAbilityStatistics(EUnitCategory::POWER_PLANT);

	return std::max(1.0f, m_energySurplus.GetAverageValue() + 0.03f * m_energyAvailable.GetAverageValue() - 2.0f * generatedPowerStats.GetMinValue());
}

float AAIBrain::GetEnergyUrgency() const
{
	const float avgPowerSurplus = GetAveragePowerSurplus();

	if(avgPowerSurplus > AAIConstants::powerSurplusToStopPowerPlantConstructionThreshold)
		return 0.0f;	
	else 
	{
		// urgency should range from 5 (little income & suplus) towards low values when surplus is large compared to generated energy
		return (0.04f * m_energyIncome.GetAverageValue() + 5.0f) / avgPowerSurplus;
	}
}

float AAIBrain::GetMetalUrgency() const
{
	if(ai->UnitTable()->GetNumberOfActiveUnitsOfCategory(AAIUnitCategory(EUnitCategory::METAL_EXTRACTOR)) > 0)
		return 4.0f / (2.0f * m_metalSurplus.GetAverageValue() + 0.5f);
	else
		return 8.0f;
}

float AAIBrain::GetEnergyStorageUrgency() const
{
	if(    (ai->UnitTable()->GetNumberOfActiveUnitsOfCategory(EUnitCategory::STORAGE) < cfg->MAX_STORAGE)
		&& (ai->UnitTable()->GetNumberOfFutureUnitsOfCategory(EUnitCategory::STORAGE) <= 0)
		&& (ai->UnitTable()->activeFactories >= cfg->MIN_FACTORIES_FOR_STORAGE) )
	{
		const float energyStorage = std::max(ai->GetAICallback()->GetEnergyStorage(), 1.0f);
		
		// urgency ranges from 0 (no energy stored) to 0.3 (storage full)
		return 0.3f * m_energyAvailable.GetAverageValue() / energyStorage;
	}
	else
		return 0.0f;
}

float AAIBrain::GetMetalStorageUrgency() const
{
	const float unusedMetalStorage = ai->GetAICallback()->GetMetalStorage() - ai->GetAICallback()->GetMetal();

	if(    (ai->UnitTable()->GetNumberOfActiveUnitsOfCategory(EUnitCategory::STORAGE) < cfg->MAX_STORAGE)
		&& (ai->UnitTable()->GetNumberOfFutureUnitsOfCategory(EUnitCategory::STORAGE) <= 0)
		&& (ai->UnitTable()->activeFactories >= cfg->MIN_FACTORIES_FOR_STORAGE) )
	{
		const float metalStorage = std::max(ai->GetAICallback()->GetMetalStorage(), 1.0f);

		// urgency ranges from 0 (no energy stored) to 1 (storage full)
		return m_metalAvailable.GetAverageValue() / metalStorage;
	}
	else
		return 0.0f;
}

bool AAIBrain::SufficientResourcesToAssistsConstructionOf(UnitDefId defId) const
{
	const AAIUnitCategory& category = ai->s_buildTree.GetUnitCategory(defId);

	if(  category.IsMetalExtractor() || category.IsPowerPlant() )
		return true;
	else if(   (m_metalSurplus.GetAverageValue()  > AAIConstants::minMetalSurplusForConstructionAssist)
			&& (m_energySurplus.GetAverageValue() > AAIConstants::minEnergySurplusForConstructionAssist) )
	{
		return true;
	}

	return false;
}

float AAIBrain::DetermineConstructionUrgencyOfFactory(UnitDefId factoryDefId, const TargetTypeValues& combatPowerVsTargetType) const
{
	const AAIMovementType& moveType = ai->s_buildTree.GetMovementType(factoryDefId);

	float terrainModifier(1.0f);

	if(moveType.IsSea())
	{
		terrainModifier = (0.3f + 0.35f * (AAIMap::s_waterTilesRatio + m_baseWaterRatio) );
	}
	else if(moveType.IsGround() || moveType.IsStaticLand())
	{
		terrainModifier = (0.3f + 0.35f * (AAIMap::s_landTilesRatio  + m_baseFlatLandRatio) );
	}

	// cost factor ranges from 2.0 (no metal income) to 0.5 (high metal income)
	const float metalIncome = m_metalIncome.GetAverageValue();
	const float costFactor  = 1.5f / (0.01f * metalIncome*metalIncome + 1.0f) + 0.5f;

	// cost rating between 0 (most expensive factory) and -> cost factor (for cheap factories)
	const StatisticalData& costs = ai->s_buildTree.GetUnitStatistics(ai->GetSide()).GetUnitCostStatistics(EUnitCategory::STATIC_CONSTRUCTOR);
	const float costRating       = costFactor * costs.GetDeviationFromMax( ai->s_buildTree.GetTotalCost(factoryDefId) );

	// between 3 (no active factories of that type) and -> 0 (many active factories of that type)
	const float numberOfExistingFactoriesRating = 3.0f / static_cast<float>(ai->BuildTable()->GetDynamicUnitTypeData(factoryDefId).active + 1);

	const float rating = terrainModifier * (ai->BuildTable()->DetermineFactoryRating(factoryDefId, combatPowerVsTargetType) + costRating + numberOfExistingFactoriesRating);

	/*ai->Log("%s: Factory Rating: %f   Cost rating: %f    Map modifier: %f    Final rating: %f \n", 	ai->s_buildTree.GetUnitTypeProperties(factoryDefId).m_name.c_str(),
																									ai->BuildTable()->DetermineFactoryRating(factoryDefId, combatPowerVsTargetType), 
																									costs.GetDeviationFromMax( ai->s_buildTree.GetTotalCost(factoryDefId) ),
																									modifier, rating);*/

	return rating;
}

ScoutSelectionCriteria AAIBrain::DetermineScoutSelectionCriteria() const
{
	ScoutSelectionCriteria selectionCriteria;
	// income factor ranges from 1.0 (no metal income) to 0.0 (high metal income)
	const float metalIncome  = m_metalIncome.GetAverageValue();
	const float incomeFactor = 1.0f / (0.01f * metalIncome*metalIncome + 1.0f);

	// cost ranges from 0.5 (excess metal, low threat level) to 3 (low metal)
	selectionCriteria.cost   = 0.5f + 2.5f * incomeFactor;

	const GamePhase gamePhase(ai->GetAICallback()->GetCurrentFrame());

	if(gamePhase.IsStartingPhase())
	{
		selectionCriteria.speed      = 1.0f;
		selectionCriteria.sightRange = 0.6f;
		selectionCriteria.cloakable  = 0.0f;
	}
	else
	{
		// speed in 0.5 to 1.5
		selectionCriteria.speed      = 0.5f + 0.2f * static_cast<float>(rand()%6);

		// range in 0.5 to 2.0
		selectionCriteria.sightRange = 0.5f + 0.3f * static_cast<float>(rand()%6);

		// cloakable in 0 to 1
		selectionCriteria.cloakable  = 0.0f + 0.25f * static_cast<float>(rand()%4);
	}

	return selectionCriteria;
}

PowerPlantSelectionCriteria AAIBrain::DeterminePowerPlantSelectionCriteria() const
{
	const float numberOfBuildingsFactor = std::tanh(0.2f * static_cast<float>(ai->UnitTable()->GetTotalNumberOfUnitsOfCategory(EUnitCategory::POWER_PLANT)) - 2.0f);

	// importance of buildtime ranges between 3 (no excess energy and no plants) to close to 0.25 (sufficient excess energy)
	const float urgency   = (0.04f * m_energyIncome.GetAverageValue() + 0.1f) / GetAveragePowerSurplus();
	const float buildtime = std::min(urgency + 0.25f,  1.75f - 1.25f * numberOfBuildingsFactor);

	// importance of generated power ranges from 0.25 (no power plants) to 2.25f (many power plants)
	const float generatedPower = 1.25f + numberOfBuildingsFactor;

	// cost ranges from 2 (no power plant) to 0.5 (many power plants)
	const float cost = 1.25f - 0.75f * numberOfBuildingsFactor;

	//ai->Log("Power plant selection: income %f   surplus %f   available %f", m_energyIncome.GetAverageValue(), GetAveragEnergySurplus(), 0.03f * m_energyAvailable.GetAverageValue());
	//ai->Log("-> cost %f, buildtime %f, power %f\n", cost, buildtime, generatedPower);

	return PowerPlantSelectionCriteria(cost, buildtime, generatedPower, m_energyIncome.GetAverageValue());
}

StorageSelectionCriteria AAIBrain::DetermineStorageSelectionCriteria() const
{
	const float numberOfBuildingsFactor = std::tanh(static_cast<float>(ai->UnitTable()->GetTotalNumberOfUnitsOfCategory(EUnitCategory::STORAGE)) - 2.0f);

	const float metalStorage = std::max(ai->GetAICallback()->GetMetalStorage(), 1.0f);
	const float usedMetalStorageCapacity = std::min(1.1f * m_metalAvailable.GetAverageValue() / metalStorage, 1.0f);

	const float energyStorage = std::max(ai->GetAICallback()->GetEnergyStorage(), 1.0f);
	const float usedEnergyStorageCapacity = m_energyAvailable.GetAverageValue() / energyStorage;

	// storedMetal/Energy ranges from 0 (no storage capacity used) to 0.5 (storage full, no storages) - 2.0 (storage full > 4 storages)
	const float storedMetal  = (1.5f  +         numberOfBuildingsFactor) * usedMetalStorageCapacity;
	const float storedEnergy = (1.25f + 0.75f * numberOfBuildingsFactor) * usedEnergyStorageCapacity;

	// cost ranges from 2.0f (no storages) to ~0.5 (> 4 storages)
	const float cost = 1.25f - 0.75f * numberOfBuildingsFactor;
	const float buildtime (cost); 

	return StorageSelectionCriteria(cost, buildtime, storedMetal, storedEnergy);
}

ExtractorSelectionCriteria AAIBrain::DetermineExtractorSelectionCriteria() const
{
	// income factor ranges from 1.0 (no metal income) to 0.0 (high metal income)
	const float metalIncome  = m_metalIncome.GetAverageValue();
	const float incomeFactor = 1.0f / (0.01f * metalIncome*metalIncome + 1.0f);

	// cost ranges from 0.5 (excess metal, high defence power) to 2.0 (low metal, low defence power)
	const float cost           = 0.5f + 1.5f * incomeFactor;
	const float extractedMetal = 0.2f + 1.8f * (1.0f - incomeFactor);

	return ExtractorSelectionCriteria(cost, extractedMetal, 0.0f);
}

StaticDefenceSelectionCriteria AAIBrain::DetermineStaticDefenceSelectionCriteria(const AAISector* sector, const AAITargetType& targetType) const
{
	// defence factor ranges from 0.0 (high defence power vs given target type) to 1 (no defence power)
	const float defenceFactor    = exp(- sector->GetFriendlyStaticDefencePower(targetType) / 6.0f);

	// number of defences factor ranges from 0.0 (~ 10 static defences) to 1 (no static defences)
	const float numberOfDefencesFactor = exp( - static_cast<float>(sector->GetNumberOfBuildings(EUnitCategory::STATIC_DEFENCE)) / 3.0f );

	// income factor ranges from 1.0 (no metal income) to 0.0 (high metal income)
	const float metalIncome  = m_metalIncome.GetAverageValue();
	const float incomeFactor = 1.0f / (0.01f * metalIncome*metalIncome + 1.0f);

	// cost ranges from 0.5 (excess metal, high defence power) to 4.0 (low metal, low defence power)
	const float cost        = 0.5f + 2.75f * incomeFactor + 0.75f * defenceFactor;

	// power ranges from 1.5 (low income) to 3.0 (high income, low defence power & high enemy pressure)
	const float combatPower = 1.5f + 0.25f * (1.0f - incomeFactor) + 0.75f * (1.0f - numberOfDefencesFactor) + 0.5f * m_estimatedPressureByEnemies;

	// buildtimes ranges form 0.25 (high income, low threat level) to 2.0 (low income, low defence power/high threat level)
	const float buildtime = 0.25f + 0.25f * m_estimatedPressureByEnemies + 1.5f * defenceFactor;

	// range ranges from 0.1 to 1.5, depending on ratio of units with high ranges
	float range;
	if( IsRandomNumberBelow(cfg->HIGH_RANGE_UNITS_RATIO) && (sector->GetNumberOfBuildings(EUnitCategory::STATIC_DEFENCE) > 1) )
	{
		// range in 0.5 to 1.5
		range = 0.5f + 0.2f * static_cast<float>(rand()%6);
	}
	else
	{
		// range in 0.1 to 0.5
		range = 0.1f + 0.1f * static_cast<float>(rand()%5);
	}

	// importance of terrain (for placement of defence) depends on range
	float terrain = 0.1f + 1.25f * range;

	if(sector->GetDistanceToBase() > 1)
		terrain += 1.0f;

	const int randomness = 3;

	return StaticDefenceSelectionCriteria(targetType, combatPower, range, cost, buildtime, terrain, randomness);
}
