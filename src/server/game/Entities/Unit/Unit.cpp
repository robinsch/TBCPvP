/*
 * Copyright (C) 2010-2012 Project SkyFire <http://www.projectskyfire.org/>
 * Copyright (C) 2010-2012 Oregon <http://www.oregoncore.com/>
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Common.h"
#include "Log.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "QuestDef.h"
#include "Player.h"
#include "Creature.h"
#include "Spell.h"
#include "Group.h"
#include "SpellAuras.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "CreatureAI.h"
#include "Formulas.h"
#include "Pet.h"
#include "Util.h"
#include "Totem.h"
#include "Battleground.h"
#include "OutdoorPvP.h"
#include "InstanceSaveMgr.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Path.h"
#include "CreatureGroups.h"
#include "PetAI.h"
#include "PassiveAI.h"
#include "Traveller.h"
#include "TemporarySummon.h"
#include "ScriptMgr.h"
#include "PathFinder.h"

#include <math.h>

float baseMoveSpeed[MAX_MOVE_TYPE] =
{
    2.5f,                  // MOVE_WALK
    7.0f,                  // MOVE_RUN
    4.5f,                  // MOVE_RUN_BACK
    4.722222f,             // MOVE_SWIM
    2.5f,                  // MOVE_SWIM_BACK
    3.141594f,             // MOVE_TURN_RATE
    7.0f,                  // MOVE_FLIGHT
    4.5f,                  // MOVE_FLIGHT_BACK
};

void InitTriggerAuraData();

// auraTypes contains attacker auras capable of proc'ing cast auras
static Unit::AuraTypeSet GenerateAttakerProcCastAuraTypes()
{
    static Unit::AuraTypeSet auraTypes;
    auraTypes.insert(SPELL_AURA_DUMMY);
    auraTypes.insert(SPELL_AURA_PROC_TRIGGER_SPELL);
    auraTypes.insert(SPELL_AURA_MOD_HASTE);
    auraTypes.insert(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
    return auraTypes;
}

// auraTypes contains victim auras capable of proc'ing cast auras
static Unit::AuraTypeSet GenerateVictimProcCastAuraTypes()
{
    static Unit::AuraTypeSet auraTypes;
    auraTypes.insert(SPELL_AURA_DUMMY);
    auraTypes.insert(SPELL_AURA_PRAYER_OF_MENDING);
    auraTypes.insert(SPELL_AURA_PROC_TRIGGER_SPELL);
    return auraTypes;
}

// auraTypes contains auras capable of proc effect/damage (but not cast) for attacker
static Unit::AuraTypeSet GenerateAttakerProcEffectAuraTypes()
{
    static Unit::AuraTypeSet auraTypes;
    auraTypes.insert(SPELL_AURA_MOD_DAMAGE_DONE);
    auraTypes.insert(SPELL_AURA_PROC_TRIGGER_DAMAGE);
    auraTypes.insert(SPELL_AURA_MOD_CASTING_SPEED);
    auraTypes.insert(SPELL_AURA_MOD_RATING);
    return auraTypes;
}

// auraTypes contains auras capable of proc effect/damage (but not cast) for victim
static Unit::AuraTypeSet GenerateVictimProcEffectAuraTypes()
{
    static Unit::AuraTypeSet auraTypes;
    auraTypes.insert(SPELL_AURA_MOD_RESISTANCE);
    auraTypes.insert(SPELL_AURA_PROC_TRIGGER_DAMAGE);
    auraTypes.insert(SPELL_AURA_MOD_PARRY_PERCENT);
    auraTypes.insert(SPELL_AURA_MOD_BLOCK_PERCENT);
    auraTypes.insert(SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN);
    return auraTypes;
}

static Unit::AuraTypeSet attackerProcCastAuraTypes = GenerateAttakerProcCastAuraTypes();
static Unit::AuraTypeSet attackerProcEffectAuraTypes = GenerateAttakerProcEffectAuraTypes();

static Unit::AuraTypeSet victimProcCastAuraTypes = GenerateVictimProcCastAuraTypes();
static Unit::AuraTypeSet victimProcEffectAuraTypes   = GenerateVictimProcEffectAuraTypes();

// auraTypes contains auras capable of proc'ing for attacker and victim
static Unit::AuraTypeSet GenerateProcAuraTypes()
{
    InitTriggerAuraData();

    Unit::AuraTypeSet auraTypes;
    auraTypes.insert(attackerProcCastAuraTypes.begin(), attackerProcCastAuraTypes.end());
    auraTypes.insert(attackerProcEffectAuraTypes.begin(), attackerProcEffectAuraTypes.end());
    auraTypes.insert(victimProcCastAuraTypes.begin(), victimProcCastAuraTypes.end());
    auraTypes.insert(victimProcEffectAuraTypes.begin(), victimProcEffectAuraTypes.end());
    return auraTypes;
}

static Unit::AuraTypeSet procAuraTypes = GenerateProcAuraTypes();

bool IsPassiveStackableSpell(uint32 spellId)
{
    if (!IsPassiveSpell(spellId))
        return false;

    SpellEntry const* spellProto = sSpellStore.LookupEntry(spellId);
    if (!spellProto)
        return false;

    for (int j = 0; j < 3; ++j)
    {
        if (std::find(procAuraTypes.begin(), procAuraTypes.end(), spellProto->EffectApplyAuraName[j]) != procAuraTypes.end())
            return false;
    }

    return true;
}

void MovementInfo::Read(ByteBuffer &data)
{
    data >> moveFlags;
    data >> moveFlags2;
    data >> time;
    data >> pos.m_positionX;
    data >> pos.m_positionY;
    data >> pos.m_positionZ;
    data >> pos.m_orientation;

    if (HasMovementFlag(MOVEFLAG_ONTRANSPORT))
    {
        data >> t_guid;
        data >> t_pos.m_positionX;
        data >> t_pos.m_positionY;
        data >> t_pos.m_positionZ;
        data >> t_pos.m_orientation;
        data >> t_time;
    }
    if (HasMovementFlag(MovementFlags(MOVEFLAG_SWIMMING | MOVEFLAG_FLYING2)))
        data >> s_pitch;

    data >> fallTime;

    if (HasMovementFlag(MOVEFLAG_FALLING))
    {
        data >> j_velocity;
        data >> j_sinAngle;
        data >> j_cosAngle;
        data >> j_xyspeed;
    }

    if (HasMovementFlag(MOVEFLAG_SPLINE_ELEVATION))
        data >> u_unk1;
}

void MovementInfo::Write(ByteBuffer &data) const
{
    data << moveFlags;
    data << moveFlags2;
    data << time;
    data << pos.GetPositionX();
    data << pos.GetPositionY();
    data << pos.GetPositionZ();
    data << pos.GetOrientation();

    if (HasMovementFlag(MOVEFLAG_ONTRANSPORT))
    {
        data << t_guid;
        data << t_pos.GetPositionX();
        data << t_pos.GetPositionY();
        data << t_pos.GetPositionZ();
        data << t_pos.GetOrientation();
        data << t_time;
    }
    if (HasMovementFlag(MovementFlags(MOVEFLAG_SWIMMING | MOVEFLAG_FLYING2)))
        data << s_pitch;

    data << fallTime;

    if (HasMovementFlag(MOVEFLAG_FALLING))
    {
        data << j_velocity;
        data << j_sinAngle;
        data << j_cosAngle;
        data << j_xyspeed;
    }

    if (HasMovementFlag(MOVEFLAG_SPLINE_ELEVATION))
        data << u_unk1;
}

Unit::Unit()
: WorldObject(), i_motionMaster(this), m_ThreatManager(this), m_HostileRefManager(this)
, IsAIEnabled(false), NeedChangeAI(false)
, i_AI(NULL), i_disabledAI(NULL), m_removedAurasCount(0), m_procDeep(0)
, m_ControlledByPlayer(false), m_lastSanctuaryTime(0)
{
    m_objectType |= TYPEMASK_UNIT;
    m_objectTypeId = TYPEID_UNIT;

    m_updateFlag = (UPDATEFLAG_HIGHGUID | UPDATEFLAG_LIVING | UPDATEFLAG_HAS_POSITION);

    m_attackTimer[BASE_ATTACK] = 0;
    m_attackTimer[OFF_ATTACK] = 0;
    m_attackTimer[RANGED_ATTACK] = 0;
    m_modAttackSpeedPct[BASE_ATTACK] = 1.0f;
    m_modAttackSpeedPct[OFF_ATTACK] = 1.0f;
    m_modAttackSpeedPct[RANGED_ATTACK] = 1.0f;
    m_modCastingSpeedPctPositive = 1.0f;
    m_modCastingSpeedPctNegative = 1.0f;

    m_extraAttacks = 0;
    m_canDualWield = false;

    m_rootTimes = 0;

    m_state = 0;
    m_form = FORM_NONE;
    m_deathState = ALIVE;

    for (uint8 i = 0; i < CURRENT_MAX_SPELL; ++i)
        m_currentSpells[i] = NULL;

    m_addDmgOnce = 0;

    for (uint8 i = 0; i < MAX_SUMMON_SLOT; ++i)
        m_SummonSlot[i] = 0;

    m_ObjectSlot[0] = m_ObjectSlot[1] = m_ObjectSlot[2] = m_ObjectSlot[3] = 0;

    m_AurasUpdateIterator = m_Auras.end();

    m_interruptMask = 0;
    m_transform = 0;
    m_ShapeShiftFormSpellId = 0;
    m_canModifyStats = false;

    for (uint8 i = 0; i < MAX_SPELL_IMMUNITY; ++i)
        m_spellImmune[i].clear();
    for (uint8 i = 0; i < UNIT_MOD_END; ++i)
    {
        m_auraModifiersGroup[i][BASE_VALUE] = 0.0f;
        m_auraModifiersGroup[i][BASE_PCT] = 1.0f;
        m_auraModifiersGroup[i][TOTAL_VALUE] = 0.0f;
        m_auraModifiersGroup[i][TOTAL_PCT] = 1.0f;
    }
                                                            // implement 50% base damage from offhand
    m_auraModifiersGroup[UNIT_MOD_DAMAGE_OFFHAND][TOTAL_PCT] = 0.5f;

    for (uint8 i = 0; i < 3; ++i)
    {
        m_weaponDamage[i][MINDAMAGE] = BASE_MINDAMAGE;
        m_weaponDamage[i][MAXDAMAGE] = BASE_MAXDAMAGE;
    }
    for (uint8 i = 0; i < MAX_STATS; ++i)
        m_createStats[i] = 0.0f;

    m_attacking = NULL;
    m_modMeleeHitChance = 0.0f;
    m_modRangedHitChance = 0.0f;
    m_modSpellHitChance = 0.0f;
    m_baseSpellCritChance = 5;

    m_CombatTimer = 0;
    m_lastManaUse = 0;

    //m_victimThreat = 0.0f;
    for (uint8 i = 0; i < MAX_SPELL_SCHOOL; ++i)
        m_threatModifier[i] = 1.0f;
    m_isSorted = true;
    for (uint8 i = 0; i < MAX_MOVE_TYPE; ++i)
        m_speed_rate[i] = 1.0f;

    m_charmInfo = NULL;
    m_unit_movement_flags = 0;
    m_reducedThreatPercent = 0;
    m_misdirectionTargetGUID = 0;
    m_stalkerGUID = 0;

    // remove aurastates allowing special moves
    for (uint8 i = 0; i < MAX_REACTIVE; ++i)
        m_reactiveTimer[i] = 0;
}

Unit::~Unit()
{
    // set current spells as deletable
    for (uint8 i = 0; i < CURRENT_MAX_SPELL; ++i)
    {
        if (m_currentSpells[i])
        {
            m_currentSpells[i]->SetReferencedFromCurrent(false);
            m_currentSpells[i] = NULL;
        }
    }

    RemoveAllGameObjects();
    RemoveAllDynObjects();
    _DeleteAuras();

    delete m_charmInfo;

    ASSERT(!m_attacking);
    ASSERT(m_attackers.empty());
    ASSERT(m_sharedVision.empty());
}

void Unit::Update(uint32 p_time)
{
    // WARNING! Order of execution here is important, do not change.
    // Spells must be processed with event system BEFORE they go to _UpdateSpells.
    // Or else we may have some SPELL_STATE_FINISHED spells stalled in pointers, that is bad.
    m_Events.Update(p_time);

    if (!IsInWorld())
        return;

    _UpdateSpells(p_time);

    // update combat timer
    if (isInCombat() && (GetTypeId() == TYPEID_PLAYER) || ((GetTypeId() == TYPEID_UNIT) && ToCreature()->isPet() && IsControlledByPlayer()))
    {
        // Check UNIT_STAT_MELEE_ATTACKING or UNIT_STAT_CHASE (without UNIT_STAT_FOLLOW in this case) so pets can reach far away
        // targets without stopping half way there and running off.
        // These flags are reset after target dies or another command is given.
        if (m_HostileRefManager.isEmpty())
        {
            if (m_CombatTimer <= p_time)
                ClearInCombat();
            else
                m_CombatTimer -= p_time;
        }
    }

    // update forced visibility
    if (m_forcedVisibilityTimer > 0)
    {
        if (m_forcedVisibilityTimer <= p_time)
        {
            m_forcedVisibilityTimer = 0;
            UpdateObjectVisibility();
        }
        else
            m_forcedVisibilityTimer -= p_time;
    }

    // not implemented before 3.0.2
    if (uint32 base_att = getAttackTimer(BASE_ATTACK))
        setAttackTimer(BASE_ATTACK, (p_time >= base_att ? 0 : base_att - p_time));
    if (uint32 ranged_att = getAttackTimer(RANGED_ATTACK))
        setAttackTimer(RANGED_ATTACK, (p_time >= ranged_att ? 0 : ranged_att - p_time));
    if (uint32 off_att = getAttackTimer(OFF_ATTACK))
        setAttackTimer(OFF_ATTACK, (p_time >= off_att ? 0 : off_att - p_time));

    // update abilities available only for fraction of time
    UpdateReactives(p_time);

    ModifyAuraState(AURA_STATE_HEALTHLESS_20_PERCENT, GetHealth() < GetMaxHealth() * 0.20f);
    ModifyAuraState(AURA_STATE_HEALTHLESS_35_PERCENT, GetHealth() < GetMaxHealth() * 0.35f);

    i_motionMaster.UpdateMotion(p_time);
}

bool Unit::haveOffhandWeapon() const
{
    if (GetTypeId() == TYPEID_PLAYER)
        return ToPlayer()->GetWeaponForAttack(OFF_ATTACK, true);
    else
        return m_canDualWield;
}

void Unit::SendMonsterMoveWithSpeedToCurrentDestination(Player* player)
{
    float x, y, z;
    if (GetMotionMaster()->GetDestination(x, y, z))
        SendMonsterMoveWithSpeed(x, y, z, 0, player);
}

void Unit::SendMonsterMoveWithSpeed(float x, float y, float z, uint32 transitTime, Player* player)
{
    if (!transitTime)
    {
        if (GetTypeId() == TYPEID_PLAYER)
        {
            Traveller<Player> traveller(*ToPlayer());
            transitTime = traveller.GetTotalTrevelTimeTo(x, y, z);
        }
        else
        {
            Traveller<Creature> traveller(*ToCreature());
            transitTime = traveller.GetTotalTrevelTimeTo(x, y, z);
        }
    }
    //float orientation = (float)atan2((double)dy, (double)dx);
    SendMonsterMove(x, y, z, transitTime, player);
}

void Unit::SendMonsterStop()
{
    WorldPacket data(SMSG_MONSTER_MOVE, (17 + GetPackGUID().size()));
    data << GetPackGUID();
    data << GetPositionX() << GetPositionY() << GetPositionZ();
    data << getMSTime();
    data << uint8(1);
    SendMessageToSet(&data, true);

    clearUnitState(UNIT_STAT_MOVE);
}

void Unit::SendMonsterMove(float NewPosX, float NewPosY, float NewPosZ, uint32 Time, Player* player)
{
    WorldPacket data(SMSG_MONSTER_MOVE, 12+4+1+4+4+4+12+GetPackGUID().size());
    data << GetPackGUID();

    data << GetPositionX() << GetPositionY() << GetPositionZ();
    data << getMSTime();

    data << uint8(0);
    data << uint32((GetUnitMovementFlags() & MOVEFLAG_LEVITATING) ? SPLINEFLAG_FLYING : SPLINEFLAG_WALKMODE);
    data << Time;                                           // Time in between points
    data << uint32(1);                                      // 1 single waypoint
    data << NewPosX << NewPosY << NewPosZ;                  // the single waypoint Point B

    if (player)
        player->GetSession()->SendPacket(&data);
    else
        SendMessageToSet(&data, true);

    addUnitState(UNIT_STAT_MOVE);
}

void Unit::SendMonsterMove(float NewPosX, float NewPosY, float NewPosZ, uint32 MoveFlags, uint32 time, float speedZ, Player* player)
{
    WorldPacket data(SMSG_MONSTER_MOVE, 12+4+1+4+4+4+12+GetPackGUID().size());
    data << GetPackGUID();

    data << uint8(0);                                       // new in 3.1
    data << GetPositionX() << GetPositionY() << GetPositionZ();
    data << getMSTime();

    data << uint8(0);
    data << MoveFlags;

    if (MoveFlags & SPLINEFLAG_JUMP)
    {
        data << time;
        data << speedZ;
        data << (uint32)0; // walk time after jump
    }
    else
        data << time;

    data << uint32(1);                                      // 1 single waypoint
    data << NewPosX << NewPosY << NewPosZ;                  // the single waypoint Point B

    if (player)
        player->GetSession()->SendPacket(&data);
    else
        SendMessageToSet(&data, true);
}

bool Unit::CanMakePathTo(float x, float y, float z, float maxLen) const
{
    if (!IsInMap(this))
        return true;

    PathInfo path(this, x, y, z, false);

    if (path.getPathType() & PATHFIND_NORMAL)
    {
        PointPath pointPath = path.getFullPath();

        if (maxLen > 0.0f && pointPath.GetTotalLength() > maxLen)
            return false;

        return true;
    }
    else if (path.getPathType() & PATHFIND_INCOMPLETE)
        return false;
    else if(path.getPathType() & PATHFIND_NOPATH)
        return false;

    //NavMesh may not be working..
    return true;
}

void Unit::BuildHeartBeatMsg(WorldPacket *data) const
{
    data->Initialize(MSG_MOVE_HEARTBEAT, 32);
    *data << GetPackGUID();
    *data << uint32(((GetUnitMovementFlags() & MOVEFLAG_LEVITATING) || isInFlight())? (SPLINEFLAG_FLYING|SPLINEFLAG_WALKMODE) : GetUnitMovementFlags());
    *data << uint8(0);                                      // 2.3.0
    *data << uint32(getMSTime());                           // time
    *data << float(GetPositionX());
    *data << float(GetPositionY());
    *data << float(GetPositionZ());
    *data << float(GetOrientation());
    *data << uint32(0);
}

template<typename Elem, typename Node>
void Unit::SendMonsterMoveByPath(Path<Elem, Node> const& path, uint32 start, uint32 end, uint32 traveltime)
{
    uint32 pathSize = end - start;
    if (pathSize < 1)
    {
        SendMonsterMove(GetPositionX(), GetPositionY(), GetPositionZ(), 0);
        return;
    }

    if (pathSize == 1)
    {
        SendMonsterMove(path[start].x, path[start].y, path[start].z, traveltime);
        return;
    }

    WorldPacket data(SMSG_MONSTER_MOVE, (GetPackGUID().size()+4+4+4+4+1+4+4+4+pathSize*4*3));
    data << GetPackGUID();
    data << GetPositionX();
    data << GetPositionY();
    data << GetPositionZ();
    data << uint32(getMSTime());
    data << uint8(0);
    data << uint32(((GetUnitMovementFlags() & MOVEFLAG_LEVITATING) || isInFlight())? (SPLINEFLAG_FLYING|SPLINEFLAG_WALKMODE) : SPLINEFLAG_WALKMODE);
    data << uint32(traveltime);
    data << uint32(pathSize);

    if (GetUnitMovementFlags() & UnitFlags(SPLINEFLAG_FLYING))
    {
        // sending a taxi flight path
        for (uint32 i = start; i < end; ++i)
        {
            data << float(path[i].x);
            data << float(path[i].y);
            data << float(path[i].z);
        }
    }
    else
    {
        // sending a series of points
        // destination
        data << path[end-1].x;
        data << path[end-1].y;
        data << path[end-1].z;

        // all other points are relative to the center of the path
        float mid_X = (GetPositionX() + path[end-1].x) * 0.5f;
        float mid_Y = (GetPositionY() + path[end-1].y) * 0.5f;
        float mid_Z = (GetPositionZ() + path[end-1].z) * 0.5f;
        for (uint32 i = start; i < end - 1; ++i)
            data.appendPackXYZ(mid_X - path[i].x, mid_Y - path[i].y, mid_Z - path[i].z);
    }

    SendMessageToSet(&data, true);
    addUnitState(UNIT_STAT_MOVE);
}


void Unit::resetAttackTimer(WeaponAttackType type)
{
    m_attackTimer[type] = uint32(GetAttackTime(type) * m_modAttackSpeedPct[type]);
}

bool Unit::IsWithinCombatRange(const Unit *obj, float dist2compare) const
{
    if (!obj || !IsInMap(obj)) return false;

    float dx = GetPositionX() - obj->GetPositionX();
    float dy = GetPositionY() - obj->GetPositionY();
    float dz = GetPositionZ() - obj->GetPositionZ();
    float distsq = dx*dx + dy*dy + dz*dz;

    float sizefactor = GetCombatReach() + obj->GetCombatReach();
    float maxdist = dist2compare + sizefactor;

    return distsq < maxdist * maxdist;
}

bool Unit::IsWithinMeleeRange(Unit const* obj) const
{
    if (!obj || !IsInMap(obj) || !InSamePhase(obj))
        return false;

    float dx = GetPositionX() - obj->GetPositionX();
    float dy = GetPositionY() - obj->GetPositionY();
    float dz = GetPositionZ() - obj->GetPositionZ();
    float distsq = dx*dx + dy*dy + dz*dz;

    float maxdist = GetMeleeRange(obj);
    maxdist = std::max(maxdist, NOMINAL_MELEE_RANGE);

    if (obj && isMoving() && obj->isMoving() &&
        obj->GetTypeId() == TYPEID_PLAYER)
        maxdist += 5.0f / 3.0f;

    return distsq <= maxdist * maxdist;
}

float Unit::GetMeleeRange(Unit const* target) const
{
    float range = GetCombatReach() + target->GetCombatReach() + 4.0f / 3.0f;
    return std::max(range, NOMINAL_MELEE_RANGE);
}

void Unit::GetRandomContactPoint(const Unit* obj, float &x, float &y, float &z, float distance2dMin, float distance2dMax) const
{
    float combat_reach = GetCombatReach();
    if (combat_reach < 0.1) // sometimes bugged for players
    {
        //sLog->outError("Unit %u (Type: %u) has invalid combat_reach %f", GetGUIDLow(), GetTypeId(), combat_reach);
        //if (GetTypeId() == TYPEID_UNIT)
        //    sLog->outError("Creature entry %u has invalid combat_reach", ToCreature()->GetEntry());
        combat_reach = DEFAULT_COMBAT_REACH;
    }
    uint32 attacker_number = getAttackers().size();
    if (attacker_number > 0)
        --attacker_number;
    GetNearPoint(obj, x, y, z, obj->GetCombatReach(), distance2dMin+(distance2dMax-distance2dMin)*rand_norm()
        , GetAngle(obj) + (attacker_number ? (M_PI/2 - M_PI * rand_norm()) * float(attacker_number) / combat_reach / 3 : 0));
}

void Unit::RemoveMovementImpairingAuras()
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        if (sSpellMgr->GetSpellCustomAttr(iter->second->GetId()) & SPELL_ATTR_CU_MOVEMENT_IMPAIR)
            RemoveAura(iter);
        else
            ++iter;
    }
}

void Unit::RemoveSpellsCausingAura(AuraType auraType, uint32 except)
{
    if (auraType >= TOTAL_AURAS) return;
    AuraList::iterator iter, next;
    for (iter = m_modAuras[auraType].begin(); iter != m_modAuras[auraType].end(); iter = next)
    {
        next = iter;
        ++next;

        if ((*iter)->GetId() == except)
            continue;

        if (*iter)
        {
            RemoveAurasDueToSpell((*iter)->GetId());
            if (!m_modAuras[auraType].empty())
                next = m_modAuras[auraType].begin();
            else
                return;
        }
    }
}

void Unit::RemoveAuraTypeByCaster(AuraType auraType, uint64 casterGUID)
{
    if (auraType >= TOTAL_AURAS) return;

    for (AuraList::iterator iter = m_modAuras[auraType].begin(); iter != m_modAuras[auraType].end();)
    {
        Aura *aur = *iter;
        ++iter;

        if (aur)
        {
            uint32 removedAuras = m_removedAurasCount;
            RemoveAurasByCasterSpell(aur->GetId(), casterGUID);
            if (m_removedAurasCount > removedAuras + 1)
                iter = m_modAuras[auraType].begin();
        }
    }
}

void Unit::RemoveAurasWithInterruptFlags(uint32 flag, uint32 except)
{
    if (!(m_interruptMask & flag))
        return;

    // interrupt auras
    AuraList::iterator iter;
    for (iter = m_interruptableAuras.begin(); iter != m_interruptableAuras.end();)
    {
        Aura *aur = *iter;
        ++iter;
        if ((aur->GetSpellProto()->AuraInterruptFlags & flag) && (!except || aur->GetId() != except))
        {
            uint32 removedAuras = m_removedAurasCount;
            RemoveAurasDueToSpell(aur->GetId());
            if (m_removedAurasCount > removedAuras + 1)
                iter = m_interruptableAuras.begin();
        }
    }

    // interrupt channeled spell
    if (Spell* spell = m_currentSpells[CURRENT_CHANNELED_SPELL])
        if (spell->getState() == SPELL_STATE_CASTING
            && (spell->m_spellInfo->ChannelInterruptFlags & flag)
            && spell->m_spellInfo->Id != except)
            InterruptNonMeleeSpells(false);

    UpdateInterruptMask();
}

void Unit::UpdateInterruptMask()
{
    m_interruptMask = 0;
    for (AuraList::iterator i = m_interruptableAuras.begin(); i != m_interruptableAuras.end(); ++i)
            m_interruptMask |= (*i)->GetSpellProto()->AuraInterruptFlags;

    if (Spell* spell = m_currentSpells[CURRENT_CHANNELED_SPELL])
        if (spell->getState() == SPELL_STATE_CASTING)
            m_interruptMask |= spell->m_spellInfo->ChannelInterruptFlags;
}

bool Unit::HasAuraType(AuraType auraType) const
{
    return (!m_modAuras[auraType].empty());
}

bool Unit::HasAuraTypeWithFamilyFlags(AuraType auraType, uint32 familyName  , uint64 familyFlags) const
{
    if (!HasAuraType(auraType)) return false;
    AuraList const &auras = GetAurasByType(auraType);
    for (AuraList::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        if (SpellEntry const *iterSpellProto = (*itr)->GetSpellProto())
            if (iterSpellProto->SpellFamilyName == familyName && iterSpellProto->SpellFamilyFlags & familyFlags)
                return true;
    return false;
}

bool Unit::HasAuraTypeWithMiscValue(AuraType auraType, int32 miscValue) const
{
    AuraList const &auras = GetAurasByType(auraType);
    for (AuraList::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        if (SpellEntry const *iterSpellProto = (*itr)->GetSpellProto())
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (iterSpellProto->EffectMiscValue[i] == miscValue)
                    return true;

    return false;
}

void Unit::GetDispellableAuraList(Unit* caster, uint32 dispelMask, dispel_list& dispelList, bool checkPositiveWhenFriendly)
{
    Unit::AuraMap const& auras = GetAuras();
    for (Unit::AuraMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
    {
        Aura *aura = (*itr).second;

        // don't try to remove passive auras
        if (aura->IsPassive())
            continue;

        if (aura && (1 << aura->GetSpellProto()->Dispel) & dispelMask)
        {
            if (aura->GetSpellProto()->Dispel == DISPEL_MAGIC)
            {
                // do not remove positive auras if friendly target
                //               negative auras if non-friendly target
                if (checkPositiveWhenFriendly && aura->IsPositive() == IsFriendlyTo(caster))
                {
                    // Mind control works vise vera, allow to dispel negative debuffs if !friendly
                    if (!(aura->GetSpellProto()->SpellFamilyName == SPELLFAMILY_PRIEST &&
                        aura->GetSpellProto()->SpellFamilyFlags & (0x0000000000020000LL) &&
                        aura->GetSpellProto()->SpellIconID == 235))
                        continue;
                }
            }

            // 2.4.3 Patch Notes: "Dispel effects will no longer attempt to remove effects that have 100% dispel resistance."
            uint32 chance = aura->CalcDispelChance(this, !this->IsFriendlyTo(caster));
            if (!chance)
                continue;

            // Add every aura stack to dispel list
            for (uint32 stack_amount = 0; stack_amount < aura->GetStackAmount(); ++stack_amount)
                dispelList.push_back(aura);
        }
    }
}

/* Called by DealDamage for auras that have a chance to be dispelled on damage taken. */
void Unit::RemoveSpellbyDamageTaken(AuraType auraType, uint32 damage, DamageEffectType damagetype, uint32 spellId)
{
    // The chance to dispel an aura depends on the damage taken with respect to the casters level.
    // auras can't break from self damage
    if (damagetype == (NODAMAGE | SELF_DAMAGE))
        return;

    // don't increase damageTakenCounter when not having correct auratype
    if (!HasAuraType(auraType))
        return;

    // don't increase damageTakenCounter when having aura that should not break on damage or ...
    AuraList const& mRemoveAuraList = GetAurasByType(auraType);
    for (AuraList::const_iterator iter = mRemoveAuraList.begin(); iter != mRemoveAuraList.end(); ++iter)
    {
        if (SpellEntry const* iterSpellProto = (*iter)->GetSpellProto())
        {
            if (sSpellMgr->GetSpellCustomAttr(iterSpellProto->Id) & SPELL_ATTR_CU_DONT_BREAK_ON_DAMAGE)
                return;

            // ... damage spell is removable spell
            if (spellId && (*iter)->GetSpellProto()->Id == spellId)
                return;
        }
    }

    uint32 currentDamage = GetDamageTakenWithActiveAuraType(auraType);
    uint32 damageMultiplier = damage * (damagetype == DIRECT_DAMAGE ? 1.5f : 1.0f);
    SetDamageTakenWithActiveAuraType(SPELL_AURA_MOD_FEAR, currentDamage + damageMultiplier);

    // The chance to dispel an aura depends on the damage taken with respect to the casters level.
    uint32 calcDmg = getLevel() > 8 ? 25 * getLevel() + 150 : 50;
    uint32 maxDmg = getLevel() > 8 ? 50 * getLevel() : 50;
    uint32 minDmg = getLevel() > 8 ? 7 * getLevel() + 10 : 10;
    bool canBreak = true;

    switch (auraType)
    {
        case SPELL_AURA_MOD_FEAR:
        case SPELL_AURA_MOD_ROOT:
            if (damagetype == DOT && GetDamageTakenWithActiveAuraType(auraType) <= minDmg)
                canBreak = false;
            break;
        default:
            break;
    }

    float chance = float(damage) / calcDmg * 100.0f;
    if (canBreak && (roll_chance_f(chance) || GetDamageTakenWithActiveAuraType(auraType) >= maxDmg))
        RemoveSpellsCausingAura(auraType);
}

uint32 Unit::DealDamage(Unit *pVictim, uint32 damage, CleanDamage const* cleanDamage, DamageEffectType damagetype, SpellSchoolMask damageSchoolMask, SpellEntry const *spellProto, bool durabilityLoss)
{
    if (!pVictim->isAlive() || pVictim->isInFlight() || pVictim->GetTypeId() == TYPEID_UNIT && pVictim->ToCreature()->IsInEvadeMode())
        return 0;

    //You don't lose health from damage taken from another player while in a sanctuary
    //You still see it in the combat log though
    if (pVictim != this && GetTypeId() == TYPEID_PLAYER && pVictim->GetTypeId() == TYPEID_PLAYER)
    {
        const AreaTableEntry *area = GetAreaEntryByAreaID(pVictim->GetAreaId());
        if (area && area->flags & AREA_FLAG_SANCTUARY)       //sanctuary
            return 0;
    }

    // Signal to pets that their owner was attacked
    if (pVictim->GetTypeId() == TYPEID_PLAYER)
    {
        Pet* pet = pVictim->ToPlayer()->GetPet();

        if (pet && pet->isAlive())
            pet->AI()->OwnerDamagedBy(this);
    }

    //Script Event damage taken
    if (pVictim->GetTypeId() == TYPEID_UNIT && (pVictim->ToCreature())->IsAIEnabled)
    {
        pVictim->ToCreature()->AI()->DamageTaken(this, damage);

        // Set tagging
        if (!pVictim->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_OTHER_TAGGER) && !pVictim->ToCreature()->isPet())
        {
            //Set Loot
            switch (GetTypeId())
            {
                case TYPEID_PLAYER:
                {
                    pVictim->ToCreature()->SetLootRecipient(this);
                    //Set tagged
                    pVictim->ToCreature()->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_OTHER_TAGGER);
                    break;
                }
                case TYPEID_UNIT:
                {
                    if (ToCreature()->isPet())
                    {
                        pVictim->ToCreature()->SetLootRecipient(this->GetOwner());
                        pVictim->ToCreature()->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_OTHER_TAGGER);
                    }
                    break;
                }
            }
        }
    }

    // Cancel capture spells at any damage taken
    if (Spell* curSpell = pVictim->m_currentSpells[CURRENT_GENERIC_SPELL])
        if (damagetype == DIRECT_DAMAGE || damagetype == SPELL_DIRECT_DAMAGE)
            if (curSpell->m_spellInfo->Id == 21651 || curSpell->m_spellInfo->Id == 26868)
                pVictim->InterruptSpell(CURRENT_GENERIC_SPELL);

    if (damage || (cleanDamage && cleanDamage->damage))
    {
        if (spellProto)
        {
            if (!(spellProto->AttributesEx4 & SPELL_ATTR_EX4_DAMAGE_DOESNT_BREAK_AURAS))
            {
                pVictim->RemoveSpellbyDamageTaken(SPELL_AURA_MOD_FEAR, damage, damagetype, spellProto->Id);
                pVictim->RemoveSpellbyDamageTaken(SPELL_AURA_MOD_ROOT, damage, damagetype, spellProto->Id);
                pVictim->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_DAMAGE, spellProto->Id);
            }
        }
        else
        {
            pVictim->RemoveSpellbyDamageTaken(SPELL_AURA_MOD_FEAR, damage, damagetype);
            pVictim->RemoveSpellbyDamageTaken(SPELL_AURA_MOD_ROOT, damage, damagetype);
            pVictim->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_DAMAGE, 0);
        }

         // Rage from physical damage received
        if (!damage)
        {
            if ((damageSchoolMask & SPELL_SCHOOL_MASK_NORMAL) && pVictim->GetTypeId() == TYPEID_PLAYER && (pVictim->getPowerType() == POWER_RAGE))
                pVictim->ToPlayer()->RewardRage(damage, 0, false);
            return 0;
        }
    }

    if (pVictim->GetTypeId() != TYPEID_PLAYER)
    {
        // no xp, health if type 8 /critters/
        if (pVictim->GetCreatureType() == CREATURE_TYPE_CRITTER)
        {
            // allow loot only if has loot_id in creature_template
            if (damage >= pVictim->GetHealth())
            {
                pVictim->setDeathState(JUST_DIED);
                pVictim->SetHealth(0);

                CreatureTemplate const* cInfo = pVictim->ToCreature()->GetCreatureTemplate();
                if (cInfo && cInfo->lootid)
                    pVictim->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);

                // some critters required for quests
                if (GetTypeId() == TYPEID_PLAYER)
                    if (CreatureTemplate const* normalInfo = sObjectMgr->GetCreatureTemplate(pVictim->GetEntry()))
                        this->ToPlayer()->KilledMonster(normalInfo, pVictim->GetGUID());
            }
            else
                pVictim->ModifyHealth(- (int32)damage);

            return damage;
        }
    }

    sLog->outDebug("DealDamageStart");

    uint32 health = pVictim->GetHealth();
    sLog->outDetail("deal dmg:%d to health:%d ", damage, health);

    // duel ends when player has 1 or less hp
    bool duel_hasEnded = false;
    if (pVictim->GetTypeId() == TYPEID_PLAYER && pVictim->ToPlayer()->duel && damage >= (health-1))
    {
        // prevent kill only if killed in duel and killed by opponent or opponent controlled creature
        if (pVictim->ToPlayer()->duel->opponent == this || pVictim->ToPlayer()->duel->opponent->GetGUID() == GetOwnerGUID())
            damage = health-1;

        duel_hasEnded = true;
    }

    // Rage from Damage made (only from direct weapon damage)
    if (cleanDamage && damagetype == DIRECT_DAMAGE && this != pVictim && GetTypeId() == TYPEID_PLAYER && (getPowerType() == POWER_RAGE))
    {
        uint32 weaponSpeedHitFactor;

        switch (cleanDamage->attackType)
        {
            case BASE_ATTACK:
            {
                if (cleanDamage->hitOutCome == MELEE_HIT_CRIT)
                    weaponSpeedHitFactor = uint32(GetAttackTime(cleanDamage->attackType)/1000.0f * 7);
                else
                    weaponSpeedHitFactor = uint32(GetAttackTime(cleanDamage->attackType)/1000.0f * 3.5f);

                ToPlayer()->RewardRage(damage, weaponSpeedHitFactor, true);

                break;
            }
            case OFF_ATTACK:
            {
                if (cleanDamage->hitOutCome == MELEE_HIT_CRIT)
                    weaponSpeedHitFactor = uint32(GetAttackTime(cleanDamage->attackType)/1000.0f * 3.5f);
                else
                    weaponSpeedHitFactor = uint32(GetAttackTime(cleanDamage->attackType)/1000.0f * 1.75f);

                ToPlayer()->RewardRage(damage, weaponSpeedHitFactor, true);

                break;
            }
            case RANGED_ATTACK:
                break;
        }
    }

    if (pVictim->GetTypeId() == TYPEID_PLAYER && GetTypeId() == TYPEID_PLAYER)
    {
        if (pVictim->ToPlayer()->InBattleGround())
        {
            Player* killer = ToPlayer();
            if (killer != pVictim->ToPlayer())
                if (BattleGround *bg = killer->GetBattleGround())
                    bg->UpdatePlayerScore(killer, SCORE_DAMAGE_DONE, damage);
        }
    }
    else if (pVictim->GetTypeId() == TYPEID_PLAYER && GetTypeId() != TYPEID_PLAYER && GetTypeId() == TYPEID_UNIT)    //Pets need damage added to bg score
    {
        if (Creature *creature = ToCreature())
            if (creature->isPet() && creature->GetOwner()->GetTypeId() == TYPEID_PLAYER)
                if (Player* owner = creature->GetOwner()->ToPlayer())
                    if (owner->InBattleGround())
                        if (BattleGround *bg = owner->GetBattleGround())
                            bg->UpdatePlayerScore(owner, SCORE_DAMAGE_DONE, damage);

    }

    if (pVictim->GetTypeId() == TYPEID_UNIT && !pVictim->ToCreature()->isPet())
    {
        if (!pVictim->ToCreature()->hasLootRecipient())
            pVictim->ToCreature()->SetLootRecipient(this);

        if (GetCharmerOrOwnerPlayerOrPlayerItself())
            pVictim->ToCreature()->LowerPlayerDamageReq(health < damage ?  health : damage);
    }

    if (health <= damage)
    {
        sLog->outDebug("DealDamage: victim just died");
        Kill(pVictim, durabilityLoss);

        //Hook for OnPVPKill Event
        if (pVictim->GetTypeId() == TYPEID_PLAYER && this->GetTypeId() == TYPEID_PLAYER)
        {
            Player* killer = ToPlayer();
            Player *killed = pVictim->ToPlayer();
            sScriptMgr->OnPVPKill(killer, killed);
        }
    }
    else                                                    // if (health <= damage)
    {
        sLog->outDebug("DealDamageAlive");

        pVictim->ModifyHealth(- (int32)damage);

        if (damagetype != DOT)
        {
            if (!getVictim())
                Attack(pVictim, (damagetype == DIRECT_DAMAGE));
        }

        if (damagetype == DIRECT_DAMAGE || damagetype == SPELL_DIRECT_DAMAGE)
            pVictim->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_DIRECT_DAMAGE, spellProto ? spellProto->Id : 0);

        if (pVictim->GetTypeId() != TYPEID_PLAYER)
        {
            if (spellProto && IsDamageToThreatSpell(spellProto))
                pVictim->AddThreat(this, damage*2, damageSchoolMask, spellProto);
            else
                pVictim->AddThreat(this, damage, damageSchoolMask, spellProto);
        }
        else                                                // victim is a player
        {
            // Rage from damage received
            if (this != pVictim && pVictim->getPowerType() == POWER_RAGE)
            {
                uint32 rage_damage = damage + (cleanDamage ? cleanDamage->damage : 0);
                pVictim->ToPlayer()->RewardRage(damage, 0, false);
            }

            // random durability for items (HIT TAKEN)
            if (roll_chance_f(sWorld->getRate(RATE_DURABILITY_LOSS_DAMAGE)))
            {
                EquipmentSlots slot = EquipmentSlots(urand(0, EQUIPMENT_SLOT_END-1));
                pVictim->ToPlayer()->DurabilityPointLossForEquipSlot(slot);
            }
        }

        if (GetTypeId() == TYPEID_PLAYER)
        {
            // random durability for items (HIT DONE)
            if (roll_chance_f(sWorld->getRate(RATE_DURABILITY_LOSS_DAMAGE)))
            {
                EquipmentSlots slot = EquipmentSlots(urand(0, EQUIPMENT_SLOT_END-1));
                ToPlayer()->DurabilityPointLossForEquipSlot(slot);
            }
        }

        if (damagetype != NODAMAGE && damage)// && pVictim->GetTypeId() == TYPEID_PLAYER)
        {
            if (pVictim != this && pVictim->GetTypeId() == TYPEID_PLAYER) // does not support creature push_back
            {
                if (damagetype != DOT)
                {
                    if (Spell* spell = pVictim->m_currentSpells[CURRENT_GENERIC_SPELL])
                    {
                        if (spell->getState() == SPELL_STATE_PREPARING)
                        {
                            uint32 interruptFlags = spell->m_spellInfo->InterruptFlags;
                            if (interruptFlags & SPELL_INTERRUPT_FLAG_DAMAGE)
                                pVictim->InterruptNonMeleeSpells(false);
                            else if (interruptFlags & SPELL_INTERRUPT_FLAG_PUSH_BACK)
                                spell->Delayed();
                        }
                    }
                }

                if (Spell* spell = pVictim->m_currentSpells[CURRENT_CHANNELED_SPELL])
                {
                    if (spell->getState() == SPELL_STATE_CASTING)
                    {
                        uint32 channelInterruptFlags = spell->m_spellInfo->ChannelInterruptFlags;
                        if (((channelInterruptFlags & CHANNEL_FLAG_DELAY) != 0) && (damagetype != DOT))
                            spell->DelayedChannel();
                    }
                }
            }
        }

        // last damage from duel opponent
        if (duel_hasEnded)
        {
            ASSERT(pVictim->GetTypeId() == TYPEID_PLAYER);
            Player *he = pVictim->ToPlayer();

            ASSERT(he->duel);

            he->SetHealth(1);

            he->duel->opponent->CombatStopWithPets(true);
            he->CombatStopWithPets(true);

            he->CastSpell(he, 7267, true);                  // beg
            he->DuelComplete(DUEL_WON);
            if (he->GetAreaId() == 2037)
                he->RemoveArenaSpellCooldowns();
        }
    }

    sLog->outDebug("DealDamageEnd returned %d damage", damage);

    return damage;
}

void Unit::CastStop(uint32 except_spellid)
{
    for (uint32 i = CURRENT_FIRST_NON_MELEE_SPELL; i < CURRENT_MAX_SPELL; i++)
        if (m_currentSpells[i] && m_currentSpells[i]->m_spellInfo->Id != except_spellid)
            InterruptSpell(CurrentSpellTypes(i), false);
}

void Unit::CastSpell(Unit* Victim, uint32 spellId, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId);

    if (!spellInfo)
    {
        sLog->outError("CastSpell: unknown spell id %i by caster: %s %u)", spellId, (GetTypeId() == TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"), (GetTypeId() == TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
        return;
    }

    CastSpell(Victim, spellInfo, triggered, castItem, triggeredByAura, originalCaster);
}

void Unit::CastSpell(Unit* Victim, SpellEntry const *spellInfo, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    if (!spellInfo)
    {
        sLog->outError("CastSpell: unknown spell by caster: %s %u)", (GetTypeId() == TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"), (GetTypeId() == TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
        return;
    }

    SpellCastTargets targets;
    uint32 targetMask = spellInfo->Targets;
    //if (targetMask & (TARGET_FLAG_UNIT|TARGET_FLAG_UNK2))
    for (int i = 0; i < 3; ++i)
    {
        if (sSpellMgr->SpellTargetType[spellInfo->EffectImplicitTargetA[i]] == TARGET_TYPE_UNIT_TARGET)
        {
            //SpellRangeEntry const* srange = sSpellRangeStore.LookupEntry(spellInfo->rangeIndex);
            if (spellInfo->Id == 3137 || spellInfo->Id == 10872) // Dirty hack for Abolish poison and Disease Effect. Which other spells should be here?
            {
                Victim = this;
                break;
            }
            else if (!Victim)
            {
                sLog->outError("CastSpell: spell id %i by caster: %s %u) does not have unit target", spellInfo->Id, (GetTypeId() == TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"), (GetTypeId() == TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
                return;
            }
            else
                break;
        }
    }
    targets.setUnitTarget(Victim);

    if (targetMask & (TARGET_FLAG_SOURCE_LOCATION|TARGET_FLAG_DEST_LOCATION))
    {
        if (!Victim)
        {
            sLog->outError("CastSpell: spell id %i by caster: %s %u) does not have destination", spellInfo->Id, (GetTypeId() == TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"), (GetTypeId() == TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
            return;
        }
        targets.setDst(Victim);
    }

    if (castItem)
        sLog->outDebug("WORLD: cast Item spellId - %i", spellInfo->Id);

    if (!originalCaster && triggeredByAura)
        originalCaster = triggeredByAura->GetCasterGUID();

    Spell *spell = new Spell(this, spellInfo, triggered, originalCaster);

    spell->m_CastItem = castItem;

    spell->prepare(&targets, triggeredByAura);
}

void Unit::CastCustomSpell(Unit* target, uint32 spellId, int32 const* bp0, int32 const* bp1, int32 const* bp2, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    CustomSpellValues values;
    if (bp0)
        values.AddSpellMod(SPELLVALUE_BASE_POINT0, *bp0);
    if (bp1)
        values.AddSpellMod(SPELLVALUE_BASE_POINT1, *bp1);
    if (bp2)
        values.AddSpellMod(SPELLVALUE_BASE_POINT2, *bp2);
    CastCustomSpell(spellId, values, target, triggered, castItem, triggeredByAura, originalCaster);
}

void Unit::CastCustomSpell(uint32 spellId, SpellValueMod mod, uint32 value, Unit* target, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    CustomSpellValues values;
    values.AddSpellMod(mod, value);
    CastCustomSpell(spellId, values, target, triggered, castItem, triggeredByAura, originalCaster);
}

void Unit::CastCustomSpell(uint32 spellId, CustomSpellValues const &value, Unit* Victim, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId);
    if (!spellInfo)
    {
        sLog->outError("CastSpell: unknown spell id %i by caster: %s %u)", spellId, (GetTypeId() == TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"), (GetTypeId() == TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
        return;
    }

    SpellCastTargets targets;
    uint32 targetMask = spellInfo->Targets;

    //check unit target
    for (int i = 0; i < 3; ++i)
    {
        if (sSpellMgr->SpellTargetType[spellInfo->EffectImplicitTargetA[i]] == TARGET_TYPE_UNIT_TARGET)
        {
            if (!Victim)
            {
                sLog->outError("CastSpell: spell id %i by caster: %s %u) does not have unit target", spellInfo->Id, (GetTypeId() == TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"), (GetTypeId() == TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
                return;
            }
            else
                break;
        }
    }
    targets.setUnitTarget(Victim);

    //check destination
    if (targetMask & (TARGET_FLAG_SOURCE_LOCATION|TARGET_FLAG_DEST_LOCATION))
    {
        if (!Victim)
        {
            sLog->outError("CastSpell: spell id %i by caster: %s %u) does not have destination", spellInfo->Id, (GetTypeId() == TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"), (GetTypeId() == TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
            return;
        }
        targets.setDst(Victim);
    }

    if (!originalCaster && triggeredByAura)
        originalCaster = triggeredByAura->GetCasterGUID();

    Spell *spell = new Spell(this, spellInfo, triggered, originalCaster);

    if (castItem)
    {
        sLog->outDebug("WORLD: cast Item spellId - %i", spellInfo->Id);
        spell->m_CastItem = castItem;
    }

    for (CustomSpellValues::const_iterator itr = value.begin(); itr != value.end(); ++itr)
        spell->SetSpellValue(itr->first, itr->second);

    spell->prepare(&targets, triggeredByAura);
}

// used for scripting
void Unit::CastSpell(float x, float y, float z, uint32 spellId, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId);

    if (!spellInfo)
    {
        sLog->outError("CastSpell(x, y, z): unknown spell id %i by caster: %s %u)", spellId, (GetTypeId() == TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"), (GetTypeId() == TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
        return;
    }

    if (castItem)
        sLog->outDebug("WORLD: cast Item spellId - %i", spellInfo->Id);

    if (!originalCaster && triggeredByAura)
        originalCaster = triggeredByAura->GetCasterGUID();

    Spell *spell = new Spell(this, spellInfo, triggered, originalCaster);

    SpellCastTargets targets;
    targets.setDst(x, y, z, GetOrientation());
    spell->m_CastItem = castItem;
    spell->prepare(&targets, triggeredByAura);
}

// used for scripting
void Unit::CastSpell(GameObject *go, uint32 spellId, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    if (!go)
        return;

    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId);

    if (!spellInfo)
    {
        sLog->outError("CastSpell(x, y, z): unknown spell id %i by caster: %s %u)", spellId, (GetTypeId() == TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"), (GetTypeId() == TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
        return;
    }

    if (!(spellInfo->Targets & (TARGET_FLAG_OBJECT | TARGET_FLAG_OBJECT_UNK)))
    {
        sLog->outError("CastSpell: spell id %i by caster: %s %u) is not gameobject spell", spellId, (GetTypeId() == TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"), (GetTypeId() == TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
        return;
    }

    if (castItem)
        sLog->outDebug("WORLD: cast Item spellId - %i", spellInfo->Id);

    if (!originalCaster && triggeredByAura)
        originalCaster = triggeredByAura->GetCasterGUID();

    Spell *spell = new Spell(this, spellInfo, triggered, originalCaster);

    SpellCastTargets targets;
    targets.setGOTarget(go);
    spell->m_CastItem = castItem;
    spell->prepare(&targets, triggeredByAura);
}

// Obsolete func need remove, here only for comotability vs another patches
uint32 Unit::SpellNonMeleeDamageLog(Unit *pVictim, uint32 spellID, uint32 damage)
{
    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellID);
    SpellNonMeleeDamage damageInfo(this, pVictim, spellInfo->Id, spellInfo->SchoolMask);
    CalculateSpellDamage(&damageInfo, damage, spellInfo);
    SendSpellNonMeleeDamageLog(&damageInfo);
    DealSpellDamage(&damageInfo, true);
    return damageInfo.damage;
}

void Unit::CalculateSpellDamage(SpellNonMeleeDamage *damageInfo, int32 damage, SpellEntry const *spellInfo, WeaponAttackType attackType)
{
    if (damage < 0)
        return;

    Unit *pVictim = damageInfo->target;
    if (!pVictim || !pVictim->isAlive())
        return;

    SpellSchoolMask damageSchoolMask = SpellSchoolMask(damageInfo->schoolMask);
    uint32 crTypeMask = pVictim->GetCreatureTypeMask();
    // Check spell crit chance
    bool crit = isSpellCrit(pVictim, spellInfo, damageSchoolMask, attackType);
    bool blocked = false;
    // Per-school calc
    switch (spellInfo->DmgClass)
    {
        // Melee and Ranged Spells
        case SPELL_DAMAGE_CLASS_RANGED:
        case SPELL_DAMAGE_CLASS_MELEE:
        {
            // Calculate damage bonus
            damage = MeleeDamageBonusDone(pVictim, damage, attackType, spellInfo, SPELL_DIRECT_DAMAGE);
            damage = pVictim->MeleeDamageBonusTaken(this, damage, attackType, spellInfo, SPELL_DIRECT_DAMAGE);

            // Physical Damage
            if (damageSchoolMask & SPELL_SCHOOL_MASK_NORMAL)
            {
                // Get blocked status
                blocked = isSpellBlocked(pVictim, spellInfo, attackType);
            }

            if (crit)
            {
                damageInfo->HitInfo |= SPELL_HIT_TYPE_CRIT;
                damage = SpellCriticalDamageBonus(spellInfo, damage, pVictim);

                // Resilience - reduce crit damage
                if (pVictim->GetTypeId() == TYPEID_PLAYER)
                    damage -= pVictim->ToPlayer()->GetMeleeCritDamageReduction(damage);
            }
            // Spell weapon based damage CAN BE crit & blocked at same time
            if (blocked)
            {
                damageInfo->blocked = uint32(pVictim->GetShieldBlockValue());
                if (damage < damageInfo->blocked)
                    damageInfo->blocked = damage;
                damage -= damageInfo->blocked;
            }
        }
        break;
        // Magical Attacks
        case SPELL_DAMAGE_CLASS_NONE:
        case SPELL_DAMAGE_CLASS_MAGIC:
        {
            // Calculate damage bonus
            damage = SpellDamageBonusDone(pVictim, spellInfo, damage, SPELL_DIRECT_DAMAGE);
            damage = pVictim->SpellDamageBonusTaken(this, spellInfo, damage, SPELL_DIRECT_DAMAGE);

            // If crit add critical bonus
            if (crit)
            {
                damageInfo->HitInfo |= SPELL_HIT_TYPE_CRIT;
                damage = SpellCriticalDamageBonus(spellInfo, damage, pVictim);

                // Resilience - reduce crit damage
                if (pVictim->GetTypeId() == TYPEID_PLAYER)
                    damage -= pVictim->ToPlayer()->GetSpellCritDamageReduction(damage);
            }
        }
        break;
    }

    // damage before absorb/resist calculation
    damageInfo->cleanDamage = damage;

    if (damageSchoolMask & SPELL_SCHOOL_MASK_NORMAL  && (sSpellMgr->GetSpellCustomAttr(spellInfo->Id) & SPELL_ATTR_CU_IGNORE_ARMOR) == 0)
        damage = CalcArmorReducedDamage(pVictim, damage);

    // Calculate absorb resist
    if (damage > 0)
    {
        CalcAbsorbResist(pVictim, damageSchoolMask, SPELL_DIRECT_DAMAGE, damage, &damageInfo->absorb, &damageInfo->resist);
        damage-= damageInfo->absorb + damageInfo->resist;
    }
    else
        damage = 0;

    damageInfo->damage = damage;
}

void Unit::DealSpellDamage(SpellNonMeleeDamage *damageInfo, bool durabilityLoss)
{
    if (!damageInfo)
        return;

    Unit *pVictim = damageInfo->target;

    if (!this || !pVictim)
        return;

    if (!pVictim->isAlive() || pVictim->isInFlight() || pVictim->GetTypeId() == TYPEID_UNIT && pVictim->ToCreature()->IsInEvadeMode())
        return;

    SpellEntry const *spellProto = sSpellStore.LookupEntry(damageInfo->SpellID);
    if (spellProto == NULL)
    {
        sLog->outDebug("Unit::DealSpellDamage has invalid damageInfo->SpellID: %u", damageInfo->SpellID);
        return;
    }

    //You don't lose health from damage taken from another player while in a sanctuary
    //You still see it in the combat log though
    if (pVictim != this && GetTypeId() == TYPEID_PLAYER && pVictim->GetTypeId() == TYPEID_PLAYER)
    {
        const AreaTableEntry *area = GetAreaEntryByAreaID(pVictim->GetAreaId());
        if (area && area->flags & 0x800)                     //sanctuary
            return;
    }

    // update at damage Judgement aura duration that applied by attacker at victim
    if (damageInfo->damage && spellProto->Id == 35395)
    {
        AuraMap& vAuras = pVictim->GetAuras();
        for (AuraMap::iterator itr = vAuras.begin(); itr != vAuras.end(); ++itr)
        {
            SpellEntry const *spellInfo = (*itr).second->GetSpellProto();
            if (spellInfo->SpellFamilyName == SPELLFAMILY_PALADIN && spellInfo->AttributesEx3 & 0x40000)
            {
                (*itr).second->SetAuraDuration((*itr).second->GetAuraMaxDuration());
                (*itr).second->UpdateAuraDuration();
            }
        }
    }
    // Call default DealDamage
    CleanDamage cleanDamage(damageInfo->cleanDamage, BASE_ATTACK, MELEE_HIT_NORMAL);
    DealDamage(pVictim, damageInfo->damage, &cleanDamage, SPELL_DIRECT_DAMAGE, SpellSchoolMask(damageInfo->schoolMask), spellProto, durabilityLoss);
}

//TODO for melee need create structure as in
void Unit::CalculateMeleeDamage(Unit *pVictim, uint32 damage, CalcDamageInfo *damageInfo, WeaponAttackType attackType)
{
    damageInfo->attacker         = this;
    damageInfo->target           = pVictim;
    damageInfo->damageSchoolMask = GetMeleeDamageSchoolMask();
    damageInfo->attackType       = attackType;
    damageInfo->damage           = 0;
    damageInfo->cleanDamage      = 0;
    damageInfo->absorb           = 0;
    damageInfo->resist           = 0;
    damageInfo->blocked_amount   = 0;

    damageInfo->TargetState      = 0;
    damageInfo->HitInfo          = 0;
    damageInfo->procAttacker     = PROC_FLAG_NONE;
    damageInfo->procVictim       = PROC_FLAG_NONE;
    damageInfo->procEx           = PROC_EX_NONE;
    damageInfo->hitOutCome       = MELEE_HIT_EVADE;

    if (!this || !pVictim)
        return;
    if (!this->isAlive() || !pVictim->isAlive())
        return;

    // Select HitInfo/procAttacker/procVictim flag based on attack type
    switch (attackType)
    {
        case BASE_ATTACK:
            damageInfo->procAttacker = PROC_FLAG_SUCCESSFUL_MELEE_HIT;
            damageInfo->procVictim   = PROC_FLAG_TAKEN_MELEE_HIT;
            damageInfo->HitInfo      = HITINFO_NORMALSWING2;
            break;
        case OFF_ATTACK:
            damageInfo->procAttacker = PROC_FLAG_SUCCESSFUL_MELEE_HIT | PROC_FLAG_SUCCESSFUL_OFFHAND_HIT;
            damageInfo->procVictim   = PROC_FLAG_TAKEN_MELEE_HIT;//|PROC_FLAG_TAKEN_OFFHAND_HIT // not used
            damageInfo->HitInfo = HITINFO_LEFTSWING;
            break;
        case RANGED_ATTACK:
            damageInfo->procAttacker = PROC_FLAG_SUCCESSFUL_RANGED_HIT;
            damageInfo->procVictim   = PROC_FLAG_TAKEN_RANGED_HIT;
            damageInfo->HitInfo = 0x08;// test
            break;
        default:
            break;
    }

    // Physical Immune check
    if (damageInfo->target->IsImmunedToDamage(SpellSchoolMask(damageInfo->damageSchoolMask), true))
    {
       damageInfo->HitInfo       |= HITINFO_NORMALSWING;
       damageInfo->TargetState    = VICTIMSTATE_IS_IMMUNE;

       damageInfo->procEx        |= PROC_EX_IMMUNE;
       damageInfo->damage         = 0;
       damageInfo->cleanDamage    = 0;
       return;
    }
    damage += CalculateDamage(damageInfo->attackType, false);
    // Add melee damage bonus
    damage = MeleeDamageBonusDone(damageInfo->target, damage, damageInfo->attackType);
    damage = damageInfo->target->MeleeDamageBonusTaken(this, damage, damageInfo->attackType);

    // Calculate armor reduction
    damageInfo->damage = (damageInfo->damageSchoolMask & SPELL_SCHOOL_MASK_NORMAL) ? CalcArmorReducedDamage(damageInfo->target, damage) : damage;
    damageInfo->cleanDamage += damage - damageInfo->damage;

    damageInfo->hitOutCome = RollMeleeOutcomeAgainst(damageInfo->target, damageInfo->attackType);

    // Disable parry or dodge for ranged attack
    if (damageInfo->attackType == RANGED_ATTACK)
    {
        if (damageInfo->hitOutCome == MELEE_HIT_PARRY) damageInfo->hitOutCome = MELEE_HIT_NORMAL;
        if (damageInfo->hitOutCome == MELEE_HIT_DODGE) damageInfo->hitOutCome = MELEE_HIT_MISS;
    }

    switch (damageInfo->hitOutCome)
    {
        case MELEE_HIT_EVADE:
        {
            damageInfo->HitInfo    |= HITINFO_MISS|HITINFO_SWINGNOHITSOUND;
            damageInfo->TargetState = VICTIMSTATE_EVADES;

            damageInfo->procEx|=PROC_EX_EVADE;
            damageInfo->damage = 0;
            damageInfo->cleanDamage = 0;
            return;
        }
        case MELEE_HIT_MISS:
        {
            damageInfo->HitInfo    |= HITINFO_MISS;
            damageInfo->TargetState = VICTIMSTATE_NORMAL;

            damageInfo->procEx |= PROC_EX_MISS;
            damageInfo->damage  = 0;
            damageInfo->cleanDamage = 0;
            break;
        }
        case MELEE_HIT_NORMAL:
            damageInfo->TargetState = VICTIMSTATE_NORMAL;
            damageInfo->procEx|=PROC_EX_NORMAL_HIT;
            break;
        case MELEE_HIT_CRIT:
        {
            damageInfo->HitInfo     |= HITINFO_CRITICALHIT;
            damageInfo->TargetState  = VICTIMSTATE_NORMAL;

            damageInfo->procEx      |= PROC_EX_CRITICAL_HIT;
            // Crit bonus calc
            damageInfo->damage += damageInfo->damage;
            int32 mod = 0;
            // Apply SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_DAMAGE or SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_DAMAGE
            if (damageInfo->attackType == RANGED_ATTACK)
                mod += damageInfo->target->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_DAMAGE);
            else
                mod += damageInfo->target->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_DAMAGE);

            mod += GetTotalAuraModifier(SPELL_AURA_MOD_CRIT_DAMAGE_BONUS);
            uint32 crTypeMask = damageInfo->target->GetCreatureTypeMask();

            // Increase crit damage from SPELL_AURA_MOD_CRIT_PERCENT_VERSUS
            mod += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_CRIT_PERCENT_VERSUS, crTypeMask);
            if (mod != 0)
                damageInfo->damage = int32((damageInfo->damage) * float((100.0f + mod)/100.0f));

            // Resilience - reduce crit damage
            if (pVictim->GetTypeId() == TYPEID_PLAYER)
            {
                uint32 resilienceReduction = pVictim->ToPlayer()->GetMeleeCritDamageReduction(damageInfo->damage);
                damageInfo->damage      -= resilienceReduction;
                damageInfo->cleanDamage += resilienceReduction;
            }
            break;
        }
        case MELEE_HIT_PARRY:
            damageInfo->TargetState  = VICTIMSTATE_PARRY;
            damageInfo->procEx      |= PROC_EX_PARRY;
            damageInfo->cleanDamage += damageInfo->damage;
            damageInfo->damage = 0;
            break;

        case MELEE_HIT_DODGE:
            damageInfo->TargetState  = VICTIMSTATE_DODGE;
            damageInfo->procEx      |= PROC_EX_DODGE;
            damageInfo->cleanDamage += damageInfo->damage;
            damageInfo->damage = 0;
            break;
        case MELEE_HIT_BLOCK:
        {
            damageInfo->TargetState = VICTIMSTATE_NORMAL;
            damageInfo->procEx     |= PROC_EX_BLOCK;
            damageInfo->blocked_amount = damageInfo->target->GetShieldBlockValue();
            if (damageInfo->blocked_amount >= damageInfo->damage)
            {
                damageInfo->TargetState = VICTIMSTATE_BLOCKS;
                damageInfo->blocked_amount = damageInfo->damage;
            }
            damageInfo->damage      -= damageInfo->blocked_amount;
            damageInfo->cleanDamage += damageInfo->blocked_amount;
            break;
        }
        case MELEE_HIT_GLANCING:
        {
            damageInfo->HitInfo     |= HITINFO_GLANCING;
            damageInfo->TargetState  = VICTIMSTATE_NORMAL;
            damageInfo->procEx      |= PROC_EX_NORMAL_HIT;
            int32 leveldif = int32(pVictim->getLevel()) - int32(getLevel());
            if (leveldif > 3) leveldif = 3;
            float reducePercent = 1 - leveldif * 0.1f;
            damageInfo->cleanDamage += damageInfo->damage-uint32(reducePercent * damageInfo->damage);
            damageInfo->damage   = uint32(reducePercent * damageInfo->damage);
            break;
        }
        case MELEE_HIT_CRUSHING:
        {
            damageInfo->HitInfo     |= HITINFO_CRUSHING;
            damageInfo->TargetState  = VICTIMSTATE_NORMAL;
            damageInfo->procEx      |= PROC_EX_NORMAL_HIT;
            // 150% normal damage
            damageInfo->damage += (damageInfo->damage / 2);
            break;
        }
        default:
            break;
    }

    // Calculate absorb resist
    if (int32(damageInfo->damage) > 0)
    {
        damageInfo->procVictim |= PROC_FLAG_TAKEN_ANY_DAMAGE;
        // Calculate absorb & resists
        CalcAbsorbResist(damageInfo->target, SpellSchoolMask(damageInfo->damageSchoolMask), DIRECT_DAMAGE, damageInfo->damage, &damageInfo->absorb, &damageInfo->resist);
        damageInfo->damage -= damageInfo->absorb + damageInfo->resist;

        if (damageInfo->absorb)
        {
            damageInfo->HitInfo |= HITINFO_ABSORB;
            damageInfo->procEx  |= PROC_EX_ABSORB;
        }
        if (damageInfo->resist)
            damageInfo->HitInfo |= HITINFO_RESIST;
    }
    else // Impossible get negative result but....
        damageInfo->damage = 0;
}

void Unit::DealMeleeDamage(CalcDamageInfo *damageInfo, bool durabilityLoss)
{
    if (damageInfo == 0)
        return;
    Unit *pVictim = damageInfo->target;

    if (!this || !pVictim)
        return;

    if (!pVictim->isAlive() || pVictim->isInFlight() || pVictim->GetTypeId() == TYPEID_UNIT && pVictim->ToCreature()->IsInEvadeMode())
        return;

    //You don't lose health from damage taken from another player while in a sanctuary
    //You still see it in the combat log though
    if (pVictim != this && GetTypeId() == TYPEID_PLAYER && pVictim->GetTypeId() == TYPEID_PLAYER)
    {
        const AreaTableEntry *area = GetAreaEntryByAreaID(pVictim->GetAreaId());
        if (area && area->flags & 0x800)                     //sanctuary
            return;
    }

    // Hmmmm dont like this emotes client must by self do all animations
    if (damageInfo->HitInfo&HITINFO_CRITICALHIT)
        pVictim->HandleEmoteCommand(EMOTE_ONESHOT_WOUNDCRITICAL);
    if (damageInfo->blocked_amount && damageInfo->TargetState != VICTIMSTATE_BLOCKS)
        pVictim->HandleEmoteCommand(EMOTE_ONESHOT_PARRYSHIELD);

    if (damageInfo->TargetState == VICTIMSTATE_PARRY)
    {
        // Get attack timers
        float offtime  = float(pVictim->getAttackTimer(OFF_ATTACK));
        float basetime = float(pVictim->getAttackTimer(BASE_ATTACK));
        // Reduce attack time
        if (pVictim->haveOffhandWeapon() && offtime < basetime)
        {
            float percent20 = pVictim->GetAttackTime(OFF_ATTACK) * 0.20f;
            float percent60 = 3.0f * percent20;
            if (offtime > percent20 && offtime <= percent60)
                pVictim->setAttackTimer(OFF_ATTACK, uint32(percent20));
            else if (offtime > percent60)
            {
                offtime -= 2.0f * percent20;
                pVictim->setAttackTimer(OFF_ATTACK, uint32(offtime));
            }
        }
        else
        {
            float percent20 = pVictim->GetAttackTime(BASE_ATTACK) * 0.20;
            float percent60 = 3.0f * percent20;
            if (basetime > percent20 && basetime <= percent60)
                pVictim->setAttackTimer(BASE_ATTACK, uint32(percent20));
            else if (basetime > percent60)
            {
                basetime -= 2.0f * percent20;
                pVictim->setAttackTimer(BASE_ATTACK, uint32(basetime));
            }
        }
    }

    // Call default DealDamage
    CleanDamage cleanDamage(damageInfo->cleanDamage, damageInfo->attackType, damageInfo->hitOutCome);
    DealDamage(pVictim, damageInfo->damage, &cleanDamage, DIRECT_DAMAGE, SpellSchoolMask(damageInfo->damageSchoolMask), NULL, durabilityLoss);

    // If this is a creature and it attacks from behind it has a probability to daze it's victim
    if ((damageInfo->hitOutCome == MELEE_HIT_CRIT || damageInfo->hitOutCome == MELEE_HIT_CRUSHING || damageInfo->hitOutCome == MELEE_HIT_NORMAL || damageInfo->hitOutCome == MELEE_HIT_GLANCING) &&
        GetTypeId() != TYPEID_PLAYER && !ToCreature()->IsControlledByPlayer() && !pVictim->HasInArc(M_PI, this)
        && (pVictim->GetTypeId() == TYPEID_PLAYER || !pVictim->ToCreature()->isWorldBoss()))
    {
        // -probability is between 0% and 40%
        // 20% base chance
        float Probability = 20.0f;

        //there is a newbie protection, at level 10 just 7% base chance; assuming linear function
        if (pVictim->getLevel() < 30)
            Probability = 0.65f*pVictim->getLevel()+0.5f;

        uint32 VictimDefense=pVictim->GetDefenseSkillValue();
        uint32 AttackerMeleeSkill=GetUnitMeleeSkill();

        Probability *= AttackerMeleeSkill/(float)VictimDefense;

        if (Probability > 40.0f)
            Probability = 40.0f;

        if (roll_chance_f(Probability))
            CastSpell(pVictim, 1604, true);
    }

    // update at damage Judgement aura duration that applied by attacker at victim
    if (damageInfo->damage)
    {
        AuraMap& vAuras = pVictim->GetAuras();
        for (AuraMap::iterator itr = vAuras.begin(); itr != vAuras.end(); ++itr)
        {
            SpellEntry const *spellInfo = (*itr).second->GetSpellProto();
            if (spellInfo->AttributesEx3 & 0x40000 && spellInfo->SpellFamilyName == SPELLFAMILY_PALADIN && ((*itr).second->GetCasterGUID() == GetGUID()))
            {
                (*itr).second->SetAuraDuration((*itr).second->GetAuraMaxDuration());
                (*itr).second->UpdateAuraDuration();
            }
        }
    }

    if (GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->CastItemCombatSpell(pVictim, damageInfo->attackType, damageInfo->procVictim, damageInfo->procEx);

    // Do effect if any damage done to target
    if (damageInfo->procVictim & PROC_FLAG_TAKEN_ANY_DAMAGE)
    {
        // victim's damage shield
        std::set<Aura*> alreadyDone;
        uint32 removedAuras = pVictim->m_removedAurasCount;
        AuraList const& vDamageShields = pVictim->GetAurasByType(SPELL_AURA_DAMAGE_SHIELD);
        for (AuraList::const_iterator i = vDamageShields.begin(), next = vDamageShields.begin(); i != vDamageShields.end(); i = next)
        {
           ++next;
           if (alreadyDone.find(*i) == alreadyDone.end())
           {
               alreadyDone.insert(*i);
               uint32 damage=(*i)->GetModifier()->m_amount;
               SpellEntry const *spellProto = sSpellStore.LookupEntry((*i)->GetId());
               if (!spellProto)
                   continue;
               //Calculate absorb resist ??? no data in opcode for this possibly unable to absorb or resist?
               //uint32 absorb;
               //uint32 resist;
               //CalcAbsorbResist(pVictim, SpellSchools(spellProto->School), SPELL_DIRECT_DAMAGE, damage, &absorb, &resist);
               //damage-=absorb + resist;

               WorldPacket data(SMSG_SPELLDAMAGESHIELD, (8+8+4+4+4));
               data << uint64(pVictim->GetGUID());
               data << uint64(GetGUID());
               data << uint32(spellProto->Id);
               data << uint32(damage);                  // Damage
               data << uint32(spellProto->SchoolMask);
               pVictim->SendMessageToSet(&data, true);

               pVictim->DealDamage(this, damage, 0, SPELL_DIRECT_DAMAGE, GetSpellSchoolMask(spellProto), spellProto, true);

               if (pVictim->m_removedAurasCount > removedAuras)
               {
                   removedAuras = pVictim->m_removedAurasCount;
                   next = vDamageShields.begin();
               }
           }
        }
    }
}

void Unit::HandleEmoteCommand(uint32 anim_id)
{
    WorldPacket data(SMSG_EMOTE, 4 + 8);
    data << uint32(anim_id);
    data << uint64(GetGUID());
    SendMessageToSet(&data, true);
}

uint32 Unit::CalcArmorReducedDamage(Unit* pVictim, const uint32 damage)
{
    uint32 newdamage = 0;
    float armor = (float)pVictim->GetArmor();

    // Ignore enemy armor by SPELL_AURA_MOD_TARGET_RESISTANCE aura
    armor += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_TARGET_RESISTANCE, SPELL_SCHOOL_MASK_NORMAL);

    if (armor < 0.0f)
        armor = 0.0f;

    float levelModifier = (float)getLevel();
    if (levelModifier > 59)
        levelModifier = levelModifier + (4.5f * (levelModifier - 59));

    float tmpvalue = 0.1f * armor / (8.5f * levelModifier + 40);
    tmpvalue = tmpvalue / (1.0f + tmpvalue);

    if (tmpvalue < 0.0f)
        tmpvalue = 0.0f;
    if (tmpvalue > 0.75f)
        tmpvalue = 0.75f;

    newdamage = uint32(damage - (damage * tmpvalue));

    return (newdamage > 1) ? newdamage : 1;
}

void Unit::CalcAbsorbResist(Unit *pVictim, SpellSchoolMask schoolMask, DamageEffectType damagetype, const uint32 damage, uint32 *absorb, uint32 *resist)
{
    if (!pVictim || !pVictim->isAlive() || !damage)
        return;

    // Magic damage, check for resists
    if (schoolMask & ~SPELL_SCHOOL_MASK_NORMAL)
    {
        // Get base victim resistance for school
        float victimResistance = (float)pVictim->GetResistance(GetFirstSchoolInMask(schoolMask));
        // Ignore resistance by self SPELL_AURA_MOD_TARGET_RESISTANCE aura
        victimResistance += (float)GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_TARGET_RESISTANCE, schoolMask);

        if (victimResistance < 0.0f || schoolMask & SPELL_SCHOOL_MASK_HOLY)
            victimResistance = 0.0f;

        victimResistance *= (float)(0.15f / getLevel());
        if (victimResistance < 0.0f)
            victimResistance = 0.0f;
        if (victimResistance > 0.75f)
            victimResistance = 0.75f;

        uint32 ran = urand(0, 100);
        uint32 faq[4] = {24, 6, 4, 6};
        uint8 m = 0;
        float Binom = 0.0f;
        for (uint8 i = 0; i < 4; i++)
        {
            Binom += 2400 *(powf(victimResistance, i) * powf((1-victimResistance), (4-i)))/faq[i];
            if (ran > Binom)
                ++m;
            else
                break;
        }
        if (damagetype == DOT && m == 4)
            *resist += uint32(damage - 1);
        else
            *resist += uint32(damage * m / 4);
        if (*resist > damage)
            *resist = damage;
    }
    else
        *resist = 0;

    int32 RemainingDamage = damage - *resist;

    // Need to remove expired auras after
    bool expiredExists = false;

    // absorb without mana cost
    int32 reflectDamage = 0;
    Aura* reflectAura = NULL;
    AuraList const& vSchoolAbsorb = pVictim->GetAurasByType(SPELL_AURA_SCHOOL_ABSORB);
    for (AuraList::const_iterator i = vSchoolAbsorb.begin(); i != vSchoolAbsorb.end() && RemainingDamage > 0; ++i)
    {
        int32 *p_absorbAmount = &(*i)->GetModifier()->m_amount;

        // should not happen....
        if (*p_absorbAmount <=0)
        {
            expiredExists = true;
            continue;
        }

        if (((*i)->GetModifier()->m_miscvalue & schoolMask) == 0)
            continue;

        // Cheat Death
        if ((*i)->GetSpellProto()->SpellFamilyName == SPELLFAMILY_ROGUE && (*i)->GetSpellProto()->SpellIconID == 2109)
        {
            if (pVictim->ToPlayer()->HasSpellCooldown(31231))
                continue;
            if (pVictim->GetHealth() <= RemainingDamage)
            {
                int32 chance = *p_absorbAmount;
                if (roll_chance_i(chance))
                {
                    pVictim->CastSpell(pVictim, 31231, true);
                    pVictim->ToPlayer()->AddSpellCooldown(31231, 0, time(NULL)+60);

                    // with health > 10% lost health until health == 10%, in other case no losses
                    uint32 health10 = pVictim->GetMaxHealth()/10;
                    RemainingDamage = pVictim->GetHealth() > health10 ? pVictim->GetHealth() - health10 : 0;
                }
            }
            continue;
        }

        int32 currentAbsorb;

        //Reflective Shield
        if ((pVictim != this) && (*i)->GetSpellProto()->SpellFamilyName == SPELLFAMILY_PRIEST && (*i)->GetSpellProto()->SpellFamilyFlags == 0x1)
        {
            if (Unit* caster = (*i)->GetCaster())
            {
                AuraList const& vOverRideCS = caster->GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
                for (AuraList::const_iterator k = vOverRideCS.begin(); k != vOverRideCS.end(); ++k)
                {
                    switch ((*k)->GetModifier()->m_miscvalue)
                    {
                        case 5065:                          // Rank 1
                        case 5064:                          // Rank 2
                        case 5063:                          // Rank 3
                        case 5062:                          // Rank 4
                        case 5061:                          // Rank 5
                        {
                            if (RemainingDamage >= *p_absorbAmount)
                                reflectDamage = *p_absorbAmount * (*k)->GetModifier()->m_amount/100;
                            else
                                reflectDamage = (*k)->GetModifier()->m_amount * RemainingDamage/100;
                            reflectAura = *i;
                        } break;
                        default: break;
                    }

                    if (reflectDamage)
                        break;
                }
            }
        }

        if (RemainingDamage >= *p_absorbAmount)
        {
            currentAbsorb = *p_absorbAmount;
            expiredExists = true;
        }
        else
        {
            currentAbsorb = RemainingDamage;
        }

        *p_absorbAmount -= currentAbsorb;
        RemainingDamage -= currentAbsorb;
    }
    // do not cast spells while looping auras; auras can get invalid otherwise
    if (reflectDamage)
        pVictim->CastCustomSpell(this, 33619, &reflectDamage, NULL, NULL, true, NULL, reflectAura);

    // Remove all expired absorb auras
    if (expiredExists)
    {
        for (AuraList::const_iterator i = vSchoolAbsorb.begin(); i != vSchoolAbsorb.end();)
        {
            Aura *aur = (*i);
            ++i;
            if (aur->GetModifier()->m_amount <= 0)
            {
                uint32 removedAuras = pVictim->m_removedAurasCount;
                pVictim->RemoveAurasDueToSpell(aur->GetId());
                if (removedAuras+1<pVictim->m_removedAurasCount)
                    i=vSchoolAbsorb.begin();
            }
        }
    }

    // absorb by mana cost
    AuraList const& vManaShield = pVictim->GetAurasByType(SPELL_AURA_MANA_SHIELD);
    for (AuraList::const_iterator i = vManaShield.begin(), next; i != vManaShield.end() && RemainingDamage > 0; i = next)
    {
        next = i; ++next;
        int32 *p_absorbAmount = &(*i)->GetModifier()->m_amount;

        // check damage school mask
        if (((*i)->GetModifier()->m_miscvalue & schoolMask) == 0)
            continue;

        int32 currentAbsorb;
        if (RemainingDamage >= *p_absorbAmount)
            currentAbsorb = *p_absorbAmount;
        else
            currentAbsorb = RemainingDamage;

        float manaMultiplier = (*i)->GetSpellProto()->EffectMultipleValue[(*i)->GetEffIndex()];
        if (Player *modOwner = GetSpellModOwner())
            modOwner->ApplySpellMod((*i)->GetId(), SPELLMOD_MULTIPLE_VALUE, manaMultiplier);

        if (manaMultiplier)
        {
            int32 maxAbsorb = int32(pVictim->GetPower(POWER_MANA) / manaMultiplier);
            if (currentAbsorb > maxAbsorb)
                currentAbsorb = maxAbsorb;
        }

        *p_absorbAmount -= currentAbsorb;
        if (*p_absorbAmount <= 0)
        {
            pVictim->RemoveAurasDueToSpell((*i)->GetId());
            next = vManaShield.begin();
        }

        int32 manaReduction = int32(currentAbsorb * manaMultiplier);
        pVictim->ApplyPowerMod(POWER_MANA, manaReduction, false);

        RemainingDamage -= currentAbsorb;
    }

    // only split damage if not damaging yourself
    if (pVictim != this)
    {
        AuraList const& vSplitDamageFlat = pVictim->GetAurasByType(SPELL_AURA_SPLIT_DAMAGE_FLAT);
        for (AuraList::const_iterator i = vSplitDamageFlat.begin(), next; i != vSplitDamageFlat.end() && RemainingDamage >= 0; i = next)
        {
            next = i; ++next;

            // check damage school mask
            if (((*i)->GetModifier()->m_miscvalue & schoolMask) == 0)
                continue;

            // Damage can be splitted only if aura has an alive caster
            Unit *caster = (*i)->GetCaster();
            if (!caster || caster == pVictim || !caster->IsInWorld() || !caster->isAlive())
                continue;

            int32 currentAbsorb;
            if (RemainingDamage >= (*i)->GetModifier()->m_amount)
                currentAbsorb = (*i)->GetModifier()->m_amount;
            else
                currentAbsorb = RemainingDamage;

            RemainingDamage -= currentAbsorb;

            if (caster->IsImmunedToDamage(schoolMask, false))
            {
                pVictim->SendSpellMiss(caster, (*i)->GetSpellProto()->Id, SPELL_MISS_IMMUNE);
                return;
            }

            SendSpellNonMeleeDamageLog(caster, (*i)->GetSpellProto()->Id, currentAbsorb, schoolMask, 0, 0, false, 0, false);

            CleanDamage cleanDamage = CleanDamage(currentAbsorb, BASE_ATTACK, MELEE_HIT_NORMAL);
            DealDamage(caster, currentAbsorb, &cleanDamage, DOT, schoolMask, (*i)->GetSpellProto(), false);
        }

        AuraList const& vSplitDamagePct = pVictim->GetAurasByType(SPELL_AURA_SPLIT_DAMAGE_PCT);
        for (AuraList::const_iterator i = vSplitDamagePct.begin(), next; i != vSplitDamagePct.end() && RemainingDamage >= 0; i = next)
        {
            next = i; ++next;
            int32 *p_absorbAmount = &(*i)->GetModifier()->m_amount;

            // check damage school mask
            if (((*i)->GetModifier()->m_miscvalue & schoolMask) == 0)
                continue;

            // Damage can be splitted only if aura has an alive caster
            Unit *caster = (*i)->GetCaster();
            if (!caster || caster == pVictim || !caster->IsInWorld() || !caster->isAlive())
                continue;

            int32 splitted = int32(RemainingDamage * (*i)->GetModifier()->m_amount / 100.0f);

            RemainingDamage -= splitted;

            if (caster->IsImmunedToDamage(schoolMask, false))
            {
                pVictim->SendSpellMiss(caster, (*i)->GetSpellProto()->Id, SPELL_MISS_IMMUNE);
                return;
            }

            SendSpellNonMeleeDamageLog(caster, (*i)->GetSpellProto()->Id, splitted, schoolMask, 0, 0, false, 0, false);

            CleanDamage cleanDamage = CleanDamage(splitted, BASE_ATTACK, MELEE_HIT_NORMAL);
            DealDamage(caster, splitted, &cleanDamage, DOT, schoolMask, (*i)->GetSpellProto(), false);
        }
    }

    *absorb = damage - RemainingDamage - *resist;
}

bool Unit::CalcBinaryResist(Unit *pVictim, SpellSchoolMask schoolMask)
{
    if (!pVictim || !pVictim->isAlive())
        return false;

    // Magic damage, check for resists
    if (schoolMask &~ SPELL_SCHOOL_MASK_NORMAL)
    {
        // Get base victim resistance for school
        float effectiveResistance = (float)pVictim->GetResistance(GetFirstSchoolInMask(schoolMask));
        // Ignore resistance by self SPELL_AURA_MOD_TARGET_RESISTANCE aura
        if (GetTypeId() == TYPEID_PLAYER)
            effectiveResistance += (float)GetInt32Value(PLAYER_FIELD_MOD_TARGET_RESISTANCE);
        else
            effectiveResistance += (float)GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_TARGET_RESISTANCE, schoolMask);

        effectiveResistance *= (float)(0.15f / getLevel());

        if (effectiveResistance < 0.0f)
            effectiveResistance = 0.0f;
        if (effectiveResistance > 0.75f)
            effectiveResistance = 0.75f;

        int32 ran = irand(0, 100);
        return ran < effectiveResistance * 100;
    }

    return false;
}

void Unit::AttackerStateUpdate(Unit *pVictim, WeaponAttackType attType, bool extra)
{
    if (!extra && hasUnitState(UNIT_STAT_CANNOT_AUTOATTACK) || HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED))
        return;

    if (!pVictim->isAlive())
        return;

    if ((attType == BASE_ATTACK || attType == OFF_ATTACK) && !IsWithinLOSInMap(pVictim))
        return;

    CombatStart(pVictim);
    RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_MELEE_ATTACK);

    uint32 hitInfo;
    if (attType == BASE_ATTACK)
        hitInfo = HITINFO_NORMALSWING2;
    else if (attType == OFF_ATTACK)
        hitInfo = HITINFO_LEFTSWING;
    else
        return;                                             // ignore ranged case

    // melee attack spell casted at main hand attack only
    if (attType == BASE_ATTACK && m_currentSpells[CURRENT_MELEE_SPELL] && !extra)
        m_currentSpells[CURRENT_MELEE_SPELL]->cast();
    else
    {
        pVictim = GetMeleeHitRedirectTarget(pVictim);

        CalcDamageInfo damageInfo;
        CalculateMeleeDamage(pVictim, 0, &damageInfo, attType);
        // Send log damage message to client
        SendAttackStateUpdate(&damageInfo);
        DealMeleeDamage(&damageInfo, true);
        ProcDamageAndSpell(damageInfo.target, damageInfo.procAttacker, damageInfo.procVictim, damageInfo.procEx, damageInfo.damage, damageInfo.attackType);

        if (GetTypeId() == TYPEID_PLAYER)
            sLog->outDebug("AttackerStateUpdate: (Player) %u attacked %u (TypeId: %u) for %u dmg, absorbed %u, blocked %u, resisted %u.",
            GetGUIDLow(), pVictim->GetGUIDLow(), pVictim->GetTypeId(), damageInfo.damage, damageInfo.absorb, damageInfo.blocked_amount, damageInfo.resist);
        else
            sLog->outDebug("AttackerStateUpdate: (NPC)    %u attacked %u (TypeId: %u) for %u dmg, absorbed %u, blocked %u, resisted %u.",
            GetGUIDLow(), pVictim->GetGUIDLow(), pVictim->GetTypeId(), damageInfo.damage, damageInfo.absorb, damageInfo.blocked_amount, damageInfo.resist);
    }
}

MeleeHitOutcome Unit::RollMeleeOutcomeAgainst(const Unit *pVictim, WeaponAttackType attType) const
{
    // This is only wrapper

    // Miss chance based on melee
    float miss_chance = MeleeSpellMissChance(pVictim, attType, int32(GetWeaponSkillValue(attType, pVictim)) - int32(pVictim->GetDefenseSkillValue(this)), 0);

    // Critical hit chance
    float crit_chance = GetUnitCriticalChance(attType, pVictim);

    // stunned target cannot dodge and this is check in GetUnitDodgeChance() (returned 0 in this case)
    float dodge_chance = pVictim->GetUnitDodgeChance();
    float block_chance = pVictim->GetUnitBlockChance();
    float parry_chance = pVictim->GetUnitParryChance();

    // Useful if want to specify crit & miss chances for melee, else it could be removed
    sLog->outDebug("MELEE OUTCOME: miss %f crit %f dodge %f parry %f block %f", miss_chance, crit_chance, dodge_chance, parry_chance, block_chance);

    return RollMeleeOutcomeAgainst(pVictim, attType, int32(crit_chance*100), int32(miss_chance*100), int32(dodge_chance*100), int32(parry_chance*100), int32(block_chance*100));
}

MeleeHitOutcome Unit::RollMeleeOutcomeAgainst(const Unit *pVictim, WeaponAttackType attType, int32 crit_chance, int32 miss_chance, int32 dodge_chance, int32 parry_chance, int32 block_chance) const
{
    if (pVictim->GetTypeId() == TYPEID_UNIT && pVictim->ToCreature()->IsInEvadeMode())
        return MELEE_HIT_EVADE;

    int32 attackerMaxSkillValueForLevel = GetMaxSkillValueForLevel(pVictim);
    int32 victimMaxSkillValueForLevel = pVictim->GetMaxSkillValueForLevel(this);

    int32 attackerWeaponSkill = GetWeaponSkillValue(attType, pVictim);
    int32 victimDefenseSkill = pVictim->GetDefenseSkillValue(this);

    // bonus from skills is 0.04%
    int32    skillBonus  = 4 * (attackerWeaponSkill - victimMaxSkillValueForLevel);
    int32    sum = 0, tmp = 0;
    int32    roll = urand(0, 10000);

    sLog->outDebug("RollMeleeOutcomeAgainst: skill bonus of %d for attacker", skillBonus);
    sLog->outDebug("RollMeleeOutcomeAgainst: rolled %d, miss %d, dodge %d, parry %d, block %d, crit %d",
        roll, miss_chance, dodge_chance, parry_chance, block_chance, crit_chance);

    tmp = miss_chance;

    if (tmp > 0 && roll < (sum += tmp))
    {
        sLog->outDebug("RollMeleeOutcomeAgainst: MISS");
        return MELEE_HIT_MISS;
    }

    // always crit against a sitting target (except 0 crit chance)
    if (pVictim->GetTypeId() == TYPEID_PLAYER && crit_chance > 0 && pVictim->IsSitState())
    {
        sLog->outDebug("RollMeleeOutcomeAgainst: CRIT (sitting victim)");
        return MELEE_HIT_CRIT;
    }

    // Dodge chance

    // only players can't dodge if attacker is behind
    if (pVictim->GetTypeId() == TYPEID_PLAYER && !pVictim->HasInArc(float(M_PI), this))
        sLog->outDebug("RollMeleeOutcomeAgainst: attack came from behind and victim was a player.");
    else
    {
        // Reduce dodge chance by attacker expertise rating
        if (GetTypeId() == TYPEID_PLAYER)
            dodge_chance -= int32(ToPlayer()->GetExpertiseDodgeOrParryReduction(attType) * 100);

        // Modify dodge chance by attacker SPELL_AURA_MOD_COMBAT_RESULT_CHANCE
        dodge_chance +=  GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_COMBAT_RESULT_CHANCE, VICTIMSTATE_DODGE) * 100;
        dodge_chance = int32(float(dodge_chance) * GetTotalAuraMultiplier(SPELL_AURA_MOD_ENEMY_DODGE));

        tmp = dodge_chance;
        if ((tmp > 0)                                        // check if unit _can_ dodge
            && ((tmp -= skillBonus) > 0)
            && roll < (sum += tmp))
        {
            sLog->outDebug("RollMeleeOutcomeAgainst: DODGE <%d, %d)", sum-tmp, sum);
            return MELEE_HIT_DODGE;
        }
    }

    // parry & block chances
    // check if attack comes from behind, nobody can parry or block if attacker is behind
    if (!pVictim->HasInArc(float(M_PI), this))
        sLog->outDebug("RollMeleeOutcomeAgainst: attack came from behind.");
    else
    {
        // Reduce parry chance by attacker expertise rating
        if (GetTypeId() == TYPEID_PLAYER)
            parry_chance -= int32(ToPlayer()->GetExpertiseDodgeOrParryReduction(attType) * 100);

        if (pVictim->GetTypeId() == TYPEID_PLAYER || !(pVictim->ToCreature()->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_NO_PARRY))
        {
            parry_chance -= skillBonus;

            if (parry_chance > 0 &&                         // check if unit _can_ parry
                (roll < (sum += parry_chance)))
            {
                sLog->outDebug ("RollMeleeOutcomeAgainst: PARRY <%d, %d)", sum - parry_chance, sum);
                return MELEE_HIT_PARRY;
            }
        }

        if (pVictim->GetTypeId() == TYPEID_PLAYER || !(pVictim->ToCreature()->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_NO_BLOCK))
        {
            tmp = block_chance;
            if (tmp > 0                                          // check if unit _can_ block
                && (tmp -= skillBonus) > 0
                && roll < (sum += tmp))
            {
                sLog->outDebug ("RollMeleeOutcomeAgainst: BLOCK <%d, %d)", sum-tmp, sum);
                return MELEE_HIT_BLOCK;
            }
        }
    }

    // Critical chance
    tmp = crit_chance;

    if (tmp > 0 && roll < (sum += tmp))
    {
        sLog->outDebug("RollMeleeOutcomeAgainst: CRIT <%d, %d)", sum-tmp, sum);
        if (GetTypeId() == TYPEID_UNIT && (ToCreature()->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_NO_CRIT))
            sLog->outDebug("RollMeleeOutcomeAgainst: CRIT DISABLED)");
        else
            return MELEE_HIT_CRIT;
    }

    // Max 40% chance to score a glancing blow against mobs that are higher level (can do only players and pets and not with ranged weapon)
    if (attType != RANGED_ATTACK &&
        (GetTypeId() == TYPEID_PLAYER || ToCreature()->isPet()) &&
        pVictim->GetTypeId() != TYPEID_PLAYER && !pVictim->ToCreature()->isPet() &&
        getLevel() < pVictim->getLevelForTarget(this))
    {
        // cap possible value (with bonuses > max skill)
        int32 skill = attackerWeaponSkill;
        int32 maxskill = attackerMaxSkillValueForLevel;
        skill = (skill > maxskill) ? maxskill : skill;

        tmp = (10 + (victimDefenseSkill - skill)) * 100;
        tmp = tmp > 4000 ? 4000 : tmp;
        if (roll < (sum += tmp))
        {
            sLog->outDebug("RollMeleeOutcomeAgainst: GLANCING <%d, %d)", sum-4000, sum);
            return MELEE_HIT_GLANCING;
        }
    }

    // mobs can only score crushing blows with autoattack
    if (getLevelForTarget(pVictim) >= pVictim->getLevelForTarget(this) + 4 &&
        !IsControlledByPlayer() &&
        !(GetTypeId() == TYPEID_UNIT && ToCreature()->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_NO_CRUSH))
    {
        // when their weapon skill is 15 or more above victim's defense skill
        tmp = victimDefenseSkill;
        int32 tmpmax = victimMaxSkillValueForLevel;
        // having defense above your maximum (from items, talents etc.) has no effect
        tmp = tmp > tmpmax ? tmpmax : tmp;
        // tmp = mob's level * 5 - player's current defense skill
        tmp = attackerMaxSkillValueForLevel - tmp;
        if (tmp >= 15)
        {
            // add 2% chance per lacking skill point, min. is 15%
            tmp = tmp * 200 - 1500;
            if (roll < (sum += tmp))
            {
                sLog->outDebug("RollMeleeOutcomeAgainst: CRUSHING <%d, %d)", sum-tmp, sum);
                return MELEE_HIT_CRUSHING;
            }
        }
    }

    sLog->outDebug("RollMeleeOutcomeAgainst: NORMAL");
    return MELEE_HIT_NORMAL;
}

uint32 Unit::CalculateDamage(WeaponAttackType attType, bool normalized)
{
    float min_damage, max_damage;

    if (normalized && GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->CalculateMinMaxDamage(attType, normalized, min_damage, max_damage);
    else
    {
        switch (attType)
        {
            case RANGED_ATTACK:
                min_damage = GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE);
                max_damage = GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE);
                break;
            case BASE_ATTACK:
                min_damage = GetFloatValue(UNIT_FIELD_MINDAMAGE);
                max_damage = GetFloatValue(UNIT_FIELD_MAXDAMAGE);
                break;
            case OFF_ATTACK:
                min_damage = GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE);
                max_damage = GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE);
                break;
                // Just for good manner
            default:
                min_damage = 0.0f;
                max_damage = 0.0f;
                break;
        }
    }

    if (min_damage > max_damage)
    {
        std::swap(min_damage, max_damage);
    }

    if (max_damage == 0.0f)
        max_damage = 5.0f;

    return urand((uint32)min_damage, (uint32)max_damage);
}

float Unit::CalculateLevelPenalty(SpellEntry const* spellProto) const
{
    if (spellProto->spellLevel <= 0)
        return 1.0f;

    float LvlPenalty = 0.0f;

    if (spellProto->spellLevel < 20)
        LvlPenalty = (20.0f - spellProto->spellLevel) * 3.75f;
    float LvlFactor = (float(spellProto->spellLevel) + 6.0f) / float(getLevel());
    if (LvlFactor > 1.0f)
        LvlFactor = 1.0f;

    return (100.0f - LvlPenalty) * LvlFactor / 100.0f;
}

void Unit::SendMeleeAttackStart(Unit* pVictim)
{
    WorldPacket data(SMSG_ATTACKSTART, 8 + 8);
    data << uint64(GetGUID());
    data << uint64(pVictim->GetGUID());

    SendMessageToSet(&data, true);
    sLog->outDebug("WORLD: Sent SMSG_ATTACKSTART");
}

void Unit::SendMeleeAttackStop(Unit* victim)
{
    if (!victim)
        return;

    WorldPacket data(SMSG_ATTACKSTOP, (4+16));            // we guess size
    data << GetPackGUID();
    data << victim->GetPackGUID();                     // can be 0x00...
    data << uint32(0);                                      // can be 0x1
    SendMessageToSet(&data, true);
    sLog->outDetail("%s %u stopped attacking %s %u", (GetTypeId() == TYPEID_PLAYER ? "player" : "creature"), GetGUIDLow(), (victim->GetTypeId() == TYPEID_PLAYER ? "player" : "creature"), victim->GetGUIDLow());

    /*if (victim->GetTypeId() == TYPEID_UNIT)
    victim->ToCreature()->AI().EnterEvadeMode(this);*/
}

bool Unit::isSpellBlocked(Unit *pVictim, SpellEntry const *spellProto, WeaponAttackType attackType)
{
    // These spells can't be blocked
    if (spellProto && spellProto->Attributes & SPELL_ATTR_IMPOSSIBLE_DODGE_PARRY_BLOCK)
        return false;

    if (pVictim->HasInArc(M_PI, this))
    {
        float blockChance = pVictim->GetUnitBlockChance();

        float fAttackerSkill = GetWeaponSkillValue(attackType, pVictim)*0.04;
        float fDefenserSkill = pVictim->GetDefenseSkillValue(this)*0.04;

        blockChance += (fDefenserSkill - fAttackerSkill);
        if (blockChance < 0.0)
            blockChance = 0.0;

        if (roll_chance_f(blockChance))
            return true;
    }
    return false;
}

// Melee based spells can be miss, parry or dodge on this step
// Crit or block - determined on damage calculation phase! (and can be both in some time)
float Unit::MeleeSpellMissChance(const Unit *pVictim, WeaponAttackType attType, int32 skillDiff, uint32 spellId) const
{
    float missChance = pVictim->GetUnitMissChance(attType);

    // If attacker is dual-wielding and not ranged + not special attack
   if (haveOffhandWeapon() && !spellId && attType != RANGED_ATTACK)
      missChance += 19.0f;

    // bonus from skills is 0.04%
    //miss_chance -= skillDiff * 0.04f;
    int32 diff = -skillDiff;
    if (pVictim->GetTypeId() == TYPEID_PLAYER)
        missChance += diff > 0 ? diff * 0.04f : diff * 0.02f;
    else
        missChance += diff > 10 ? 1 + (diff - 10) * 0.4f : diff * 0.1f;

   // Calculate hit chance
    float hitChance = 100.0f;

    // Spellmod from SPELLMOD_RESIST_MISS_CHANCE
    if (spellId)
    {
        if (Player *modOwner = GetSpellModOwner())
            modOwner->ApplySpellMod(spellId, SPELLMOD_RESIST_MISS_CHANCE, hitChance);
    }

    missChance += hitChance - 100.0f;

    if (attType == RANGED_ATTACK)
        missChance -= m_modRangedHitChance;
    else
        missChance -= m_modMeleeHitChance;

    // Limit miss chance from 0 to 60%
    if (missChance < 0.0f)
        return 0.0f;
    if (missChance > 60.0f)
        return 60.0f;
    return missChance;
}

int32 Unit::GetMechanicResistChance(SpellEntry const* spellInfo) const
{
    if (!spellInfo)
        return 0;

    int32 resistMech = 0;
    for (uint8 eff = 0; eff < MAX_SPELL_EFFECTS; ++eff)
    {
        if (spellInfo->Effect[eff] == 0)
           break;

        int32 effectMech = GetEffectMechanic(spellInfo, eff);
        if (effectMech)
        {
            int32 temp = GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_MECHANIC_RESISTANCE, effectMech);
            if (resistMech < temp)
                resistMech = temp;
        }
    }

    return resistMech;
}

// Melee based spells hit result calculations
SpellMissInfo Unit::MeleeSpellHitResult(Unit *pVictim, SpellEntry const *spell)
{
    // Spells with SPELL_ATTR3_IGNORE_HIT_RESULT will additionally fully ignore
    // resist and deflect chances
    if (spell->AttributesEx3 & SPELL_ATTR_EX3_IGNORE_HIT_RESULT)
        return SPELL_MISS_NONE;

    WeaponAttackType attType = BASE_ATTACK;

    if (spell->DmgClass == SPELL_DAMAGE_CLASS_RANGED)
        attType = RANGED_ATTACK;

    // bonus from skills is 0.04% per skill Diff
    int32 attackerWeaponSkill = !(spell->DmgClass == SPELL_DAMAGE_CLASS_RANGED && !(spell->Attributes & SPELL_ATTR_RANGED) )
        ? int32(GetWeaponSkillValue(attType, pVictim))
        : int32(GetMaxSkillValueForLevel());

    int32 skillDiff = attackerWeaponSkill - int32(pVictim->GetMaxSkillValueForLevel(this));
    int32 fullSkillDiff = attackerWeaponSkill - int32(pVictim->GetDefenseSkillValue(this));

    uint32 roll = urand (0, 10000);

    bool isCasting = pVictim->IsNonMeleeSpellCasted(false);
    bool lostControl = pVictim->hasUnitState(UNIT_STAT_LOST_CONTROL);

    uint32 missChance = uint32(MeleeSpellMissChance(pVictim, attType, fullSkillDiff, spell->Id) * 100.0f);
    // Roll miss
    uint32 tmp = missChance;
    if (roll < tmp)
        return SPELL_MISS_MISS;

    // Same spells cannot be parry/dodge/block
    if (spell->Attributes & SPELL_ATTR_IMPOSSIBLE_DODGE_PARRY_BLOCK)
        return SPELL_MISS_NONE;

    bool canDodge = true;
    bool canParry = true;
    bool canBlock = spell->AttributesEx3 & SPELL_ATTR_EX3_BLOCKABLE_SPELL;

    // Check for attack from behind
    if (!pVictim->HasInArc(M_PI, this))
    {
        // Can`t dodge from behind in PvP (but its possible in PvE)
        if (pVictim->GetTypeId() == TYPEID_PLAYER)
            canDodge = false;
        // Can`t parry or block
        canParry = false;
        canBlock = false;
    }

    // Ranged attacks can only miss, resist and deflect
    if (attType == RANGED_ATTACK)
    {
        canParry = false;
        canDodge = false;
    }

    // Ignore combat result aura
    AuraList const& ignore = GetAurasByType(SPELL_AURA_IGNORE_COMBAT_RESULT);
    for (AuraList::const_iterator i = ignore.begin(); i != ignore.end(); ++i)
    {
        if (!sSpellMgr->IsAffectedBySpell(spell, (*i)->GetId(), (*i)->GetEffIndex(), (*i)->GetSpellProto()->SpellFamilyFlags))
            continue;

        switch ((*i)->GetMiscValue())
        {
            case MELEE_HIT_DODGE:
                canDodge = false;
                break;
            case MELEE_HIT_BLOCK:
                canBlock = false;
                break;
            case MELEE_HIT_PARRY:
                canParry = false;
                break;
            default:
                break;
        }
    }

    if (canDodge)
    {
        // Roll dodge
        int32 dodgeChance = int32(pVictim->GetUnitDodgeChance() * 100.0f) - skillDiff * 4;
        // Reduce enemy dodge chance by SPELL_AURA_MOD_COMBAT_RESULT_CHANCE
        dodgeChance += GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_COMBAT_RESULT_CHANCE, VICTIMSTATE_DODGE) * 100;
        dodgeChance = int32(float(dodgeChance) * GetTotalAuraMultiplier(SPELL_AURA_MOD_ENEMY_DODGE));
        // Reduce dodge chance by attacker expertise rating
        if (GetTypeId() == TYPEID_PLAYER)
            dodgeChance -= int32(ToPlayer()->GetExpertiseDodgeOrParryReduction(attType) * 100.0f);
        if (dodgeChance < 0)
            dodgeChance = 0;

        if (roll < (tmp += dodgeChance))
            return SPELL_MISS_DODGE;
    }

    if (canParry)
    {
        // Roll parry
        int32 parryChance = int32(pVictim->GetUnitParryChance() * 100.0f)  - skillDiff * 4;
        // Reduce parry chance by attacker expertise rating
        if (GetTypeId() == TYPEID_PLAYER)
            parryChance -= int32(ToPlayer()->GetExpertiseDodgeOrParryReduction(attType) * 100.0f);
        if (parryChance < 0)
            parryChance = 0;

        tmp += parryChance;
        if (roll < tmp)
            return SPELL_MISS_PARRY;
    }

    if (canBlock)
    {
        int32 blockChance = int32(pVictim->GetUnitBlockChance() * 100.0f)  - skillDiff * 4;
        if (blockChance < 0)
            blockChance = 0;
        tmp += blockChance;

        if (roll < tmp)
            return SPELL_MISS_BLOCK;
    }

    return SPELL_MISS_NONE;
}

// TODO need use unit spell resistances in calculations
// Initial Binary Spell Check here
SpellMissInfo Unit::MagicSpellHitResult(Unit *pVictim, SpellEntry const *spell)
{
    // Can`t miss on dead target (on skinning for example)
    if ((!pVictim->isAlive() && pVictim->GetTypeId() != TYPEID_PLAYER) || spell->AttributesEx3 & SPELL_ATTR_EX3_IGNORE_HIT_RESULT)
        return SPELL_MISS_NONE;

    SpellSchoolMask schoolMask = GetSpellSchoolMask(spell);
    // PvP - PvE spell misschances per leveldif > 2
    int32 lchance = pVictim->GetTypeId() == TYPEID_PLAYER ? 7 : 11;
    int32 leveldif = int32(pVictim->getLevelForTarget(this)) - int32(getLevelForTarget(pVictim));

    // Base hit chance from attacker and victim levels
    int32 modHitChance;
    if (leveldif < 3)
        modHitChance = 96 - leveldif;
    else
        modHitChance = 94 - (leveldif - 2) * lchance;

    // base hit chance for holy spells should be 100
    if (schoolMask & SPELL_SCHOOL_MASK_HOLY)
        modHitChance = 100;

    // Spellmod from SPELLMOD_RESIST_MISS_CHANCE
    if (Player *modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spell->Id, SPELLMOD_RESIST_MISS_CHANCE, modHitChance);
    // Increase from attacker SPELL_AURA_MOD_INCREASES_SPELL_PCT_TO_HIT auras
    modHitChance+=GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_INCREASES_SPELL_PCT_TO_HIT, schoolMask);
    // Chance hit from victim SPELL_AURA_MOD_ATTACKER_SPELL_HIT_CHANCE auras
    modHitChance+= pVictim->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_ATTACKER_SPELL_HIT_CHANCE, schoolMask);

    // Reduce spell hit chance for Area of effect spells from victim SPELL_AURA_MOD_AOE_AVOIDANCE aura
    if (IsAreaOfEffectSpell(spell) && !IsFriendlyTo(pVictim))
        modHitChance-=pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_AOE_AVOIDANCE);
    // Reduce spell hit chance for dispel mechanic spells from victim SPELL_AURA_MOD_DISPEL_RESIST
    if (IsDispelSpell(spell))
        modHitChance-=pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_DISPEL_RESIST);
    // Chance resist debuff
    modHitChance -= pVictim->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_DEBUFF_RESISTANCE, int32(spell->Dispel));

    int32 HitChance = modHitChance * 100;
    // Increase hit chance from attacker SPELL_AURA_MOD_SPELL_HIT_CHANCE and attacker ratings
    HitChance += int32(m_modSpellHitChance*100.0f);

    // Decrease hit chance from victim rating bonus
    if (pVictim->GetTypeId() == TYPEID_PLAYER)
        HitChance -= int32(pVictim->ToPlayer()->GetRatingBonusValue(CR_HIT_TAKEN_SPELL)*100.0f);

    if (HitChance <  100) HitChance =  100;
    if (HitChance > 10000) HitChance = 10000;

    uint32 rand = urand(0, 10000);
    if (rand > HitChance)
        return SPELL_MISS_RESIST;

    // binary resist spells
    if (IsBinaryResistable(spell) && CalcBinaryResist(pVictim, schoolMask))
        return SPELL_MISS_RESIST;

    return SPELL_MISS_NONE;
}

SpellMissInfo Unit::MagicAuraHitResult(Unit* victim, SpellEntry const* spellInfo)
{
    uint32 hitChance = 100;
    SpellSchoolMask schoolMask = SpellSchoolMask(spellInfo->SchoolMask);

    // SPELL_AURA_MOD_MECHANIC_RESISTANCE
    hitChance -= victim->GetMechanicResistChance(spellInfo);
    // SPELLMOD_RESIST_MISS_CHANCE
    if (Player *modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellInfo->Id, SPELLMOD_RESIST_MISS_CHANCE, hitChance);
    // Increase from caster SPELL_AURA_MOD_INCREASES_SPELL_PCT_TO_HIT auras
    hitChance += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_INCREASES_SPELL_PCT_TO_HIT, schoolMask);
    // Increase from victim SPELL_AURA_MOD_ATTACKER_SPELL_HIT_CHANCE auras
    hitChance += victim->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_ATTACKER_SPELL_HIT_CHANCE, schoolMask);

    if (urand(0, 100) > hitChance)
        return SPELL_MISS_RESIST;

    return SPELL_MISS_NONE;
}

// Calculate spell hit result can be:
// Every spell can: Evade/Immune/Reflect/Sucesful hit
// For melee based spells:
//   Miss
//   Dodge
//   Parry
// For spells
//   Resist
SpellMissInfo Unit::SpellHitResult(Unit *pVictim, SpellEntry const *spell, bool CanReflect)
{
    // Self casted spell can never miss
    if (pVictim == this)
        return SPELL_MISS_NONE;

    // Return evade for units in evade mode
    if (pVictim->GetTypeId() == TYPEID_UNIT && pVictim->ToCreature()->IsInEvadeMode() && this != pVictim)
        return SPELL_MISS_EVADE;

    // Check for immune (use charges)
    if (pVictim->IsImmunedToSpell(spell, true))
        return SPELL_MISS_IMMUNE;

    // All positive spells can`t miss
    // TODO: client not show miss log for this spells - so need find info for this in dbc and use it!
    if (IsPositiveSpell(spell->Id) && (!IsHostileTo(pVictim)) && !pVictim->hasUnitState(UNIT_STAT_ISOLATED))  //prevent from affecting enemy by "positive" spell
        return SPELL_MISS_NONE;

    // Check for immune (use charges)
    // Check if Spell cannot be immuned
    if (!(spell->Attributes & SPELL_ATTR_UNAFFECTED_BY_INVULNERABILITY))
        if (pVictim->IsImmunedToDamage(GetSpellSchoolMask(spell), true))
            return SPELL_MISS_IMMUNE;

    if (this == pVictim)
        return SPELL_MISS_NONE;

    // Try victim reflect spell
    if (CanReflect)
    {
        int32 reflectchance = pVictim->GetTotalAuraModifier(SPELL_AURA_REFLECT_SPELLS);
        Unit::AuraList const& mReflectSpellsSchool = pVictim->GetAurasByType(SPELL_AURA_REFLECT_SPELLS_SCHOOL);
        for (Unit::AuraList::const_iterator i = mReflectSpellsSchool.begin(); i != mReflectSpellsSchool.end(); ++i)
            if ((*i)->GetModifier()->m_miscvalue & GetSpellSchoolMask(spell))
                reflectchance = (*i)->GetModifierValue();

        if (reflectchance > 0 && roll_chance_i(reflectchance))
            return SPELL_MISS_REFLECT;
    }

    switch (spell->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_RANGED:
        case SPELL_DAMAGE_CLASS_MELEE:
            return MeleeSpellHitResult(pVictim, spell);
        case SPELL_DAMAGE_CLASS_NONE:
            return SPELL_MISS_NONE;
        case SPELL_DAMAGE_CLASS_MAGIC:
            return MagicSpellHitResult(pVictim, spell);
    }
    return SPELL_MISS_NONE;
}

float Unit::GetUnitMissChance(WeaponAttackType attType) const
{
    float missChance = 5.0f;

    if (attType == RANGED_ATTACK)
        missChance -= GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_RANGED_HIT_CHANCE);
    else
        missChance -= GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_MELEE_HIT_CHANCE);

    return missChance;
}

uint32 Unit::GetDefenseSkillValue(Unit const* target) const
{
    if (GetTypeId() == TYPEID_PLAYER)
    {
        // in PvP use full skill instead current skill value
        uint32 value = (target && target->GetTypeId() == TYPEID_PLAYER)
            ? ToPlayer()->GetMaxSkillValue(SKILL_DEFENSE)
            : ToPlayer()->GetSkillValue(SKILL_DEFENSE);
        value += uint32(ToPlayer()->GetRatingBonusValue(CR_DEFENSE_SKILL));
        return value;
    }
    else
        return GetUnitMeleeSkill(target);
}

float Unit::GetUnitDodgeChance() const
{
    if (IsNonMeleeSpellCasted(false) || hasUnitState(UNIT_STAT_LOST_CONTROL))
        return 0.0f;

    if (GetTypeId() == TYPEID_PLAYER)
        return GetFloatValue(PLAYER_DODGE_PERCENTAGE);
    else
    {
        if (ToCreature()->isTotem())
            return 0.0f;
        else if (ToCreature()->isShadowfiend())
            return 85.0f;
        else
        {
            float dodge = 5.0f;
            dodge += GetTotalAuraModifier(SPELL_AURA_MOD_DODGE_PERCENT);
            return dodge > 0.0f ? dodge : 0.0f;
        }
    }
}

float Unit::GetUnitParryChance() const
{
    if (IsNonMeleeSpellCasted(false) || hasUnitState(UNIT_STAT_LOST_CONTROL))
        return 0.0f;

    float chance = 0.0f;

    if (Player const* player = ToPlayer())
    {
        if (player->CanParry())
        {
            Item *tmpitem = player->GetWeaponForAttack(BASE_ATTACK, true);
            if (!tmpitem)
                tmpitem = player->GetWeaponForAttack(OFF_ATTACK, true);

            if (tmpitem)
                chance = GetFloatValue(PLAYER_PARRY_PERCENTAGE);
        }
    }
    else if (GetTypeId() == TYPEID_UNIT)
    {
        if (ToCreature()->isTotem())
            chance = 0.0f;
        else if (GetCreatureType() == CREATURE_TYPE_HUMANOID || CREATURE_TYPE_DRAGONKIN || CREATURE_TYPE_ELEMENTAL || CREATURE_TYPE_GIANT  || CREATURE_TYPE_GIANT || CREATURE_TYPE_DEMON)
        {
            chance = 5.0f;
            chance += GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT);
        }
        /// @todo: robinsch: iirc this should be handeled by auras
        else if (GetEntry() == 17252 || GetEntry() == 1860 || GetEntry() == 1863)   // Felguard, Succubus and Voidwalker have additional parry
        {
            chance = 8.62f;
            chance += GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT);
        }
        else if (GetEntry() == 417) // Felhunter
        {
            chance = 6.0f;
            chance += GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT);
        }
        else if (ToCreature()->isPet()) // Hunter Pets
        {
            if (Pet *pet = ((Pet*)this))
                if (pet->getPetType() == HUNTER_PET)
                {
                    chance = 5.0f;
                    chance += GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT);
                }
        }

    }

    return chance > 0.0f ? chance : 0.0f;
}

float Unit::GetUnitBlockChance() const
{
    if (IsNonMeleeSpellCasted(false) || hasUnitState(UNIT_STAT_LOST_CONTROL))
        return 0.0f;

    if (Player const* player = ToPlayer())
    {
        if (player->CanBlock())
        {
            Item *tmpitem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            if (tmpitem && !tmpitem->IsBroken() && tmpitem->GetProto()->Block)
                return GetFloatValue(PLAYER_BLOCK_PERCENTAGE);
        }
        // is player but has no block ability or no not broken shield equipped
        return 0.0f;
    }
    else
    {
        if (ToCreature()->isTotem())
            return 0.0f;
        /// @todo: robinsch: iirc this should be handeled by auras
        else if (GetEntry() == 17252 || GetEntry() == 1860 || GetEntry() == 1863)   // Felguard, Succubus and Voidwalker have additional block
            return 20.0f;
        else if (GetEntry() == 417) // Felhunter
            return 10.0f;
        else
        {
            float block = 5.0f;
            block += GetTotalAuraModifier(SPELL_AURA_MOD_BLOCK_PERCENT);
            return block > 0.0f ? block : 0.0f;
        }
    }
}

float Unit::GetUnitCriticalChance(WeaponAttackType attackType, const Unit *pVictim) const
{
    float crit;

    if (GetTypeId() == TYPEID_PLAYER)
    {
        switch (attackType)
        {
            case BASE_ATTACK:
                crit = GetFloatValue(PLAYER_CRIT_PERCENTAGE);
                break;
            case OFF_ATTACK:
                crit = GetFloatValue(PLAYER_OFFHAND_CRIT_PERCENTAGE);
                break;
            case RANGED_ATTACK:
                crit = GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE);
                break;
                // Just for good manner
            default:
                crit = 0.0f;
                break;
        }
    }
    else
    {
        crit = 5.0f;
        crit += GetTotalAuraModifier(SPELL_AURA_MOD_CRIT_PERCENT);
    }

    // flat aura mods
    if (attackType == RANGED_ATTACK)
        crit += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_CHANCE);
    else
        crit += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_CHANCE);

    crit += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_SPELL_AND_WEAPON_CRIT_CHANCE);

    // reduce crit chance from Rating for players
    if (pVictim->GetTypeId() == TYPEID_PLAYER)
    {
        if (attackType == RANGED_ATTACK)
            crit -= pVictim->ToPlayer()->GetRatingBonusValue(CR_CRIT_TAKEN_RANGED);
        else
            crit -= pVictim->ToPlayer()->GetRatingBonusValue(CR_CRIT_TAKEN_MELEE);
    }

    if (crit < 0.0f)
        crit = 0.0f;
    return crit;
}

uint32 Unit::GetWeaponSkillValue (WeaponAttackType attType, Unit const* target) const
{
    uint32 value = 0;
    if (GetTypeId() == TYPEID_PLAYER)
    {
        Item* item = ToPlayer()->GetWeaponForAttack(attType, true);

        // feral or unarmed skill only for base attack
        if (attType != BASE_ATTACK && !item)
        {
            if (attType == RANGED_ATTACK && getClass() == CLASS_PALADIN) //hammer
                return GetMaxSkillValueForLevel();
            return 0;
        }

        if (ToPlayer()->IsInFeralForm())
            return GetMaxSkillValueForLevel();              // always maximized SKILL_FERAL_COMBAT in fact

        // weapon skill or (unarmed for base attack and fist weapons)
        uint32  skill = item && item->GetSkill() != SKILL_FIST_WEAPONS ? item->GetSkill() : SKILL_UNARMED;

        // in PvP use full skill instead current skill value
        value = (target && target->isCharmedOwnedByPlayerOrPlayer())
            ? ToPlayer()->GetMaxSkillValue(skill)
            : ToPlayer()->GetSkillValue(skill);
        // Modify value from ratings
        value += uint32(ToPlayer()->GetRatingBonusValue(CR_WEAPON_SKILL));
        switch (attType)
        {
            case BASE_ATTACK:   value += uint32(ToPlayer()->GetRatingBonusValue(CR_WEAPON_SKILL_MAINHAND)); break;
            case OFF_ATTACK:    value += uint32(ToPlayer()->GetRatingBonusValue(CR_WEAPON_SKILL_OFFHAND));  break;
            case RANGED_ATTACK: value += uint32(ToPlayer()->GetRatingBonusValue(CR_WEAPON_SKILL_RANGED));   break;
        }
    }
    else
        value = GetUnitMeleeSkill(target);
   return value;
}

void Unit::_DeleteAuras()
{
    while (!m_removedAuras.empty())
    {
        delete m_removedAuras.front();
        m_removedAuras.pop_front();
    }
}

void Unit::_UpdateSpells(uint32 time)
{
    if (m_currentSpells[CURRENT_AUTOREPEAT_SPELL])
        _UpdateAutoRepeatSpell();

    // remove finished spells from current pointers
    for (uint32 i = 0; i < CURRENT_MAX_SPELL; ++i)
    {
        if (m_currentSpells[i] && m_currentSpells[i]->getState() == SPELL_STATE_FINISHED)
        {
            m_currentSpells[i]->SetReferencedFromCurrent(false);
            m_currentSpells[i] = NULL;                      // remove pointer
        }
    }

    // m_AurasUpdateIterator can be updated in inderect called code at aura remove to skip next planned to update but removed auras
    for (m_AurasUpdateIterator = m_Auras.begin(); m_AurasUpdateIterator != m_Auras.end();)
    {
        Aura* i_aura = m_AurasUpdateIterator->second;
        ++m_AurasUpdateIterator;                            // need shift to next for allow update if need into aura update
        i_aura->Update(time);

        Unit* target = i_aura->GetTarget();
        Unit* caster = i_aura->GetCaster();

        if (target && target->GetTypeId() == TYPEID_PLAYER && caster && caster->GetTypeId() == TYPEID_PLAYER)
            if (!i_aura->IsPassive())
                ((Player*)target)->SendArenaSpectatorAura(false, i_aura->GetStackAmount(), i_aura->GetAuraDuration(), 
                    i_aura->GetAuraMaxDuration(), i_aura->GetId(), i_aura->IsPeriodic(), i_aura->IsPositive(), ((Player*)caster)->GetGUID());
    }

    // remove expired auras
    for (AuraMap::iterator i = m_Auras.begin(); i != m_Auras.end();)
    {
        if (i->second->IsExpired())
            RemoveAura(i);
        else
            ++i;
    }

    _DeleteAuras();

    if (!m_gameObj.empty())
    {
        std::list<GameObject*>::iterator itr;
        for (itr = m_gameObj.begin(); itr != m_gameObj.end();)
        {
            if (!(*itr)->isSpawned())
            {
                (*itr)->SetOwnerGUID(0);
                (*itr)->SetRespawnTime(0);
                (*itr)->Delete();
                m_gameObj.erase(itr++);
            }
            else
                ++itr;
        }
    }
}

void Unit::_UpdateAutoRepeatSpell()
{
    //check "realtime" interrupts
    if ((GetTypeId() == TYPEID_PLAYER && isMoving()) || IsNonMeleeSpellCasted(false, false, true))
    {
        // cancel wand shoot
        if (m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_spellInfo->Category == 351)
            InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
        m_AutoRepeatFirstCast = true;
        return;
    }

    //apply delay
    if (m_AutoRepeatFirstCast && getAttackTimer(RANGED_ATTACK) < 500)
        setAttackTimer(RANGED_ATTACK, 500);
    m_AutoRepeatFirstCast = false;

    //castroutine
    if (isAttackReady(RANGED_ATTACK))
    {
        // Check if able to cast
        if (m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->CanCast(true))
        {
            InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
            return;
        }

        // we want to shoot
        Spell* spell = new Spell(this, m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_spellInfo, true, 0);
        spell->prepare(&(m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_targets));

        // all went good, reset attack
        resetAttackTimer(RANGED_ATTACK);
    }
}

void Unit::SetCurrentCastedSpell(Spell * pSpell)
{
    ASSERT(pSpell);                                         // NULL may be never passed here, use InterruptSpell or InterruptNonMeleeSpells

    CurrentSpellTypes CSpellType = pSpell->GetCurrentContainer();

    if (pSpell == m_currentSpells[CSpellType])              // avoid breaking self
        return;

    // break same type spell if it is not delayed
    InterruptSpell(CSpellType, false);

    // special breakage effects:
    switch (CSpellType)
    {
        case CURRENT_GENERIC_SPELL:
        {
            // generic spells always break channeled not delayed spells
            InterruptSpell(CURRENT_CHANNELED_SPELL, false);

            // autorepeat breaking
            if (m_currentSpells[CURRENT_AUTOREPEAT_SPELL])
            {
                // break autorepeat if not Auto Shot
                if (m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_spellInfo->Id != 75)
                    InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
                m_AutoRepeatFirstCast = true;
            }

            if (GetSpellCastTime(pSpell->m_spellInfo) > 0)
                addUnitState(UNIT_STAT_CASTING);

            break;
        }
        case CURRENT_CHANNELED_SPELL:
        {
            // channel spells always break generic non-delayed and any channeled spells
            InterruptSpell(CURRENT_GENERIC_SPELL, false);
            InterruptSpell(CURRENT_CHANNELED_SPELL);

            // it also does break autorepeat if not Auto Shot
            if (m_currentSpells[CURRENT_AUTOREPEAT_SPELL] &&
                m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_spellInfo->Id != 75)
                InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
            addUnitState(UNIT_STAT_CASTING);

            break;
        }
        case CURRENT_AUTOREPEAT_SPELL:
        {
            // only Auto Shoot does not break anything
            if (pSpell->m_spellInfo->Id != 75)
            {
                // generic autorepeats break generic non-delayed and channeled non-delayed spells
                InterruptSpell(CURRENT_GENERIC_SPELL, false);
                InterruptSpell(CURRENT_CHANNELED_SPELL, false);
            }
            // special action: set first cast flag
            m_AutoRepeatFirstCast = true;

            break;
        }
        default:
            break; // other spell types don't break anything now
    }

    // current spell (if it is still here) may be safely deleted now
    if (m_currentSpells[CSpellType])
        m_currentSpells[CSpellType]->SetReferencedFromCurrent(false);

    // set new current spell
    m_currentSpells[CSpellType] = pSpell;
    pSpell->SetReferencedFromCurrent(true);

    pSpell->m_selfContainer = &(m_currentSpells[pSpell->GetCurrentContainer()]);
}

void Unit::InterruptSpell(CurrentSpellTypes spellType, bool withDelayed, bool withInstant)
{
    ASSERT(spellType < CURRENT_MAX_SPELL);

    Spell *spell = m_currentSpells[spellType];
    if (spell
        && (withDelayed || spell->getState() != SPELL_STATE_DELAYED)
        && (withInstant || spell->GetCastTime() > 0))
    {
        // for example, do not let self-stun aura interrupt itself
        if (!spell->IsInterruptable())
            return;

        m_currentSpells[spellType] = NULL;

        // send autorepeat cancel message for autorepeat spells
        if (spellType == CURRENT_AUTOREPEAT_SPELL)
        {
            if (GetTypeId() == TYPEID_PLAYER)
                ToPlayer()->SendAutoRepeatCancel();
        }

        if (GetTypeId() == TYPEID_PLAYER)
            ((Player*)this)->SendArenaSpectatorSpell(spell->m_spellInfo->Id, 99999);

        if (spell->getState() != SPELL_STATE_FINISHED)
            spell->cancel();
        spell->SetReferencedFromCurrent(false);
    }
}

void Unit::FinishSpell(CurrentSpellTypes spellType, bool ok /*= true*/)
{
    Spell* spell = m_currentSpells[spellType];
    if (!spell)
        return;

    if (spellType == CURRENT_CHANNELED_SPELL)
        spell->SendChannelUpdate(0);

    spell->finish(ok);
}

bool Unit::IsNonMeleeSpellCasted(bool withDelayed, bool skipChanneled, bool skipAutorepeat) const
{
    // We don't do loop here to explicitly show that melee spell is excluded.
    // Maybe later some special spells will be excluded too.

    // generic spells are casted when they are not finished and not delayed
    if (m_currentSpells[CURRENT_GENERIC_SPELL] &&
        (m_currentSpells[CURRENT_GENERIC_SPELL]->getState() != SPELL_STATE_FINISHED) &&
        (withDelayed || m_currentSpells[CURRENT_GENERIC_SPELL]->getState() != SPELL_STATE_DELAYED))
        return(true);

    // channeled spells may be delayed, but they are still considered casted
    else if (!skipChanneled && m_currentSpells[CURRENT_CHANNELED_SPELL] &&
        (m_currentSpells[CURRENT_CHANNELED_SPELL]->getState() != SPELL_STATE_FINISHED))
        return(true);

    // autorepeat spells may be finished or delayed, but they are still considered casted
    else if (!skipAutorepeat && m_currentSpells[CURRENT_AUTOREPEAT_SPELL])
        return(true);

    return(false);
}

void Unit::InterruptNonMeleeSpells(bool withDelayed, uint32 spell_id, bool withInstant)
{
    // generic spells are interrupted if they are not finished or delayed
    if (m_currentSpells[CURRENT_GENERIC_SPELL] && (!spell_id || m_currentSpells[CURRENT_GENERIC_SPELL]->m_spellInfo->Id == spell_id))
        InterruptSpell(CURRENT_GENERIC_SPELL, withDelayed, withInstant);

    // autorepeat spells are interrupted if they are not finished or delayed
    if (m_currentSpells[CURRENT_AUTOREPEAT_SPELL] && (!spell_id || m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_spellInfo->Id == spell_id))
        InterruptSpell(CURRENT_AUTOREPEAT_SPELL, withDelayed, withInstant);

    // channeled spells are interrupted if they are not finished, even if they are delayed
    if (m_currentSpells[CURRENT_CHANNELED_SPELL] && (!spell_id || m_currentSpells[CURRENT_CHANNELED_SPELL]->m_spellInfo->Id == spell_id))
        InterruptSpell(CURRENT_CHANNELED_SPELL, true, true);
}

Spell* Unit::FindCurrentSpellBySpellId(uint32 spell_id) const
{
    for (uint32 i = 0; i < CURRENT_MAX_SPELL; i++)
        if (m_currentSpells[i] && m_currentSpells[i]->m_spellInfo->Id == spell_id)
            return m_currentSpells[i];
    return NULL;
}

int32 Unit::GetCurrentSpellCastTime(uint32 spell_id) const
{
    if (Spell const * spell = FindCurrentSpellBySpellId(spell_id))
        return spell->GetCastTime();
    return 0;
}

bool Unit::isInFrontInMap(Unit const* target, float distance,  float arc) const
{
    return IsWithinDistInMap(target, distance) && HasInArc(arc, target);
}

bool Unit::isInBackInMap(Unit const* target, float distance, float arc) const
{
    return IsWithinDistInMap(target, distance) && !HasInArc(2 * M_PI - arc, target);
}

void Unit::SetFacingToObject(WorldObject* pObject)
{
    // update orientation at server
    SetOrientation(GetAngle(pObject));

    // and client
    WorldPacket data;
    BuildHeartBeatMsg(&data);
    SendMessageToSet(&data, false);
}

void Unit::SetFacingToOrientation(float orientation)
{
    // update orientation at server
    SetOrientation(orientation);

    // and client
    WorldPacket data;
    BuildHeartBeatMsg(&data);
    SendMessageToSet(&data, false);
}

bool Unit::isInAccessiblePlaceFor(Creature const* c) const
{
    if (IsInWater())
        return c->canSwim();
    else
        return c->canWalk() || c->canFly();
}

bool Unit::IsInWater() const
{
    return GetBaseMap()->IsInWater(GetPositionX(), GetPositionY(), GetPositionZ());
}

bool Unit::IsUnderWater() const
{
    return GetBaseMap()->IsUnderWater(GetPositionX(), GetPositionY(), GetPositionZ());
}

void Unit::DeMorph()
{
    SetDisplayId(GetNativeDisplayId());
}

int32 Unit::GetTotalAuraModifier(AuraType auratype) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
        modifier += (*i)->GetModifierValue();

    return modifier;
}

float Unit::GetTotalAuraMultiplier(AuraType auratype) const
{
    float multiplier = 1.0f;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
        multiplier *= (100.0f + (*i)->GetModifierValue())/100.0f;

    return multiplier;
}

int32 Unit::GetMaxPositiveAuraModifier(AuraType auratype) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        int32 amount = (*i)->GetModifierValue();
        if (amount > modifier)
            modifier = amount;
    }

    return modifier;
}

int32 Unit::GetMaxNegativeAuraModifier(AuraType auratype) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        int32 amount = (*i)->GetModifierValue();
        if (amount < modifier)
            modifier = amount;
    }

    return modifier;
}

int32 Unit::GetTotalAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue & misc_mask)
            modifier += (*i)->GetModifierValue();
    }
    return modifier;
}

float Unit::GetTotalAuraMultiplierByMiscMask(AuraType auratype, uint32 misc_mask) const
{
    float multiplier = 1.0f;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue & misc_mask)
            multiplier *= (100.0f + (*i)->GetModifierValue())/100.0f;
    }
    return multiplier;
}

int32 Unit::GetMaxPositiveAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        int32 amount = (*i)->GetModifierValue();
        if (mod->m_miscvalue & misc_mask && amount > modifier)
            modifier = amount;
    }

    return modifier;
}

int32 Unit::GetMaxNegativeAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        int32 amount = (*i)->GetModifierValue();
        if (mod->m_miscvalue & misc_mask && amount < modifier)
            modifier = amount;
    }

    return modifier;
}

int32 Unit::GetTotalAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue == misc_value)
            modifier += (*i)->GetModifierValue();
    }
    return modifier;
}

float Unit::GetTotalAuraMultiplierByMiscValue(AuraType auratype, int32 misc_value) const
{
    float multiplier = 1.0f;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue == misc_value)
            multiplier *= (100.0f + (*i)->GetModifierValue())/100.0f;
    }
    return multiplier;
}

int32 Unit::GetMaxPositiveAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        int32 amount = (*i)->GetModifierValue();
        if (mod->m_miscvalue == misc_value && amount > modifier)
            modifier = amount;
    }

    return modifier;
}

int32 Unit::GetMaxNegativeAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for (AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        int32 amount = (*i)->GetModifierValue();
        if (mod->m_miscvalue == misc_value && amount < modifier)
            modifier = amount;
    }

    return modifier;
}

bool Unit::AddAura(Aura *Aur)
{
    if (Aur->GetTarget() != this)
    {
        sLog->outError("Aura (spell %u eff %u) add to aura list of %s (lowguid: %u) but Aura target is %s (lowguid: %u)",
            Aur->GetId(), Aur->GetEffIndex(), (GetTypeId() == TYPEID_PLAYER?"player":"creature"), GetGUIDLow(),
            (Aur->GetTarget()->GetTypeId() == TYPEID_PLAYER?"player":"creature"), Aur->GetTarget()->GetGUIDLow());
        delete Aur;
        return false;
    }

    SpellEntry const* aurSpellInfo = Aur->GetSpellProto();
    spellEffectPair spair = spellEffectPair(Aur->GetId(), Aur->GetEffIndex());

    bool stackModified = false;
    // passive and persistent auras can stack with themselves any number of times
    if (!Aur->IsPassive() && !Aur->IsPersistent())
    {
        for (AuraMap::iterator i2 = m_Auras.lower_bound(spair); i2 != m_Auras.upper_bound(spair);)
        {
            Aura* aur2 = i2->second;
            if (aur2->GetCasterGUID() == Aur->GetCasterGUID())
            {
                if (!stackModified)
                {
                    // replace aura if next will > spell StackAmount
                    if (aurSpellInfo->StackAmount)
                    {
                        // prevent adding stack more than once
                        stackModified=true;
                        Aur->SetStackAmount(aur2->GetStackAmount());
                        if (Aur->GetStackAmount() < aurSpellInfo->StackAmount)
                            Aur->SetStackAmount(Aur->GetStackAmount()+1);
                    }

                    // Allow mongoose procs from different weapon stacks
                    if (Aur->GetId() == 28093 || Aur->GetId() == 20007)
                    {
                        if (Aur->GetCastItemGUID() != i2->second->GetCastItemGUID())
                        {
                            i2++;
                            continue;
                        }
                    }
                    RemoveAura(i2, AURA_REMOVE_BY_STACK);
                    i2 = m_Auras.lower_bound(spair);
                    continue;
                }
            }
            else if (aur2->GetCasterGUID() != Aur->GetCasterGUID() && aurSpellInfo->StackAmount > 1 && aur2->GetSpellProto()->StackAmount > 1) // Auras belong to different casters, try merge into one stack (breaks non-stacking auras)
            {
                if (!stackModified) //A ura stacks such as scorch
                {
                    // replace aura if next will > spell StackAmount
                    if (aurSpellInfo->StackAmount)
                    {

                        if (Aur->GetId() == aur2->GetId())
                        {   
                                stackModified=true;
                                Aur->SetStackAmount(aur2->GetStackAmount());  
                                if (Aur->GetStackAmount() < aurSpellInfo->StackAmount) 
                                   Aur->SetStackAmount(Aur->GetStackAmount()+1);
                        }
                    }
                    Unit *caster = aur2->GetCaster();   // Need to update the original aura owner
                    RemoveAura(i2, AURA_REMOVE_BY_STACK);
                    i2 = m_Auras.lower_bound(spair);
                    
                    if (caster && caster->GetTypeId() == TYPEID_PLAYER)
                        Aur->SendAuraDurationForCaster((Player*)caster);    // The problem with this is it removes the duration tracking for the other player (is this retail like?)

                    continue;
                }
            }
            switch (aurSpellInfo->EffectApplyAuraName[Aur->GetEffIndex()])
            {
                // DOT or HOT from different casters will stack
                case SPELL_AURA_PERIODIC_DAMAGE:
                case SPELL_AURA_PERIODIC_HEAL:
                case SPELL_AURA_PERIODIC_TRIGGER_SPELL:
                case SPELL_AURA_PERIODIC_ENERGIZE:
                case SPELL_AURA_PERIODIC_MANA_LEECH:
                case SPELL_AURA_PERIODIC_LEECH:
                case SPELL_AURA_POWER_BURN_MANA:
                case SPELL_AURA_OBS_MOD_MANA:
                case SPELL_AURA_OBS_MOD_HEALTH:
                case SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE:  
                    ++i2;
                    continue;
            }
            if (((aurSpellInfo->SpellFamilyFlags & 0x800000LL) && aurSpellInfo->SpellIconID == 548) || (aurSpellInfo->Id == 35339))  // Special Case: Mindflay
            {
                ++i2;
                continue;
            }
            RemoveAura(i2, AURA_REMOVE_BY_STACK);
            i2=m_Auras.lower_bound(spair);
            continue;
        }
    }

    // passive auras stack with all (except passive spell proc auras)
    if ((!Aur->IsPassive() || !IsPassiveStackableSpell(Aur->GetId())) &&
        !(Aur->GetId() == 20584 || Aur->GetId() == 8326))
    {
        if (!RemoveNoStackAurasDueToAura(Aur))
        {
            delete Aur;
            return false;                                   // couldn't remove conflicting aura with higher rank
        }
    }

    // Calculate if aura should be applied to target - e.g. Frostbolt should deal damage but slow effect can resist
    if (Aur->GetTarget() && Aur->GetCaster())
    {
        SpellMissInfo auraMissInfo = Aur->GetCaster()->MagicAuraHitResult(Aur->GetTarget(), Aur->GetSpellProto());
        if (auraMissInfo != SPELL_MISS_NONE)
        {
            Aur->GetCaster()->SendSpellMiss(Aur->GetTarget(), Aur->GetId(), auraMissInfo);
            delete Aur;
            return false;
        }
    }

    // update single target auras list (before aura add to aura list, to prevent unexpected remove recently added aura)
    if (Aur->IsSingleTarget() && Aur->GetTarget())
    {
        // caster pointer can be deleted in time aura remove, find it by guid at each iteration
        for (;;)
        {
            Unit* caster = Aur->GetCaster();
            if (!caster)                                     // caster deleted and not required adding scAura
                break;

            bool restart = false;
            AuraList& scAuras = caster->GetSingleCastAuras();
            for (AuraList::iterator itr = scAuras.begin(); itr != scAuras.end(); ++itr)
            {
                if ((*itr)->GetTarget() != Aur->GetTarget() &&
                    IsSingleTargetSpells((*itr)->GetSpellProto(), aurSpellInfo))
                {
                    if ((*itr)->IsInUse())
                    {
                        sLog->outError("Aura (Spell %u Effect %u) is in process but attempt removed at aura (Spell %u Effect %u) adding, need add stack rule for IsSingleTargetSpell", (*itr)->GetId(), (*itr)->GetEffIndex(), Aur->GetId(), Aur->GetEffIndex());
                        continue;
                    }
                    (*itr)->GetTarget()->RemoveAura((*itr)->GetId(), (*itr)->GetEffIndex());
                    restart = true;
                    break;
                }
            }

            if (!restart)
            {
                // done
                scAuras.push_back(Aur);
                break;
            }
        }
    }

    // add aura, register in lists and arrays
    Aur->_AddAura();
    m_Auras.insert(AuraMap::value_type(spellEffectPair(Aur->GetId(), Aur->GetEffIndex()), Aur));
    if (Aur->GetModifier()->m_auraname < TOTAL_AURAS)
    {
        m_modAuras[Aur->GetModifier()->m_auraname].push_back(Aur);
        if (Aur->GetSpellProto()->AuraInterruptFlags)
        {
            m_interruptableAuras.push_back(Aur);
            AddInterruptMask(Aur->GetSpellProto()->AuraInterruptFlags);
        }
        if ((Aur->GetSpellProto()->Attributes & SPELL_ATTR_BREAKABLE_BY_DAMAGE)
            && (Aur->GetModifier()->m_auraname != SPELL_AURA_MOD_POSSESS)) //only dummy aura is breakable
        {
            m_ccAuras.push_back(Aur);
        }
    }

    Aur->ApplyModifier(true, true);

    uint32 id = Aur->GetId();
    if (sSpellMgr->GetSpellCustomAttr(id) & SPELL_ATTR_CU_LINK_AURA)
    {
        if (const std::vector<int32> *spell_triggered = sSpellMgr->GetSpellLinked(id + SPELL_LINK_AURA))
            for (std::vector<int32>::const_iterator itr = spell_triggered->begin(); itr != spell_triggered->end(); ++itr)
                if (*itr < 0)
                    ApplySpellImmune(id, IMMUNITY_ID, -(*itr), true);
                else if (Unit* caster = Aur->GetCaster())
                    caster->AddAura(*itr, this);
    }

    sLog->outDebug("Aura %u now is in use", Aur->GetModifier()->m_auraname);
    return true;
}

bool Unit::CheckNoStackAurasDueToSpell(SpellEntry const* SpellInfo, uint32 effIndex, uint64 CasterGuid)
{
    if (!SpellInfo)
        return false;

    uint32 spellId = SpellInfo->Id;
    AuraMap::iterator i, next;
    for (i = m_Auras.begin(); i != m_Auras.end(); i = next)
    {
        next = i;
        ++next;
        if (!(*i).second)
            continue;

        SpellEntry const* spellProto = (*i).second->GetSpellProto();
        if (!spellProto)
            continue;

        uint32 spellId = spellProto->Id;

        if (spellId == spellId)
            continue;

        uint32 i_effIndex = (*i).second->GetEffIndex();

        bool triggeredBySpell = false;
        // prevent triggered aura of removing aura that triggered it
        for (uint8 j = 0; j < MAX_SPELL_EFFECTS; ++j)
            if (spellProto->EffectTriggerSpell[j] == SpellInfo->Id)
                triggeredBySpell = true;
        if (triggeredBySpell)
            continue;

        for (uint8 j = 0; j < MAX_SPELL_EFFECTS; ++j)
        {
            // prevent remove dummy triggered spells at next effect aura add
            switch (SpellInfo->Effect[j])                   // main spell auras added added after triggered spell
            {
                case SPELL_EFFECT_DUMMY:
                    switch (spellId)
                    {
                        case 5420:
                            if (spellId == 34123)
                                triggeredBySpell = true;
                            break;
                    }
                    break;
            }

            if (triggeredBySpell)
                break;

            // prevent remove form main spell by triggered passive spells
            switch (spellProto->EffectApplyAuraName[j])    // main aura added before triggered spell
            {
                case SPELL_AURA_MOD_SHAPESHIFT:
                    switch (spellId)
                    {
                        case 24858:
                            if (spellId == 24905)
                                triggeredBySpell = true;
                            break;
                        case 33891:
                            if (spellId == 5420 || spellId == 34123)
                                triggeredBySpell = true;
                            break;
                        case 34551:
                            if (spellId == 22688)
                                triggeredBySpell = true;
                            break;
                            // stealth && shadowmeld should work together
                            // Shadowmeld: Stealth Rank 1-4
                        case 20508:
                            if (spellId == 1784 || spellId == 1785 || spellId == 1786 || spellId == 1787)
                                triggeredBySpell = true;
                            break;
                            // Stealth: Shadowmeld
                        case 1784:
                        case 1785:
                        case 1786:
                        case 1787:
                            if (spellId == 20508)
                                triggeredBySpell = true; break;
                    }
                    break;
            }
        }

        /*if (!triggeredBySpell)
        {
            bool sameCaster = CasterGuid == (*i).second->GetCasterGUID();
            Unit* caster = GetUnit(*this, CasterGuid);
            if (caster && sSpellMgr->IsNoStackSpellDueToSpell(spellId, spellId, sameCaster))
            {
                int32 aur1Rank = caster->CalculateSpellDamage(this, SpellInfo, effIndex);
                int32 aur2Rank = (*i).second->GetModifierValue();
                // Check if effect is "better"
                if (!sameCaster && (abs(aur1Rank) - abs(aur2Rank)) < 0)
                    return false;
                // TODO - store auras to be removed so we can safe some time
            }
        }*/
    }
    return true;
}


void Unit::RemoveRankAurasDueToSpell(uint32 spellId)
{
    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId);
    if (!spellInfo)
        return;
    AuraMap::iterator i, next;
    for (i = m_Auras.begin(); i != m_Auras.end(); i = next)
    {
        next = i;
        ++next;
        uint32 i_spellId = (*i).second->GetId();
        if ((*i).second && i_spellId && i_spellId != spellId)
        {
            if (sSpellMgr->IsRankSpellDueToSpell(spellInfo, i_spellId))
            {
                RemoveAurasDueToSpell(i_spellId);

                if (m_Auras.empty())
                    break;
                else
                    next =  m_Auras.begin();
            }
        }
    }
}

bool Unit::RemoveNoStackAurasDueToAura(Aura *Aur)
{
    if (!Aur)
        return false;

    SpellEntry const* spellProto = Aur->GetSpellProto();
    if (!spellProto)
        return false;

    uint32 spellId = Aur->GetId();
    uint32 effIndex = Aur->GetEffIndex();

    AuraMap::iterator i, next;
    for (i = m_Auras.begin(); i != m_Auras.end(); i = next)
    {
        next = i;
        ++next;
        if (!(*i).second) continue;

        SpellEntry const* i_spellProto = (*i).second->GetSpellProto();

        if (!i_spellProto)
            continue;

        uint32 i_spellId = i_spellProto->Id;

        if (spellId == i_spellId)   
            continue;

        if (IsPassiveSpell(i_spellId))
        {
            if (IsPassiveStackableSpell(i_spellId))
                continue;

            // passive non-stackable spells not stackable only with another rank of same spell
            if (!sSpellMgr->IsRankSpellDueToSpell(spellProto, i_spellId))
                continue;
        }

        uint32 i_effIndex = (*i).second->GetEffIndex();

        bool is_triggered_by_spell = false;
        // prevent triggered aura of removing aura that triggered it
        for (int j = 0; j < 3; ++j)
            if (i_spellProto->EffectTriggerSpell[j] == spellProto->Id)
                is_triggered_by_spell = true;
        if (is_triggered_by_spell) continue;

        for (int j = 0; j < 3; ++j)
        {
            // prevent remove dummy triggered spells at next effect aura add
            switch (spellProto->Effect[j])                   // main spell auras added added after triggered spell
            {
                case SPELL_EFFECT_DUMMY:
                    switch (spellId)
                    {
                        case 5420: if (i_spellId == 34123) is_triggered_by_spell = true; break;
                    }
                    break;
            }

            if (is_triggered_by_spell)
                break;

            // prevent remove form main spell by triggered passive spells
            switch (i_spellProto->EffectApplyAuraName[j])    // main aura added before triggered spell
            {
                case SPELL_AURA_MOD_SHAPESHIFT:
                    switch (i_spellId)
                    {
                        case 24858: if (spellId == 24905)                  is_triggered_by_spell = true; break;
                        case 33891: if (spellId == 5420 || spellId == 34123) is_triggered_by_spell = true; break;
                        case 34551: if (spellId == 22688)                  is_triggered_by_spell = true; break;
                    }
                    break;
            }
        }

        if (!is_triggered_by_spell)
        {
            bool sameCaster = Aur->GetCasterGUID() == (*i).second->GetCasterGUID();
            if (sSpellMgr->IsNoStackSpellDueToSpell(spellId, i_spellId, sameCaster))
            {
                int32 aur1Rank = Aur->GetModifierValue();
                int32 aur2Rank = (*i).second->GetModifierValue();
                // Check if effect is "better"
                if (!sameCaster && (abs(aur1Rank)-abs(aur2Rank)) < 0)
                    return false;
                // Some spells should be not removed by lower rank of them (totem, paladin aura)
                if (!sameCaster
                    &&(spellProto->Effect[effIndex] == SPELL_EFFECT_APPLY_AREA_AURA_PARTY)
                    &&(spellProto->DurationIndex == 21)
                    &&(sSpellMgr->IsRankSpellDueToSpell(spellProto, i_spellId))
                    &&(CompareAuraRanks(spellId, effIndex, i_spellId, i_effIndex) < 0))
                    return false;

                // Its a parent aura (create this aura in ApplyModifier)
                if ((*i).second->IsInUse())
                {
                    sLog->outError("Aura (Spell %u Effect %u) is in process but attempt removed at aura (Spell %u Effect %u) adding, need add stack rule for Unit::RemoveNoStackAurasDueToAura", i->second->GetId(), i->second->GetEffIndex(), Aur->GetId(), Aur->GetEffIndex());
                    continue;
                }

            uint64 caster = (*i).second->GetCasterGUID();
            // Remove all auras by aura caster
            for (uint8 a=0;a<3;++a)
            {
                spellEffectPair spair = spellEffectPair(i_spellId, a);
                for (AuraMap::iterator iter = m_Auras.lower_bound(spair); iter != m_Auras.upper_bound(spair);)
                {
                    if (iter->second->GetCasterGUID() == caster)
                    {
                        RemoveAura(iter, AURA_REMOVE_BY_STACK);
                        iter = m_Auras.lower_bound(spair);
                    }
                    else
                        ++iter;
                }
            }

                if (m_Auras.empty())
                    break;
                else
                    next =  m_Auras.begin();
            }
        }
    }
    return true;
}

void Unit::RemoveAura(uint32 spellId, uint32 effindex, Aura* except)
{
    spellEffectPair spair = spellEffectPair(spellId, effindex);
    for (AuraMap::iterator iter = m_Auras.lower_bound(spair); iter != m_Auras.upper_bound(spair);)
    {
        if (iter->second != except)
        {
            RemoveAura(iter);
            iter = m_Auras.lower_bound(spair);
        }
        else
            ++iter;
    }
}

void Unit::RemoveAurasByCasterSpell(uint32 spellId, uint64 casterGUID)
{
    for (int k = 0; k < 3; ++k)
    {
        spellEffectPair spair = spellEffectPair(spellId, k);
        for (AuraMap::iterator iter = m_Auras.lower_bound(spair); iter != m_Auras.upper_bound(spair);)
        {
            if (iter->second->GetCasterGUID() == casterGUID)
            {
                RemoveAura(iter);
                iter = m_Auras.upper_bound(spair);          // overwrite by more appropriate
            }
            else
                ++iter;
        }
    }
}

void Unit::SetAurasDurationByCasterSpell(uint32 spellId, uint64 casterGUID, int32 duration)
{
    for (uint8 i = 0; i < 3; ++i)
    {
        spellEffectPair spair = spellEffectPair(spellId, i);
        for (AuraMap::const_iterator itr = GetAuras().lower_bound(spair); itr != GetAuras().upper_bound(spair); ++itr)
        {
            if (itr->second->GetCasterGUID() == casterGUID)
            {
                itr->second->SetAuraDuration(duration);
                break;
            }
        }
    }
}

Aura* Unit::GetAuraByCasterSpell(uint32 spellId, uint64 casterGUID)
{
    // Returns first found aura from spell-use only in cases where effindex of spell doesn't matter!
    for (uint8 i = 0; i < 3; ++i)
    {
        spellEffectPair spair = spellEffectPair(spellId, i);
        for (AuraMap::const_iterator itr = GetAuras().lower_bound(spair); itr != GetAuras().upper_bound(spair); ++itr)
        {
            if (itr->second->GetCasterGUID() == casterGUID)
                return itr->second;
        }
    }
    return NULL;
}

void Unit::RemoveAurasDueToSpellByDispel(uint32 spellId, uint64 casterGUID, Unit *dispeler)
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        Aura *aur = iter->second;
        if (aur->GetId() == spellId && aur->GetCasterGUID() == casterGUID)
        {
            // Custom dispel case
            // Unstable Affliction
            if (aur->GetSpellProto()->SpellFamilyName == SPELLFAMILY_WARLOCK && (aur->GetSpellProto()->SpellFamilyFlags & 0x010000000000LL))
            {
                int32 damage = aur->GetModifier()->m_amount * 9;
                uint64 caster_guid = aur->GetCasterGUID();

                // Remove aura
                RemoveAura(iter, AURA_REMOVE_BY_DISPEL);

                // backfire damage and silence
                dispeler->CastCustomSpell(dispeler, 31117, &damage, NULL, NULL, true, NULL, NULL, caster_guid);

                iter = m_Auras.begin();                     // iterator can be invalidate at cast if self-dispel
            }
            else
                RemoveAura(iter, AURA_REMOVE_BY_DISPEL);
        }
        else
            ++iter;
    }
}

void Unit::RemoveAurasDueToSpellBySteal(uint32 spellId, uint64 casterGUID, Unit *stealer)
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        Aura *aur = iter->second;
        if (aur->GetId() == spellId && aur->GetCasterGUID() == casterGUID)
        {
            int32 basePoints = aur->GetBasePoints();
            // construct the new aura for the attacker
            Aura * new_aur = CreateAura(aur->GetSpellProto(), aur->GetEffIndex(), NULL/*&basePoints*/, stealer);
            if (!new_aur)
                continue;

            // set its duration and maximum duration
            // max duration 2 minutes (in msecs)
            int32 dur = aur->GetAuraDuration();
            const int32 max_dur = 2*MINUTE*IN_MILLISECONDS;
            new_aur->SetAuraMaxDuration(max_dur > dur ? dur : max_dur);
            new_aur->SetAuraDuration(max_dur > dur ? dur : max_dur);

            // Unregister _before_ adding to stealer
            aur->UnregisterSingleCastAura();
            // strange but intended behaviour: Stolen single target auras won't be treated as single targeted
            new_aur->SetIsSingleTarget(false);
            // add the new aura to stealer
            stealer->AddAura(new_aur);
            // add pre cast aura at aura apply
            stealer->ApplyPreCastSpell(stealer, aur->GetSpellProto());
            // Remove aura as dispel
           if (iter->second->GetStackAmount() > 1)
            {
                // reapply modifier with reduced stack amount
                iter->second->ApplyModifier(false, true);
                iter->second->SetStackAmount(iter->second->GetStackAmount() - 1);
                iter->second->ApplyModifier(true, true);

                iter->second->UpdateSlotCounter();
                ++iter;
            }
            else
                RemoveAura(iter, AURA_REMOVE_BY_DISPEL);
        }
        else
            ++iter;
    }
}

void Unit::RemoveAurasDueToSpellByCancel(uint32 spellId)
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        if (iter->second->GetId() == spellId)
            RemoveAura(iter, AURA_REMOVE_BY_CANCEL);
        else
            ++iter;
    }
}

void Unit::RemoveAurasWithAttribute(uint32 flags)
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        SpellEntry const *spell = iter->second->GetSpellProto();
        if (spell->Attributes & flags)
            RemoveAura(iter);
        else
            ++iter;
    }
}

void Unit::RemoveAurasWithDispelType(DispelType type)
{
    // Create dispel mask by dispel type
    uint32 dispelMask = GetDispellMask(type);
    // Dispel all existing auras vs current dispel type
    AuraMap& auras = GetAuras();
    for (AuraMap::iterator itr = auras.begin(); itr != auras.end();)
    {
        SpellEntry const* spell = itr->second->GetSpellProto();
        if ((1<<spell->Dispel) & dispelMask)
        {
            // Dispel aura
            RemoveAurasDueToSpell(spell->Id);
            itr = auras.begin();
        }
        else
            ++itr;
    }
}

void Unit::RemoveSingleAuraFromStackByDispel(uint32 spellId)
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        Aura *aur = iter->second;
        if (aur->GetId() == spellId)
        {
            if (iter->second->GetStackAmount() > 1)
            {
                // reapply modifier with reduced stack amount
                iter->second->ApplyModifier(false, true);
                iter->second->SetStackAmount(iter->second->GetStackAmount()-1);
                iter->second->ApplyModifier(true, true);

                iter->second->UpdateSlotCounter();
                return; // not remove aura if stack amount > 1
            }
            else
                RemoveAura(iter, AURA_REMOVE_BY_DISPEL);
        }
        else
            ++iter;
    }
}

void Unit::RemoveSingleAuraFromStack(uint32 spellId, uint32 effindex)
{
    AuraMap::iterator iter = m_Auras.find(spellEffectPair(spellId, effindex));
    if (iter != m_Auras.end())
    {
        if (iter->second->GetStackAmount() > 1)
        {
            // reapply modifier with reduced stack amount
            iter->second->ApplyModifier(false, true);
            iter->second->SetStackAmount(iter->second->GetStackAmount()-1);
            iter->second->ApplyModifier(true, true);

            iter->second->UpdateSlotCounter();
            return; // not remove aura if stack amount > 1
        }
        RemoveAura(iter);
    }
}

void Unit::RemoveAurasDueToSpell(uint32 spellId, Aura* except)
{
    for (int i = 0; i < 3; ++i)
        RemoveAura(spellId, i, except);
}

void Unit::RemoveAurasDueToItemSpell(Item* castItem, uint32 spellId)
{
    for (int k=0; k < 3; ++k)
    {
        spellEffectPair spair = spellEffectPair(spellId, k);
        for (AuraMap::iterator iter = m_Auras.lower_bound(spair); iter != m_Auras.upper_bound(spair);)
        {
            if (iter->second->GetCastItemGUID() == castItem->GetGUID())
            {
                RemoveAura(iter);
                iter = m_Auras.upper_bound(spair);          // overwrite by more appropriate
            }
            else
                ++iter;
        }
    }
}

void Unit::RemoveNotOwnSingleTargetAuras()
{
    // single target auras from other casters
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        if (iter->second->GetCasterGUID() != GetGUID() && IsSingleTargetSpell(iter->second->GetSpellProto()))
            RemoveAura(iter);
        else
            ++iter;
    }

    // single target auras at other targets
    AuraList& scAuras = GetSingleCastAuras();
    for (AuraList::iterator iter = scAuras.begin(); iter != scAuras.end();)
    {
        Aura* aur = *iter;
        ++iter;
        if (aur->GetTarget() != this)
        {
            uint32 removedAuras = m_removedAurasCount;
            aur->GetTarget()->RemoveAura(aur->GetId(), aur->GetEffIndex());
            if (m_removedAurasCount > removedAuras + 1)
                iter = scAuras.begin();
        }
    }
}

void Unit::RemoveAllGroupBuffsFromCaster(uint64 casterGUID)
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        SpellEntry const *spell = iter->second->GetSpellProto();
        if (IsGroupBuff(spell))
            RemoveAura(iter);
        else
            ++iter;
    }
}

void Unit::RemoveAura(AuraMap::iterator &i, AuraRemoveMode mode)
{
    Aura* Aur = i->second;

    // if unit currently update aura list then make safe update iterator shift to next
    if (m_AurasUpdateIterator == i)
        ++m_AurasUpdateIterator;

    // some ShapeshiftBoosts at remove trigger removing other auras including parent Shapeshift aura
    // remove aura from list before to prevent deleting it before
    m_Auras.erase(i);
    ++m_removedAurasCount;

    SpellEntry const* AurSpellInfo = Aur->GetSpellProto();
    Unit* caster = NULL;
    Aur->UnregisterSingleCastAura();

    // remove from list before mods removing (prevent cyclic calls, mods added before including to aura list - use reverse order)
    if (Aur->GetModifier()->m_auraname < TOTAL_AURAS)
    {
        m_modAuras[Aur->GetModifier()->m_auraname].remove(Aur);

        if (Aur->GetSpellProto()->AuraInterruptFlags)
        {
            m_interruptableAuras.remove(Aur);
            UpdateInterruptMask();
        }

        if ((Aur->GetSpellProto()->Attributes & SPELL_ATTR_BREAKABLE_BY_DAMAGE)
            && (Aur->GetModifier()->m_auraname != SPELL_AURA_MOD_POSSESS)) //only dummy aura is breakable
        {
            m_ccAuras.remove(Aur);
        }
    }

    // Set remove mode
    Aur->SetRemoveMode(mode);

    // Statue unsummoned at aura remove
    //Totem* statue = NULL;
    bool channeled = false;
    if (Aur->GetAuraDuration() && !Aur->IsPersistent() && IsChanneledSpell(AurSpellInfo))
    {
        if (!caster)                                         // can be already located for IsSingleTargetSpell case
            caster = Aur->GetCaster();

        if (caster && caster->isAlive())
        {
            // stop caster chanelling state
            if (caster->m_currentSpells[CURRENT_CHANNELED_SPELL]
                //prevent recurential call
                && caster->m_currentSpells[CURRENT_CHANNELED_SPELL]->getState() != SPELL_STATE_FINISHED)
            {
                if (caster == this || !IsAreaOfEffectSpell(AurSpellInfo))
                {
                    // remove auras only for non-aoe spells or when chanelled aura is removed
                    // because aoe spells don't require aura on target to continue
                    if (AurSpellInfo->EffectApplyAuraName[Aur->GetEffIndex()] != SPELL_AURA_PERIODIC_DUMMY
                        && AurSpellInfo->EffectApplyAuraName[Aur->GetEffIndex()] != SPELL_AURA_DUMMY)
                        //don't stop channeling of scripted spells (this is actually a hack)
                    {
                        caster->m_currentSpells[CURRENT_CHANNELED_SPELL]->cancel();
                        caster->m_currentSpells[CURRENT_CHANNELED_SPELL]=NULL;
                    }
                }

                //if (caster->GetTypeId() == TYPEID_UNIT && caster->ToCreature()->isTotem() && ((Totem*)caster)->GetTotemType() == TOTEM_STATUE)
                //    statue = ((Totem*)caster);
            }

            // Unsummon summon as possessed creatures on spell cancel
            if (caster->GetTypeId() == TYPEID_PLAYER)
                if (Unit *charm = caster->GetCharm())
                    if (charm->GetTypeId() == TYPEID_UNIT
                        && charm->ToCreature()->HasSummonMask(SUMMON_MASK_PUPPET)
                        && charm->GetUInt32Value(UNIT_CREATED_BY_SPELL) == AurSpellInfo->Id)
                        ((Puppet*)charm)->UnSummon();
        }
    }

    sLog->outDebug("Aura %u (%u) now is remove mode %d", Aur->GetId(), Aur->GetModifier()->m_auraname, mode);
    ASSERT(!Aur->IsInUse());
    Aur->ApplyModifier(false, true);

    Unit* auraTarget = Aur->GetTarget();
    Unit* auraCaster = Aur->GetCaster();

    if (auraTarget && auraTarget->GetTypeId() == TYPEID_PLAYER && auraCaster && auraCaster->GetTypeId() == TYPEID_PLAYER)
        ((Player*)Aur->GetTarget())->SendArenaSpectatorAura(true, Aur->GetStackAmount(), Aur->GetAuraDuration(), Aur->GetAuraMaxDuration(), 
            Aur->GetId(), Aur->IsPeriodic(), Aur->IsPositive(), ((Player*)Aur->GetCaster())->GetGUID());

    Aur->SetStackAmount(0);

    // set aura to be removed during unit::_updatespells
    m_removedAuras.push_back(Aur);

    Aur->_RemoveAura();

    bool stack = false;
    spellEffectPair spair = spellEffectPair(Aur->GetId(), Aur->GetEffIndex());
    for (AuraMap::const_iterator itr = GetAuras().lower_bound(spair); itr != GetAuras().upper_bound(spair); ++itr)
    {
        if (itr->second->GetCasterGUID() == GetGUID())
        {
            stack = true;
        }
    }
    if (!stack)
    {
        // Remove all triggered by aura spells vs unlimited duration
        Aur->CleanupTriggeredSpells();

        // Remove Linked Auras
        uint32 id = Aur->GetId();
        if (sSpellMgr->GetSpellCustomAttr(id) & SPELL_ATTR_CU_LINK_REMOVE)
        {
            if (const std::vector<int32> *spell_triggered = sSpellMgr->GetSpellLinked(-(int32)id))
                for (std::vector<int32>::const_iterator itr = spell_triggered->begin(); itr != spell_triggered->end(); ++itr)
                    if (*itr < 0)
                        RemoveAurasDueToSpell(-(*itr));
                    else if (Unit* caster = Aur->GetCaster())
                        CastSpell(this, *itr, true, 0, 0, caster->GetGUID());
        }
        if (sSpellMgr->GetSpellCustomAttr(id) & SPELL_ATTR_CU_LINK_AURA)
        {
            if (const std::vector<int32> *spell_triggered = sSpellMgr->GetSpellLinked(id + SPELL_LINK_AURA))
                for (std::vector<int32>::const_iterator itr = spell_triggered->begin(); itr != spell_triggered->end(); ++itr)
                    if (*itr < 0)
                        ApplySpellImmune(id, IMMUNITY_ID, -(*itr), false);
                    else
                        RemoveAurasDueToSpell(*itr);
        }
    }

    //if (statue)
    //    statue->UnSummon();

    i = m_Auras.begin();
}

void Unit::RemoveAllAuras()
{
    while (!m_Auras.empty())
    {
        AuraMap::iterator iter = m_Auras.begin();
        RemoveAura(iter);
    }
}

void Unit::RemoveAllAurasButPermanent()
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        SpellEntry const *spell = iter->second->GetSpellProto();
        int32 duration = GetSpellDuration(spell);
        if (duration > 0)
            RemoveAura(iter);
        else
            ++iter;
    }
}

void Unit::RemoveArenaAuras(bool onleave)
{
    // in join, remove positive buffs, on end, remove negative
    // used to remove positive visible auras in arenas
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        if ((!(iter->second->GetSpellProto()->AttributesEx4 & (1<<21)) &&
                                                            // don't remove stances, shadowform, pally/hunter auras
            !iter->second->IsPassive() &&                   // don't remove passive auras
            (!(iter->second->GetSpellProto()->Attributes & SPELL_ATTR_UNAFFECTED_BY_INVULNERABILITY) ||
            !(iter->second->GetSpellProto()->Attributes & SPELL_ATTR_HIDE_IN_COMBAT_LOG)) &&
                                                            // not unaffected by invulnerability auras or not having that unknown flag (that seemed the most probable)
            (iter->second->IsPositive() ^ onleave) || iter->second->GetId() == 28509))         // remove positive buffs on enter, negative buffs on leave
            RemoveAura(iter);
        else
            ++iter;
    }
}

void Unit::RemoveAllAurasOnDeath()
{
    // used just after dieing to remove all visible auras
    // and disable the mods for the passive ones
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        if (!iter->second->IsPassive() && !iter->second->IsDeathPersistent())
            RemoveAura(iter, AURA_REMOVE_BY_DEATH);
        else
            ++iter;
    }
}

void Unit::DelayAura(uint32 spellId, uint32 effindex, int32 delaytime)
{
    AuraMap::iterator iter = m_Auras.find(spellEffectPair(spellId, effindex));
    if (iter != m_Auras.end())
    {
        if (iter->second->GetAuraDuration() < delaytime)
            iter->second->SetAuraDuration(0);
        else
            iter->second->SetAuraDuration(iter->second->GetAuraDuration() - delaytime);
        iter->second->UpdateAuraDuration();
        sLog->outDebug("Aura %u partially interrupted on unit %u, new duration: %u ms", iter->second->GetModifier()->m_auraname, GetGUIDLow(), iter->second->GetAuraDuration());
    }
}

void Unit::_RemoveAllAuraMods()
{
    for (AuraMap::iterator i = m_Auras.begin(); i != m_Auras.end(); ++i)
    {
        (*i).second->ApplyModifier(false);
    }
}

void Unit::_ApplyAllAuraMods()
{
    for (AuraMap::iterator i = m_Auras.begin(); i != m_Auras.end(); ++i)
    {
        (*i).second->ApplyModifier(true);
    }
}

Aura* Unit::GetAura(uint32 spellId, uint32 effindex)
{
    AuraMap::iterator iter = m_Auras.find(spellEffectPair(spellId, effindex));
    if (iter != m_Auras.end())
        return iter->second;
    return NULL;
}

void Unit::AddDynObject(DynamicObject* dynObj)
{
    m_dynObjGUIDs.push_back(dynObj->GetGUID());
}

void Unit::RemoveDynObject(uint32 spellid)
{
    if (m_dynObjGUIDs.empty())
        return;
    for (DynObjectGUIDs::iterator i = m_dynObjGUIDs.begin(); i != m_dynObjGUIDs.end();)
    {
        DynamicObject* dynObj = GetMap()->GetDynamicObject(*i);
        if (!dynObj) // may happen if a dynobj is removed when grid unload
        {
            i = m_dynObjGUIDs.erase(i);
        }
        else if (spellid == 0 || dynObj->GetSpellId() == spellid)
        {
            dynObj->Delete();
            i = m_dynObjGUIDs.erase(i);
        }
        else
            ++i;
    }
}

void Unit::RemoveAllDynObjects()
{
    while (!m_dynObjGUIDs.empty())
    {
        DynamicObject* dynObj = GetMap()->GetDynamicObject(*m_dynObjGUIDs.begin());
        if (dynObj)
            dynObj->Delete();
        m_dynObjGUIDs.erase(m_dynObjGUIDs.begin());
    }
}

DynamicObject * Unit::GetDynObject(uint32 spellId, uint32 effIndex)
{
    for (DynObjectGUIDs::iterator i = m_dynObjGUIDs.begin(); i != m_dynObjGUIDs.end();)
    {
        DynamicObject* dynObj = GetMap()->GetDynamicObject(*i);
        if (!dynObj)
        {
            i = m_dynObjGUIDs.erase(i);
            continue;
        }

        if (dynObj->GetSpellId() == spellId && dynObj->GetEffIndex() == effIndex)
            return dynObj;
        ++i;
    }
    return NULL;
}

DynamicObject * Unit::GetDynObject(uint32 spellId)
{
    for (DynObjectGUIDs::iterator i = m_dynObjGUIDs.begin(); i != m_dynObjGUIDs.end();)
    {
        DynamicObject* dynObj = GetMap()->GetDynamicObject(*i);
        if (!dynObj)
        {
            i = m_dynObjGUIDs.erase(i);
            continue;
        }

        if (dynObj->GetSpellId() == spellId)
            return dynObj;
        ++i;
    }
    return NULL;
}

void Unit::AddGameObject(GameObject* gameObj)
{
    if (!gameObj || !gameObj->GetOwnerGUID() == 0) return;
    m_gameObj.push_back(gameObj);
    gameObj->SetOwnerGUID(GetGUID());
}

void Unit::RemoveGameObject(GameObject* gameObj, bool del)
{
    if (!gameObj || !gameObj->GetOwnerGUID() == GetGUID()) return;

    gameObj->SetOwnerGUID(0);

    // GO created by some spell
    if (uint32 spellid = gameObj->GetSpellId())
    {
        RemoveAurasDueToSpell(spellid);

        if (GetTypeId() == TYPEID_PLAYER)
        {
            SpellEntry const* createBySpell = sSpellStore.LookupEntry(spellid);
            // Need activate spell use for owner
            if (createBySpell && createBySpell->Attributes & SPELL_ATTR_DISABLED_WHILE_ACTIVE)
                // note: item based cooldowns and cooldown spell mods with charges ignored (unknown existed cases)
                ToPlayer()->SendCooldownEvent(createBySpell);
        }
    }

    m_gameObj.remove(gameObj);

    if (del)
    {
        gameObj->SetRespawnTime(0);
        gameObj->Delete();
    }
}

void Unit::RemoveGameObject(uint32 spellid, bool del)
{
    if (m_gameObj.empty())
        return;
    std::list<GameObject*>::iterator i, next;
    for (i = m_gameObj.begin(); i != m_gameObj.end(); i = next)
    {
        next = i;
        if (spellid == 0 || (*i)->GetSpellId() == spellid)
        {
            (*i)->SetOwnerGUID(0);
            if (del)
            {
                (*i)->SetRespawnTime(0);
                (*i)->Delete();
            }

            next = m_gameObj.erase(i);
        }
        else
            ++next;
    }
}

void Unit::RemoveAllGameObjects()
{
    // remove references to unit
    for (std::list<GameObject*>::iterator i = m_gameObj.begin(); i != m_gameObj.end();)
    {
        (*i)->SetOwnerGUID(0);
        (*i)->SetRespawnTime(0);
        (*i)->Delete();
        i = m_gameObj.erase(i);
    }
}

void Unit::SendSpellNonMeleeDamageLog(SpellNonMeleeDamage *log)
{
    WorldPacket data(SMSG_SPELLNONMELEEDAMAGELOG, (16+4+4+1+4+4+1+1+4+4+1)); // we guess size
    data << log->target->GetPackGUID();
    data << log->attacker->GetPackGUID();
    data << uint32(log->SpellID);
    data << uint32(log->damage);                            // damage amount
    data << uint8 (log->schoolMask);                        // damage school
    data << uint32(log->absorb);                            // AbsorbedDamage
    data << uint32(log->resist);                            // resist
    data << uint8 (log->physicalLog);                       // damsge type? flag
    data << uint8 (log->unused);                            // unused
    data << uint32(log->blocked);                           // blocked
    data << uint32(log->HitInfo);
    data << uint8 (0);                                      // flag to use extend data
    SendMessageToSet(&data, true);
}

void Unit::SendSpellNonMeleeDamageLog(Unit *target, uint32 SpellID, uint32 Damage, SpellSchoolMask damageSchoolMask, uint32 AbsorbedDamage, uint32 Resist, bool PhysicalDamage, uint32 Blocked, bool CriticalHit)
{
    sLog->outDebug("Sending: SMSG_SPELLNONMELEEDAMAGELOG");
    WorldPacket data(SMSG_SPELLNONMELEEDAMAGELOG, (16+4+4+1+4+4+1+1+4+4+1)); // we guess size
    data << target->GetPackGUID();
    data << GetPackGUID();
    data << uint32(SpellID);
    data << uint32(Damage-AbsorbedDamage-Resist-Blocked);
    data << uint8(damageSchoolMask);                        // spell school
    data << uint32(AbsorbedDamage);                         // AbsorbedDamage
    data << uint32(Resist);                                 // resist
    data << uint8(PhysicalDamage);                          // if 1, then client show spell name (example: %s's ranged shot hit %s for %u school or %s suffers %u school damage from %s's spell_name
    data << uint8(0);                                       // unk isFromAura
    data << uint32(Blocked);                                // blocked
    data << uint32(CriticalHit ? 0x27 : 0x25);              // hitType, flags: 0x2 - SPELL_HIT_TYPE_CRIT, 0x10 - replace caster?
    data << uint8(0);                                       // isDebug?
    SendMessageToSet(&data, true);
}

void Unit::ProcDamageAndSpell(Unit *pVictim, uint32 procAttacker, uint32 procVictim, uint32 procExtra, uint32 amount, WeaponAttackType attType, SpellEntry const *procSpell, bool canTrigger)
{
     // Not much to do if no flags are set.
    if (procAttacker && canTrigger)
        ProcDamageAndSpellFor(false, pVictim, procAttacker, procExtra, attType, procSpell, amount);
    // Now go on with a victim's events'n'auras
    // Not much to do if no flags are set or there is no victim
    if (pVictim && pVictim->isAlive() && procVictim)
        pVictim->ProcDamageAndSpellFor(true, this, procVictim, procExtra, attType, procSpell, amount);
}

void Unit::SendSpellMiss(Unit *target, uint32 spellID, SpellMissInfo missInfo)
{
    WorldPacket data(SMSG_SPELLLOGMISS, (4+8+1+4+8+1));
    data << uint32(spellID);
    data << uint64(GetGUID());
    data << uint8(0);                                       // can be 0 or 1
    data << uint32(1);                                      // target count
    // for (i = 0; i < target count; ++i)
    data << uint64(target->GetGUID());                      // target GUID
    data << uint8(missInfo);
    // end loop
    SendMessageToSet(&data, true);
}

void Unit::SendAttackStateUpdate(CalcDamageInfo *damageInfo)
{
    WorldPacket data(SMSG_ATTACKERSTATEUPDATE, (16+84));    // we guess size
    data << (uint32)damageInfo->HitInfo;
    data << GetPackGUID();
    data << damageInfo->target->GetPackGUID();
    data << (uint32)(damageInfo->damage);     // Full damage

    data << (uint8)1;                         // Sub damage count
    //=== Sub damage description
    data << (uint32)(damageInfo->damageSchoolMask); // School of sub damage
    data << (float)damageInfo->damage;        // sub damage
    data << (uint32)damageInfo->damage;       // Sub Damage
    data << (uint32)damageInfo->absorb;       // Absorb
    data << (uint32)damageInfo->resist;       // Resist
    //=================================================
    data << (uint32)damageInfo->TargetState;
    data << (uint32)0;
    data << (uint32)0;
    data << (uint32)damageInfo->blocked_amount;
    SendMessageToSet(&data, true);/**/
}

void Unit::SendAttackStateUpdate(uint32 HitInfo, Unit *target, uint8 SwingType, SpellSchoolMask damageSchoolMask, uint32 Damage, uint32 AbsorbDamage, uint32 Resist, VictimState TargetState, uint32 BlockedAmount)
{
    sLog->outDebug("WORLD: Sending SMSG_ATTACKERSTATEUPDATE");

    WorldPacket data(SMSG_ATTACKERSTATEUPDATE, (16+45));    // we guess size
    data << (uint32)HitInfo;
    data << GetPackGUID();
    data << target->GetPackGUID();
    data << (uint32)(Damage-AbsorbDamage-Resist-BlockedAmount);

    data << (uint8)SwingType;                               // count?

    // for (i = 0; i < SwingType; ++i)
    data << (uint32)damageSchoolMask;
    data << (float)(Damage-AbsorbDamage-Resist-BlockedAmount);
    // still need to double check damage
    data << (uint32)(Damage-AbsorbDamage-Resist-BlockedAmount);
    data << (uint32)AbsorbDamage;
    data << (uint32)Resist;
    // end loop

    data << (uint32)TargetState;

    if (AbsorbDamage == 0)                                 //also 0x3E8 = 0x3E8, check when that happens
        data << (uint32)0;
    else
        data << (uint32)-1;

    data << (uint32)0;
    data << (uint32)BlockedAmount;

    SendMessageToSet(&data, true);
}

bool Unit::HandleHasteAuraProc(Unit *pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const * procSpell, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 cooldown)
{
    SpellEntry const *hasteSpell = triggeredByAura->GetSpellProto();

    Item* castItem = triggeredByAura->GetCastItemGUID() && GetTypeId() == TYPEID_PLAYER
        ? ToPlayer()->GetItemByGuid(triggeredByAura->GetCastItemGUID()) : NULL;

    uint32 triggered_spell_id = 0;
    Unit* target = pVictim;
    int32 basepoints0 = 0;

    switch (hasteSpell->SpellFamilyName)
    {
        case SPELLFAMILY_ROGUE:
        {
            switch (hasteSpell->Id)
            {
                // Blade Flurry
                case 13877:
                case 33735:
                {
                    target = SelectNearbyTarget();
                    if (!target || target == pVictim)
                        return false;
                    basepoints0 = damage;
                    triggered_spell_id = 22482;
                    break;
                }
            }
            break;
        }
    }

    // processed charge only counting case
    if (!triggered_spell_id)
        return true;

    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(triggered_spell_id);

    if (!triggerEntry)
    {
        sLog->outError("Unit::HandleHasteAuraProc: Spell %u has invalid triggered spell %u", hasteSpell->Id, triggered_spell_id);
        return false;
    }

    // default case
    if (!target || target != this && !target->isAlive())
        return false;

    if (cooldown && GetTypeId() == TYPEID_PLAYER && ToPlayer()->HasSpellCooldown(triggered_spell_id))
        return false;

    if (basepoints0)
        CastCustomSpell(target, triggered_spell_id, &basepoints0, NULL, NULL, true, castItem, triggeredByAura);
    else
        CastSpell(target, triggered_spell_id, true, castItem, triggeredByAura);

    if (cooldown && GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->AddSpellCooldown(triggered_spell_id, 0, time(NULL) + cooldown);

    return true;
}

bool Unit::HandleDummyAuraProc(Unit *pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const * procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown)
{
    SpellEntry const *dummySpell = triggeredByAura->GetSpellProto();
    uint32 effIndex = triggeredByAura->GetEffIndex();

    Item* castItem = triggeredByAura->GetCastItemGUID() && GetTypeId() == TYPEID_PLAYER
        ? ToPlayer()->GetItemByGuid(triggeredByAura->GetCastItemGUID()) : NULL;

    uint32 triggered_spell_id = 0;
    Unit* target = pVictim;
    int32 basepoints0 = 0;
    uint64 originalCaster = 0;

    switch (dummySpell->SpellFamilyName)
    {
        case SPELLFAMILY_GENERIC:
        {
            switch (dummySpell->Id)
            {
                // Eye for an Eye
                case 9799:
                case 25988:
                {
                    // prevent damage back from weapon special attacks
                    if (!procSpell || procSpell->DmgClass != SPELL_DAMAGE_CLASS_MAGIC)
                        return false;

                    // return damage % to attacker but < 50% own total health
                    basepoints0 = triggeredByAura->GetModifier()->m_amount*int32(damage)/100;
                    if (basepoints0 > GetMaxHealth()/2)
                        basepoints0 = GetMaxHealth()/2;

                    triggered_spell_id = 25997;
                    break;
                }
                // Sweeping Strikes
                case 12328:
                case 18765:
                case 35429:
                {
                    // prevent chain of triggered spell from same triggered spell
                    if (procSpell && (procSpell->Id == 26654 || procSpell->Id == 12723))
                        return false;

                    target = SelectNearbyTarget(8.0f);
                    if (!target)
                        return false;

                    triggered_spell_id = 12723;

                    if (procSpell)
                    {
                        // execute : normalized weapon damage if target > 20% health
                        if (procSpell->Id == 20647 && target->GetHealth() > target->GetMaxHealth() * 0.2f)
                        {
                            damage = CalculateDamage(BASE_ATTACK, false);
                            damage = MeleeDamageBonusDone(target, damage, BASE_ATTACK);
                            basepoints0 = damage;
                            break;
                        }

                        // Limit Whirlwind to hit one target applying 1s cooldown
                        if (procSpell->SpellFamilyName == SPELLFAMILY_WARRIOR && procSpell->SpellFamilyFlags & 0x400400000LL)
                            cooldown = 1;
                    }

                    float armor = pVictim->GetArmor();
                    // Ignore enemy armor by SPELL_AURA_MOD_TARGET_RESISTANCE aura
                    armor += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_TARGET_RESISTANCE, SPELL_SCHOOL_MASK_NORMAL);

                    if (armor < 0.0f)
                        armor = 0.0f;

                    float levelModifier = getLevel();
                    if (levelModifier > 59)
                        levelModifier = levelModifier + (4.5f * (levelModifier-59));

                    float mitigation = 0.1f * armor / (8.5f * levelModifier + 40);
                    mitigation = mitigation/(1.0f + mitigation);

                    if (mitigation < 0.0f)
                        mitigation = 0.0f;

                    if (mitigation > 0.75f)
                        mitigation = 0.75f;

                    // calculate base damage before armor mitigation
                    damage = uint32(damage / (1.0f - mitigation));

                    basepoints0 = damage;
                    break;
                }
                // Unstable Power
                case 24658:
                {
                    if (!procSpell || procSpell->Id == 24659)
                        return false;
                    // Need remove one 24659 aura
                    RemoveSingleAuraFromStack(24659, 0);
                    RemoveSingleAuraFromStack(24659, 1);
                    return true;
                }
                // Restless Strength
                case 24661:
                {
                    // Need remove one 24662 aura
                    RemoveSingleAuraFromStack(24662, 0);
                    return true;
                }
                // Adaptive Warding (Frostfire Regalia set)
                case 28764:
                {
                    if (!procSpell)
                        return false;

                    // find Mage Armor
                    bool found = false;
                    AuraList const& mRegenInterupt = GetAurasByType(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT);
                    for (AuraList::const_iterator iter = mRegenInterupt.begin(); iter != mRegenInterupt.end(); ++iter)
                    {
                        if (SpellEntry const* iterSpellProto = (*iter)->GetSpellProto())
                        {
                            if (iterSpellProto->SpellFamilyName == SPELLFAMILY_MAGE && (iterSpellProto->SpellFamilyFlags & 0x10000000))
                            {
                                found=true;
                                break;
                            }
                        }
                    }
                    if (!found)
                        return false;

                    switch (GetFirstSchoolInMask(GetSpellSchoolMask(procSpell)))
                    {
                        case SPELL_SCHOOL_NORMAL:
                        case SPELL_SCHOOL_HOLY:
                            return false;                   // ignored
                        case SPELL_SCHOOL_FIRE:   triggered_spell_id = 28765; break;
                        case SPELL_SCHOOL_NATURE: triggered_spell_id = 28768; break;
                        case SPELL_SCHOOL_FROST:  triggered_spell_id = 28766; break;
                        case SPELL_SCHOOL_SHADOW: triggered_spell_id = 28769; break;
                        case SPELL_SCHOOL_ARCANE: triggered_spell_id = 28770; break;
                        default:
                            return false;
                    }

                    target = this;
                    break;
                }
                // Obsidian Armor (Justice Bearer`s Pauldrons shoulder)
                case 27539:
                {
                    if (!procSpell)
                        return false;

                    switch (GetFirstSchoolInMask(GetSpellSchoolMask(procSpell)))
                    {
                        case SPELL_SCHOOL_NORMAL:
                            return false;                   // ignore
                        case SPELL_SCHOOL_HOLY:   triggered_spell_id = 27536; break;
                        case SPELL_SCHOOL_FIRE:   triggered_spell_id = 27533; break;
                        case SPELL_SCHOOL_NATURE: triggered_spell_id = 27538; break;
                        case SPELL_SCHOOL_FROST:  triggered_spell_id = 27534; break;
                        case SPELL_SCHOOL_SHADOW: triggered_spell_id = 27535; break;
                        case SPELL_SCHOOL_ARCANE: triggered_spell_id = 27540; break;
                        default:
                            return false;
                    }

                    target = this;
                    break;
                }
                // Mana Leech (Passive) (Priest Pet Aura)
                case 28305:
                {
                    // Cast on owner
                    target = GetOwner();
                    if (!target)
                        return false;

                    basepoints0 = int32(damage * 2.5f);     // manaregen
                    triggered_spell_id = 34650;
                    break;
                }
                // Mark of Malice
                case 33493:
                {
                    // Cast finish spell at last charge
                    if (triggeredByAura->m_procCharges > 1)
                        return false;

                    target = this;
                    triggered_spell_id = 33494;
                    break;
                }
                // Twisted Reflection (boss spell)
                case 21063:
                    triggered_spell_id = 21064;
                    break;
                // Vampiric Aura (boss spell)
                case 38196:
                {
                    basepoints0 = 3 * damage;               // 300%
                    if (basepoints0 < 0)
                        return false;

                    triggered_spell_id = 31285;
                    target = this;
                    break;
                }
                // Aura of Madness (Darkmoon Card: Madness trinket)
                //=====================================================
                // 39511 Sociopath: +35 strength (Paladin, Rogue, Druid, Warrior)
                // 40997 Delusional: +70 attack power (Rogue, Hunter, Paladin, Warrior, Druid)
                // 40998 Kleptomania: +35 agility (Warrior, Rogue, Paladin, Hunter, Druid)
                // 40999 Megalomania: +41 damage/healing (Druid, Shaman, Priest, Warlock, Mage, Paladin)
                // 41002 Paranoia: +35 spell/melee/ranged crit strike rating (All classes)
                // 41005 Manic: +35 haste (spell, melee and ranged) (All classes)
                // 41009 Narcissism: +35 intellect (Druid, Shaman, Priest, Warlock, Mage, Paladin, Hunter)
                // 41011 Martyr Complex: +35 stamina (All classes)
                // 41406 Dementia: Every 5 seconds either gives you +5% damage/healing. (Druid, Shaman, Priest, Warlock, Mage, Paladin)
                // 41409 Dementia: Every 5 seconds either gives you -5% damage/healing. (Druid, Shaman, Priest, Warlock, Mage, Paladin)
                case 39446:
                {
                    if (GetTypeId() != TYPEID_PLAYER || !this->isAlive())
                        return false;

                    // Select class defined buff
                    switch (getClass())
                    {
                        case CLASS_PALADIN:                 // 39511, 40997, 40998, 40999, 41002, 41005, 41009, 41011, 41409
                        case CLASS_DRUID:                   // 39511, 40997, 40998, 40999, 41002, 41005, 41009, 41011, 41409
                        {
                            uint32 RandomSpell[]={39511, 40997, 40998, 40999, 41002, 41005, 41009, 41011, 41409};
                            triggered_spell_id = RandomSpell[ irand(0, sizeof(RandomSpell)/sizeof(uint32) - 1) ];
                            break;
                        }
                        case CLASS_ROGUE:                   // 39511, 40997, 40998, 41002, 41005, 41011
                        case CLASS_WARRIOR:                 // 39511, 40997, 40998, 41002, 41005, 41011
                        {
                            uint32 RandomSpell[]={39511, 40997, 40998, 41002, 41005, 41011};
                            triggered_spell_id = RandomSpell[ irand(0, sizeof(RandomSpell)/sizeof(uint32) - 1) ];
                            break;
                        }
                        case CLASS_PRIEST:                  // 40999, 41002, 41005, 41009, 41011, 41406, 41409
                        case CLASS_SHAMAN:                  // 40999, 41002, 41005, 41009, 41011, 41406, 41409
                        case CLASS_MAGE:                    // 40999, 41002, 41005, 41009, 41011, 41406, 41409
                        case CLASS_WARLOCK:                 // 40999, 41002, 41005, 41009, 41011, 41406, 41409
                        {
                            uint32 RandomSpell[]={40999, 41002, 41005, 41009, 41011, 41406, 41409};
                            triggered_spell_id = RandomSpell[ irand(0, sizeof(RandomSpell)/sizeof(uint32) - 1) ];
                            break;
                        }
                        case CLASS_HUNTER:                  // 40997, 40999, 41002, 41005, 41009, 41011, 41406, 41409
                        {
                            uint32 RandomSpell[]={40997, 40999, 41002, 41005, 41009, 41011, 41406, 41409};
                            triggered_spell_id = RandomSpell[ irand(0, sizeof(RandomSpell)/sizeof(uint32) - 1) ];
                            break;
                        }
                        default:
                            return false;
                    }

                    target = this;
                    if (roll_chance_i(10))
                        ToPlayer()->Say("This is Madness!", LANG_UNIVERSAL);
                    break;
                }
                /*
                // TODO: need find item for aura and triggered spells
                // Sunwell Exalted Caster Neck (??? neck)
                // cast ??? Light's Wrath if Exalted by Aldor
                // cast ??? Arcane Bolt if Exalted by Scryers*/
                case 46569:
                    return false;                           // disable for while
                // Sunwell Exalted Caster Neck (Shattered Sun Pendant of Acumen neck)
                // cast 45479 Light's Wrath if Exalted by Aldor
                // cast 45429 Arcane Bolt if Exalted by Scryers
                case 45481:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // Get Aldor reputation rank
                    if (ToPlayer()->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45479;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (ToPlayer()->GetReputationRank(934) == REP_EXALTED)
                    {
                        if (this->IsFriendlyTo(target))
                            return false;

                        triggered_spell_id = 45429;
                        break;
                    }
                    return false;
                }
                // Sunwell Exalted Melee Neck (Shattered Sun Pendant of Might neck)
                // cast 45480 Light's Strength if Exalted by Aldor
                // cast 45428 Arcane Strike if Exalted by Scryers
                case 45482:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // Get Aldor reputation rank
                    if (ToPlayer()->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45480;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (ToPlayer()->GetReputationRank(934) == REP_EXALTED)
                    {
                        triggered_spell_id = 45428;
                        break;
                    }
                    return false;
                }
                // Sunwell Exalted Tank Neck (Shattered Sun Pendant of Resolve neck)
                // cast 45431 Arcane Insight if Exalted by Aldor
                // cast 45432 Light's Ward if Exalted by Scryers
                case 45483:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // Get Aldor reputation rank
                    if (ToPlayer()->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45432;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (ToPlayer()->GetReputationRank(934) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45431;
                        break;
                    }
                    return false;
                }
                // Sunwell Exalted Healer Neck (Shattered Sun Pendant of Restoration neck)
                // cast 45478 Light's Salvation if Exalted by Aldor
                // cast 45430 Arcane Surge if Exalted by Scryers
                case 45484:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // Get Aldor reputation rank
                    if (ToPlayer()->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45478;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (ToPlayer()->GetReputationRank(934) == REP_EXALTED)
                    {
                        triggered_spell_id = 45430;
                        break;
                    }
                    return false;
                }
            }
            break;
        }
        case SPELLFAMILY_MAGE:
        {
            // Magic Absorption
            if (dummySpell->SpellIconID == 459)             // only this spell have SpellIconID == 459 and dummy aura
            {
                if (getPowerType() != POWER_MANA)
                    return false;

                // mana reward
                basepoints0 = (triggeredByAura->GetModifier()->m_amount * GetMaxPower(POWER_MANA) / 100);
                target = this;
                triggered_spell_id = 29442;
                break;
            }
            // Master of Elements
            if (dummySpell->SpellIconID == 1920)
            {
                if (!procSpell)
                    return false;

                // mana cost save
                basepoints0 = procSpell->manaCost * triggeredByAura->GetModifier()->m_amount/100;
                if (basepoints0 <=0)
                    return false;

                target = this;
                triggered_spell_id = 29077;
                break;
            }
            // Incanter's Regalia set (add trigger chance to Mana Shield)
            if (dummySpell->SpellFamilyFlags & 0x0000000000008000LL)
            {
                if (GetTypeId() != TYPEID_PLAYER)
                    return false;

                // Only proc if target has Incanter's Regalia equiped
                if (!HasAura(37424, 0))
                    return false;

                target = this;
                triggered_spell_id = 37436;
                break;
            }
            switch (dummySpell->Id)
            {
                // Ignite
                case 11119:
                case 11120:
                case 12846:
                case 12847:
                case 12848:
                {
                    // Patch 2.3.0 (13-Nov-2007): Ignite: This talent is no longer triggered by damage dealt by Molten Armor.
                    if (procSpell && procSpell->Id == 34913)
                        return false;

                    switch (dummySpell->Id)
                    {
                        case 11119: basepoints0 = int32(0.04f*damage); break;
                        case 11120: basepoints0 = int32(0.08f*damage); break;
                        case 12846: basepoints0 = int32(0.12f*damage); break;
                        case 12847: basepoints0 = int32(0.16f*damage); break;
                        case 12848: basepoints0 = int32(0.20f*damage); break;
                        default:
                            sLog->outError("Unit::HandleDummyAuraProc: non handled spell id: %u (IG)", dummySpell->Id);
                            return false;
                    }

                    AuraList const &DoT = pVictim->GetAurasByType(SPELL_AURA_PERIODIC_DAMAGE);
                    for (AuraList::const_iterator itr = DoT.begin(); itr != DoT.end(); ++itr)
                        if ((*itr)->GetId() == 12654 && (*itr)->GetCaster() == this)
                            if ((*itr)->GetBasePoints() > 0)
                                basepoints0 += int((*itr)->GetBasePoints()/((*itr)->GetTickNumber() + 1));

                    triggered_spell_id = 12654;
                    break;
                }
                // Combustion
                case 11129:
                {
                    //last charge and crit
                    if (triggeredByAura->m_procCharges <= 1 && (procEx & PROC_EX_CRITICAL_HIT))
                    {
                        RemoveAurasDueToSpell(28682);       //-> remove Combustion auras
                        return true;                        // charge counting (will removed)
                    }

                    CastSpell(this, 28682, true, castItem, triggeredByAura);
                    return (procEx & PROC_EX_CRITICAL_HIT);// charge update only at crit hits, no hidden cooldowns
                }
            }
            break;
        }
        case SPELLFAMILY_WARRIOR:
        {
            // Retaliation
            if (dummySpell->SpellFamilyFlags == 0x0000000800000000LL)
            {
                // check attack comes not from behind
                if (!HasInArc(M_PI, pVictim))
                    return false;

                triggered_spell_id = 22858;
                break;
            }
            // Second Wind
            else if (dummySpell->SpellIconID == 1697)
            {
                // only for spells and hit/crit (trigger start always) and not start from self casted spells (5530 Mace Stun Effect for example)
                if (procSpell == 0 || !(procEx & (PROC_EX_NORMAL_HIT|PROC_EX_CRITICAL_HIT)) || this == pVictim)
                    return false;
                // Need stun or root mechanic
                if (procSpell->Mechanic != MECHANIC_ROOT && procSpell->Mechanic != MECHANIC_STUN)
                {
                    int32 i;
                    for (i = 0; i < 3; i++)
                        if (procSpell->EffectMechanic[i] == MECHANIC_ROOT || procSpell->EffectMechanic[i] == MECHANIC_STUN)
                            break;
                    if (i == 3)
                        return false;
                }

                switch (dummySpell->Id)
                {
                    case 29838: triggered_spell_id=29842; break;
                    case 29834: triggered_spell_id=29841; break;
                    default:
                        sLog->outError("Unit::HandleDummyAuraProc: non handled spell id: %u (SW)", dummySpell->Id);
                    return false;
                }

                target = this;
                break;
            }
            break;
        }
        case SPELLFAMILY_WARLOCK:
        {
            // Seed of Corruption
            if (dummySpell->SpellFamilyFlags & 0x0000001000000000LL)
            {
                if (procSpell && procSpell->Id == 27285)
                    return false;
                Modifier* mod = triggeredByAura->GetModifier();
                // if damage is more than need or target die from damage deal finish spell
                if (mod->m_amount <= damage || GetHealth() <= damage)
                {
                    // remember guid before aura delete
                    uint64 casterGuid = triggeredByAura->GetCasterGUID();

                    // Remove aura (before cast for prevent infinite loop handlers)
                    RemoveAurasDueToSpell(triggeredByAura->GetId());

                    // Cast finish spell (triggeredByAura already not exist!)
                    if (Unit* caster = GetUnit(*this, casterGuid))
                        caster->CastSpell(this, 27285, true, castItem);
                    return true;                            // no hidden cooldown
                }

                // Damage counting
                mod->m_amount-=damage;
                return true;
            }
            // Seed of Corruption (Mobs cast) - no die req
            if (dummySpell->SpellFamilyFlags == 0x00LL && dummySpell->SpellIconID == 1932)
            {
                Modifier* mod = triggeredByAura->GetModifier();
                // if damage is more than need deal finish spell
                if (mod->m_amount <= damage)
                {
                    // remember guid before aura delete
                    uint64 casterGuid = triggeredByAura->GetCasterGUID();

                    // Remove aura (before cast for prevent infinite loop handlers)
                    RemoveAurasDueToSpell(triggeredByAura->GetId());

                    // Cast finish spell (triggeredByAura already not exist!)
                    if (Unit* caster = GetUnit(*this, casterGuid))
                        caster->CastSpell(this, 32865, true, castItem);
                    return true;                            // no hidden cooldown
                }
                // Damage counting
                mod->m_amount-=damage;
                return true;
            }
            switch (dummySpell->Id)
            {
                // Nightfall
                case 18094:
                case 18095:
                {
                    target = this;
                    triggered_spell_id = 17941;
                    break;
                }
                //Soul Leech
                case 30293:
                case 30295:
                case 30296:
                {
                    // health
                    basepoints0 = int32(damage*triggeredByAura->GetModifier()->m_amount/100);
                    target = this;
                    triggered_spell_id = 30294;
                    break;
                }
                // Shadowflame (Voidheart Raiment set bonus)
                case 37377:
                {
                    triggered_spell_id = 37379;
                    break;
                }
                // Pet Healing (Corruptor Raiment or Rift Stalker Armor)
                case 37381:
                {
                    target = GetGuardianPet();
                    if (!target)
                        return false;

                    // heal amount
                    basepoints0 = damage * triggeredByAura->GetModifier()->m_amount/100;
                    triggered_spell_id = 37382;
                    break;
                }
                // Shadowflame Hellfire (Voidheart Raiment set bonus)
                case 39437:
                {
                    triggered_spell_id = 37378;
                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_PRIEST:
        {
            // Vampiric Touch
            if (dummySpell->SpellFamilyFlags & 0x0000040000000000LL)
            {
                if (!pVictim || !pVictim->isAlive())
                    return false;

                // pVictim is caster of aura
                if (triggeredByAura->GetCasterGUID() != pVictim->GetGUID())
                    return false;

                // energize amount
                basepoints0 = triggeredByAura->GetModifier()->m_amount*damage/100;
                pVictim->CastCustomSpell(pVictim, 34919, &basepoints0, NULL, NULL, true, castItem, triggeredByAura);
                return true;                                // no hidden cooldown
            }
            switch (dummySpell->Id)
            {
                // Vampiric Embrace
                case 15286:
                {
                    if (!pVictim || !pVictim->isAlive())
                        return false;

                    // pVictim is caster of aura
                    if (triggeredByAura->GetCasterGUID() != pVictim->GetGUID())
                        return false;

                    // heal amount
                    basepoints0 = triggeredByAura->GetModifier()->m_amount*damage/100;
                    pVictim->CastCustomSpell(pVictim, 15290, &basepoints0, NULL, NULL, true, castItem, triggeredByAura);
                    return true;                                // no hidden cooldown
                }
                // Priest Tier 6 Trinket (Ashtongue Talisman of Acumen)
                case 40438:
                {
                    // Shadow Word: Pain
                    if (procSpell->SpellFamilyFlags & 0x0000000000008000LL)
                        triggered_spell_id = 40441;
                    // Renew
                    else if (procSpell->SpellFamilyFlags & 0x0000000000000040LL)
                        triggered_spell_id = 40440;
                    else
                        return false;

                    target = this;
                    break;
                }
                // Oracle Healing Bonus ("Garments of the Oracle" set)
                case 26169:
                {
                    // heal amount
                    basepoints0 = int32(damage * 10/100);
                    target = this;
                    triggered_spell_id = 26170;
                    break;
                }
                // Frozen Shadoweave (Shadow's Embrace set) warning! its not only priest set
                case 39372:
                {
                    if (!procSpell || (GetSpellSchoolMask(procSpell) & (SPELL_SCHOOL_MASK_FROST | SPELL_SCHOOL_MASK_SHADOW)) == 0)
                        return false;

                    // heal amount
                    basepoints0 = int32(damage * 2 / 100);
                    target = this;
                    triggered_spell_id = 39373;
                    break;
                }
                // Greater Heal (Vestments of Faith (Priest Tier 3) - 4 pieces bonus)
                case 28809:
                {
                    triggered_spell_id = 28810;
                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_DRUID:
        {
            switch (dummySpell->Id)
            {
                // Healing Touch (Dreamwalker Raiment set)
                case 28719:
                {
                    // mana back
                    basepoints0 = int32(procSpell->manaCost * 30 / 100);
                    target = this;
                    triggered_spell_id = 28742;
                    break;
                }
                // Healing Touch Refund (Idol of Longevity trinket)
                case 28847:
                {
                    target = this;
                    triggered_spell_id = 28848;
                    break;
                }
                // Mana Restore (Malorne Raiment set / Malorne Regalia set)
                case 37288:
                case 37295:
                {
                    target = this;
                    triggered_spell_id = 37238;
                    break;
                }
                // Druid Tier 6 Trinket
                case 40442:
                {
                    float  chance;

                    // Starfire
                    if (procSpell->SpellFamilyFlags & 0x0000000000000004LL)
                    {
                        triggered_spell_id = 40445;
                        chance = 25.0f;
                    }
                    // Rejuvenation
                    else if (procSpell->SpellFamilyFlags & 0x0000000000000010LL)
                    {
                        triggered_spell_id = 40446;
                        chance = 25.0f;
                    }
                    // Mangle (Bear) and Mangle (Cat)
                    else if (procSpell->SpellFamilyFlags & 0x0000044000000000LL)
                    {
                        triggered_spell_id = 40452;
                        chance = 40.0f;
                    }
                    else
                        return false;

                    if (!roll_chance_f(chance))
                        return false;

                    target = this;
                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_ROGUE:
        {
            // Should be casted before projectile hits target
            /*switch (dummySpell->Id)
            {
                // Deadly Throw Interrupt
                case 32748:
                {
                    // Prevent cast Deadly Throw Interrupt on self from last effect (apply dummy) of Deadly Throw
                    if (this == pVictim)
                        return false;

                    triggered_spell_id = 32747;
                    break;
                }
            }*/
            // Quick Recovery
            if (dummySpell->SpellIconID == 2116)
            {
                if (!procSpell)
                    return false;

                // only rogue's finishing moves (maybe need additional checks)
                if (procSpell->SpellFamilyName != SPELLFAMILY_ROGUE ||
                    (procSpell->SpellFamilyFlags & SPELLFAMILYFLAG_ROGUE__FINISHING_MOVE) == 0)
                    return false;

                // energy cost save
                basepoints0 = procSpell->manaCost * triggeredByAura->GetModifier()->m_amount/100;
                if (basepoints0 <= 0)
                    return false;

                target = this;
                triggered_spell_id = 31663;
                break;
            }
            break;
        }
        case SPELLFAMILY_HUNTER:
        {
            // Thrill of the Hunt
            if (dummySpell->SpellIconID == 2236)
            {
                if (!procSpell)
                    return false;

                // mana cost save
                basepoints0 = procSpell->manaCost * 40/100;
                if (basepoints0 <= 0)
                    return false;

                target = this;
                triggered_spell_id = 34720;
                break;
            }
            break;
        }
        case SPELLFAMILY_PALADIN:
        {
            // Seal of Righteousness - melee proc dummy
            if (dummySpell->SpellFamilyFlags&0x000000008000000LL && triggeredByAura->GetEffIndex() == 0)
            {
                if (GetTypeId() != TYPEID_PLAYER)
                    return false;

                uint32 spellId;
                switch (triggeredByAura->GetId())
                {
                    case 21084: spellId = 25742; break;     // Rank 1
                    case 20287: spellId = 25740; break;     // Rank 2
                    case 20288: spellId = 25739; break;     // Rank 3
                    case 20289: spellId = 25738; break;     // Rank 4
                    case 20290: spellId = 25737; break;     // Rank 5
                    case 20291: spellId = 25736; break;     // Rank 6
                    case 20292: spellId = 25735; break;     // Rank 7
                    case 20293: spellId = 25713; break;     // Rank 8
                    case 27155: spellId = 27156; break;     // Rank 9
                    default:
                        sLog->outError("Unit::HandleDummyAuraProc: non handled possibly SoR (Id = %u)", triggeredByAura->GetId());
                        return false;
                }
                Item *item = ToPlayer()->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
                float speed = (item ? item->GetProto()->Delay : BASE_ATTACK_TIME)/1000.0f;

                float damageBasePoints;
                if (item && item->GetProto()->InventoryType == INVTYPE_2HWEAPON)
                    // two hand weapon
                    damageBasePoints=1.20f*triggeredByAura->GetModifier()->m_amount * 1.2f * 1.03f * speed/100.0f + 1;
                else
                    // one hand weapon/no weapon
                    damageBasePoints=0.85f*ceil(triggeredByAura->GetModifier()->m_amount * 1.2f * 1.03f * speed/100.0f) - 1;

                int32 damagePoint = int32(damageBasePoints + 0.03f * (GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE)+GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE))/2.0f) + 1;

                // apply damage bonuses manually
                if (damagePoint >= 0)
                {
                    damagePoint = SpellDamageBonusDone(pVictim, dummySpell, damagePoint, SPELL_DIRECT_DAMAGE);
                    damagePoint = pVictim->SpellDamageBonusTaken(this, dummySpell, damagePoint, SPELL_DIRECT_DAMAGE);
                }

                CastCustomSpell(pVictim, spellId, &damagePoint, NULL, NULL, true, NULL, triggeredByAura);
                return true;                                // no hidden cooldown
            }
            // Seal of Blood do damage trigger
            if (dummySpell->SpellFamilyFlags & 0x0000040000000000LL)
            {
                // don't proc from Seal of Blood (self proc) or Crusader Strike
                if (procSpell && (procSpell->Id == 31893 || procSpell->Id == 31892 || procSpell->Id == 35395))
                    return false;

                switch (triggeredByAura->GetEffIndex())
                {
                    case 0:
                        triggered_spell_id = 31893;
                        break;
                    case 1:
                    {
                        // damage
                        damage += CalculateDamage(BASE_ATTACK, false) * 10 / 100; // add spell damage from prev effect (10%)
                        basepoints0 =  triggeredByAura->GetModifier()->m_amount * damage / 100;

                        target = this;

                        triggered_spell_id = 32221;
                        break;
                    }
                }
            }

            switch (dummySpell->Id)
            {
                // Holy Power (Redemption Armor set)
                case 28789:
                {
                    if (!pVictim)
                        return false;

                    // Set class defined buff
                    switch (pVictim->getClass())
                    {
                        case CLASS_PALADIN:
                        case CLASS_PRIEST:
                        case CLASS_SHAMAN:
                        case CLASS_DRUID:
                            triggered_spell_id = 28795;     // Increases the friendly target's mana regeneration by $s1 per 5 sec. for $d.
                            break;
                        case CLASS_MAGE:
                        case CLASS_WARLOCK:
                            triggered_spell_id = 28793;     // Increases the friendly target's spell damage and healing by up to $s1 for $d.
                            break;
                        case CLASS_HUNTER:
                        case CLASS_ROGUE:
                            triggered_spell_id = 28791;     // Increases the friendly target's attack power by $s1 for $d.
                            break;
                        case CLASS_WARRIOR:
                            triggered_spell_id = 28790;     // Increases the friendly target's armor
                            break;
                        default:
                            return false;
                    }
                    break;
                }
                //Seal of Vengeance
                case 31801:
                {
                    if (effIndex != 0)                       // effect 1, 2 used by seal unleashing code
                        return false;

                    triggered_spell_id = 31803;
                    break;
                }
                // Spiritual Att.
                case 31785:
                case 33776:
                {
                    // if healed by another unit (pVictim)
                    if (this == pVictim)
                        return false;

                    // heal amount
                    basepoints0 = triggeredByAura->GetModifierValue() * std::min(damage, GetMaxHealth() - GetHealth()) / 100;
                    target = this;

                    sLog->outError("mod: %i", triggeredByAura->GetModifierValue());
                    sLog->outError("damage: %u", damage);
                    sLog->outError("basepoints0: %u", basepoints0);

                    if (basepoints0)
                        triggered_spell_id = 31786;
                    break;
                }
                // Paladin Tier 6 Trinket (Ashtongue Talisman of Zeal)
                case 40470:
                {
                    if (!procSpell)
                        return false;

                    float  chance;

                    // Flash of light/Holy light
                    if (procSpell->SpellFamilyFlags & 0x00000000C0000000LL)
                    {
                        triggered_spell_id = 40471;
                        chance = 15.0f;
                    }
                    // Judgement
                    else if (procSpell->SpellFamilyFlags & 0x0000000000800000LL)
                    {
                        triggered_spell_id = 40472;
                        chance = 50.0f;
                    }
                    else
                        return false;

                    if (!roll_chance_f(chance))
                        return false;

                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_SHAMAN:
        {
            switch (dummySpell->Id)
            {
                // Totemic Power (The Earthshatterer set)
                case 28823:
                {
                    if (!pVictim)
                        return false;

                    // Set class defined buff
                    switch (pVictim->getClass())
                    {
                        case CLASS_PALADIN:
                        case CLASS_PRIEST:
                        case CLASS_SHAMAN:
                        case CLASS_DRUID:
                            triggered_spell_id = 28824;     // Increases the friendly target's mana regeneration by $s1 per 5 sec. for $d.
                            break;
                        case CLASS_MAGE:
                        case CLASS_WARLOCK:
                            triggered_spell_id = 28825;     // Increases the friendly target's spell damage and healing by up to $s1 for $d.
                            break;
                        case CLASS_HUNTER:
                        case CLASS_ROGUE:
                            triggered_spell_id = 28826;     // Increases the friendly target's attack power by $s1 for $d.
                            break;
                        case CLASS_WARRIOR:
                            triggered_spell_id = 28827;     // Increases the friendly target's armor
                            break;
                        default:
                            return false;
                    }
                    break;
                }
                // Lesser Healing Wave (Totem of Flowing Water Relic)
                case 28849:
                {
                    target = this;
                    triggered_spell_id = 28850;
                    break;
                }
                // Windfury Weapon (Passive) 1-5 Ranks
                case 33757:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                        return false;

                    if (!castItem || !castItem->IsEquipped())
                        return false;

                    if (triggeredByAura && castItem->GetGUID() != triggeredByAura->GetCastItemGUID())
                        return false;

                    // custom cooldown processing case
                    if (cooldown && ToPlayer()->HasSpellCooldown(dummySpell->Id))
                        return false;

                    uint32 spellId;
                    switch (castItem->GetEnchantmentId(EnchantmentSlot(TEMP_ENCHANTMENT_SLOT)))
                    {
                        case 283: spellId = 33757; break;   //1 Rank
                        case 284: spellId = 33756; break;   //2 Rank
                        case 525: spellId = 33755; break;   //3 Rank
                        case 1669:spellId = 33754; break;   //4 Rank
                        case 2636:spellId = 33727; break;   //5 Rank
                        default:
                        {
                            sLog->outError("Unit::HandleDummyAuraProc: non handled item enchantment (rank?) %u for spell id: %u (Windfury)",
                                castItem->GetEnchantmentId(EnchantmentSlot(TEMP_ENCHANTMENT_SLOT)), dummySpell->Id);
                            return false;
                        }
                    }

                    SpellEntry const* windfurySpellEntry = sSpellStore.LookupEntry(spellId);
                    if (!windfurySpellEntry)
                    {
                        sLog->outError("Unit::HandleDummyAuraProc: invalid spell id: %u (Windfury)", spellId);
                        return false;
                    }

                    int32 extra_attack_power = CalculateSpellDamage(pVictim, windfurySpellEntry, EFFECT_INDEX_0);

                    // Off-Hand case
                    if (castItem->GetSlot() == EQUIPMENT_SLOT_OFFHAND)
                    {
                        // Value gained from additional AP
                        basepoints0 = int32(extra_attack_power / 14.0f * GetAttackTime(OFF_ATTACK) / 1000 / 2);
                        triggered_spell_id = 33750;
                    }
                    // Main-Hand case
                    else
                    {
                        // Value gained from additional AP
                        basepoints0 = int32(extra_attack_power / 14.0f * GetAttackTime(BASE_ATTACK) / 1000);
                        triggered_spell_id = 25504;
                    }

                    // apply cooldown before cast to prevent processing itself
                    if (cooldown)
                        ToPlayer()->AddSpellCooldown(dummySpell->Id, 0, time(NULL) + cooldown);

                    // Attack Twice
                    for (uint32 i = 0; i < 2; ++i)
                        CastCustomSpell(pVictim, triggered_spell_id, &basepoints0, NULL, NULL, true, 0, 0, originalCaster);

                    return true;
                }
                // Shaman Tier 6 Trinket
                case 40463:
                {
                    if (!procSpell)
                        return false;

                    float chance;
                    if (procSpell->SpellFamilyFlags & 0x0000000000000001LL)
                    {
                        triggered_spell_id = 40465;         // Lightning Bolt
                        chance = 15.0f;
                    }
                    else if (procSpell->SpellFamilyFlags & 0x0000000000000080LL)
                    {
                        triggered_spell_id = 40465;         // Lesser Healing Wave
                        chance = 10.0f;
                    }
                    else if (procSpell->SpellFamilyFlags & 0x0000001000000000LL)
                    {
                        triggered_spell_id = 40466;         // Stormstrike
                        chance = 50.0f;
                    }
                    else
                        return false;

                    if (!roll_chance_f(chance))
                        return false;

                    target = this;
                    break;
                }
            }

            // Earth Shield
            if (dummySpell->SpellFamilyFlags == 0x40000000000LL)
            {
                // Now correctly uses the Shaman's own spell critical strike chance to determine the chance of a critical heal.
                originalCaster = triggeredByAura->GetCasterGUID();
                target = this;
                basepoints0 = triggeredByAura->GetModifier()->m_amount;
                triggered_spell_id = 379;
                break;
            }
            // Lightning Overload
            if (dummySpell->SpellIconID == 2018)            // only this spell have SpellFamily Shaman SpellIconID == 2018 and dummy aura
            {
                if (!procSpell || GetTypeId() != TYPEID_PLAYER || !pVictim)
                    return false;

                // custom cooldown processing case
                if (cooldown && GetTypeId() == TYPEID_PLAYER && ToPlayer()->HasSpellCooldown(dummySpell->Id))
                    return false;

                uint32 spellId = 0;
                // Every Lightning Bolt and Chain Lightning spell have duplicate vs half damage and zero cost
                switch (procSpell->Id)
                {
                    // Lightning Bolt
                    case   403: spellId = 45284; break;     // Rank  1
                    case   529: spellId = 45286; break;     // Rank  2
                    case   548: spellId = 45287; break;     // Rank  3
                    case   915: spellId = 45288; break;     // Rank  4
                    case   943: spellId = 45289; break;     // Rank  5
                    case  6041: spellId = 45290; break;     // Rank  6
                    case 10391: spellId = 45291; break;     // Rank  7
                    case 10392: spellId = 45292; break;     // Rank  8
                    case 15207: spellId = 45293; break;     // Rank  9
                    case 15208: spellId = 45294; break;     // Rank 10
                    case 25448: spellId = 45295; break;     // Rank 11
                    case 25449: spellId = 45296; break;     // Rank 12
                    // Chain Lightning
                    case   421: spellId = 45297; break;     // Rank  1
                    case   930: spellId = 45298; break;     // Rank  2
                    case  2860: spellId = 45299; break;     // Rank  3
                    case 10605: spellId = 45300; break;     // Rank  4
                    case 25439: spellId = 45301; break;     // Rank  5
                    case 25442: spellId = 45302; break;     // Rank  6
                    default:
                        sLog->outError("Unit::HandleDummyAuraProc: non handled spell id: %u (LO)", procSpell->Id);
                        return false;
                }
                // No thread generated mod
                SpellModifier *mod = new SpellModifier;
                mod->op = SPELLMOD_THREAT;
                mod->value = -100;
                mod->type = SPELLMOD_PCT;
                mod->spellId = dummySpell->Id;
                mod->effectId = 0;
                mod->lastAffected = NULL;
                mod->mask = 0x0000000000000003LL;
                mod->charges = 0;
                ToPlayer()->AddSpellMod(mod, true);

                // Remove cooldown (Chain Lightning - have Category Recovery time)
                if (procSpell->SpellFamilyFlags & 0x0000000000000002LL)
                    ToPlayer()->RemoveSpellCooldown(spellId);

                // Hmmm.. in most case spells already set half basepoints but...
                // Lightning Bolt (2-10 rank) have full basepoint and half bonus from level
                // As on wiki:
                // BUG: Rank 2 to 10 (and maybe 11) of Lightning Bolt will proc another Bolt with FULL damage (not halved). This bug is known and will probably be fixed soon.
                // So - no add changes :)
                CastSpell(pVictim, spellId, true, castItem, triggeredByAura);

                ToPlayer()->AddSpellMod(mod, false);

                if (cooldown && GetTypeId() == TYPEID_PLAYER)
                    ToPlayer()->AddSpellCooldown(dummySpell->Id, 0, time(NULL) + cooldown);

                return true;
            }
            break;
        }
        case SPELLFAMILY_POTION:
        {
            // alchemist's stone
            /*
            if (dummySpell->Id == 17619)
            {
                if (procSpell->SpellFamilyName == SPELLFAMILY_POTION)
                {
                    for (uint8 i = 0;i < 3;i++)
                    {
                        if (procSpell->Effect[i] == SPELL_EFFECT_HEAL)
                        {
                            triggered_spell_id = 21399;
                        }
                        else if (procSpell->Effect[i] == SPELL_EFFECT_ENERGIZE)
                        {
                            triggered_spell_id = 21400;
                        }
                        else
                            continue;

                        basepoints0 = CalculateSpellDamage(this, procSpell, i);
                        CastCustomSpell(this, triggered_spell_id, &basepoints0, NULL, NULL, true, castItem, triggeredByAura);
                    }
                    return true;
                }
            }
            */
        }
        default:
            break;
    }

    // processed charge only counting case
    if (!triggered_spell_id)
        return true;

    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(triggered_spell_id);

    if (!triggerEntry)
    {
        sLog->outError("Unit::HandleDummyAuraProc: Spell %u has invalid triggered spell %u", dummySpell->Id, triggered_spell_id);
        return false;
    }

    // default case
    if (!target || target != this && !target->isAlive())
        return false;

    if (cooldown && GetTypeId() == TYPEID_PLAYER && ToPlayer()->HasSpellCooldown(triggered_spell_id))
        return false;

    if (basepoints0)
        CastCustomSpell(target, triggered_spell_id, &basepoints0, NULL, NULL, true, castItem, triggeredByAura, originalCaster);
    else
        CastSpell(target, triggered_spell_id, true, castItem, triggeredByAura, originalCaster);

    if (cooldown && GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->AddSpellCooldown(triggered_spell_id, 0, time(NULL) + cooldown);

    return true;
}

bool Unit::HandleProcTriggerSpell(Unit *pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const *procSpell, uint32 procFlags, uint32 procEx, uint32 cooldown)
{
    // Get triggered aura spell info
    SpellEntry const* auraSpellInfo = triggeredByAura->GetSpellProto();

    // Basepoints of trigger aura
    int32 triggerAmount = triggeredByAura->GetModifier()->m_amount;

    // Set trigger spell id, target, custom basepoints
    uint32 trigger_spell_id = auraSpellInfo->EffectTriggerSpell[triggeredByAura->GetEffIndex()];

    Unit*  target = NULL;
    int32  basepoints0 = 0;

    if (triggeredByAura->GetModifier()->m_auraname == SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE)
        basepoints0 = triggerAmount;

    Item* castItem = triggeredByAura->GetCastItemGUID() && GetTypeId() == TYPEID_PLAYER
        ? ToPlayer()->GetItemByGuid(triggeredByAura->GetCastItemGUID()) : NULL;

    // Try handle unknown trigger spells
    if (sSpellStore.LookupEntry(trigger_spell_id) == NULL)
    {
        switch (auraSpellInfo->SpellFamilyName)
        {
            case SPELLFAMILY_GENERIC:
                switch (auraSpellInfo->Id)
                {
                    case 23780:             // Aegis of Preservation (Aegis of Preservation trinket)
                        trigger_spell_id = 23781;
                        break;
                    case 33896:             // Desperate Defense (Stonescythe Whelp, Stonescythe Alpha, Stonescythe Ambusher)
                        trigger_spell_id = 33898;
                        break;
                    case 43820:             // Charm of the Witch Doctor (Amani Charm of the Witch Doctor trinket)
                        // Pct value stored in dummy
                        basepoints0 = pVictim->GetCreateHealth() * auraSpellInfo->EffectBasePoints[1] / 100;
                        target = pVictim;
                        break;
                    case 27522:             // Mana Drain Trigger of Black Grasp of the Destroyer
                    case 46939:             // Mana Drain Trigger of Black Bow of the Betrayer
                    {
                        // On successful melee or ranged attack gain 8 mana and if possible drain 8 mana from the target.
                        if (isAlive())
                            CastSpell(this, 29471, true, castItem, triggeredByAura);
                        if (pVictim && pVictim->isAlive() && pVictim->getPowerType() == POWER_MANA && pVictim->GetPower(POWER_MANA) >= 8)
                        {
                            CastSpell(pVictim, 27526, true, castItem, triggeredByAura);
                            CastSpell(this, 29471, true, castItem, triggeredByAura);
                        }
                        return true;
                    }
                    // Moonkin Form (Passive)
                    case 24905:
                    {
                        // Elune's Touch (instead non-existed triggered spell) 30% from AP
                        trigger_spell_id = 33926;
                        basepoints0 = GetTotalAttackPowerValue(BASE_ATTACK) * 30 / 100;
                        target = this;
                    }
                }
                break;
            case SPELLFAMILY_MAGE:
                if (auraSpellInfo->SpellIconID == 2127)     // Blazing Speed
                {
                    switch (auraSpellInfo->Id)
                    {
                        case 31641:  // Rank 1
                        case 31642:  // Rank 2
                            trigger_spell_id = 31643;
                            break;
                        default:
                            sLog->outError("Unit::HandleProcTriggerSpell: Spell %u miss posibly Blazing Speed", auraSpellInfo->Id);
                            return false;
                    }
                }
                break;
            case SPELLFAMILY_WARRIOR:
                // Rampage
                if (auraSpellInfo->SpellIconID == 2006 && auraSpellInfo->SpellFamilyFlags == 0x100000)
                {
                    switch (auraSpellInfo->Id)
                    {
                        case 29801: trigger_spell_id = 30029; break;       // Rank 1
                        case 30030: trigger_spell_id = 30031; break;       // Rank 2
                        case 30033: trigger_spell_id = 30032; break;       // Rank 3
                        default:
                            sLog->outError("Unit::HandleProcTriggerSpell: Spell %u not handled in Rampage", auraSpellInfo->Id);
                        return false;
                    }
                }
                break;
            case SPELLFAMILY_WARLOCK:
            {
                // Pyroclasm
                if (auraSpellInfo->SpellIconID == 1137)
                {
                    if (!pVictim || !pVictim->isAlive() || pVictim == this || procSpell == NULL)
                        return false;
                    // Calculate spell tick count for spells
                    uint32 tick = 1; // Default tick = 1

                    // Hellfire have 15 tick
                    if (procSpell->SpellFamilyFlags&0x0000000000000040LL)
                        tick = 15;
                    // Rain of Fire have 4 tick
                    else if (procSpell->SpellFamilyFlags&0x0000000000000020LL)
                        tick = 4;

                    // Calculate chance = baseChance / tick
                    float chance = 0;
                    switch (auraSpellInfo->Id)
                    {
                        case 18096: chance = 13.0f / tick; break;
                        case 18073: chance = 26.0f / tick; break;
                    }
                    // Roll chance
                    if (!roll_chance_f(chance))
                        return false;

                    trigger_spell_id = 18093;
                }
                // Drain Soul
                else if (auraSpellInfo->SpellFamilyFlags & 0x0000000000004000LL)
                {
                    Unit::AuraList const& mAddFlatModifier = GetAurasByType(SPELL_AURA_ADD_FLAT_MODIFIER);
                    for (Unit::AuraList::const_iterator i = mAddFlatModifier.begin(); i != mAddFlatModifier.end(); ++i)
                    {
                        // Improved Drain Soul
                        if ((*i)->GetModifier()->m_miscvalue == SPELLMOD_CHANCE_OF_SUCCESS && (*i)->GetSpellProto()->SpellIconID == 113)
                        {
                            int32 value2 = CalculateSpellDamage(this, (*i)->GetSpellProto(), 2);
                            basepoints0 = value2 * GetMaxPower(POWER_MANA) / 100;
                        }
                    }

                    if (basepoints0)
                        trigger_spell_id = 18371;
                }
                break;
            }
            case SPELLFAMILY_PRIEST:
            {
                // Greater Heal Refund
                if (auraSpellInfo->Id == 37594)
                    trigger_spell_id = 37595;
                // Shadowguard
                else if (auraSpellInfo->SpellFamilyFlags == 0x100080000000LL && auraSpellInfo->SpellVisual == 7958)
                {
                    switch (auraSpellInfo->Id)
                    {
                        case 18137: trigger_spell_id = 28377; break;  // Rank 1
                        case 19308: trigger_spell_id = 28378; break;  // Rank 2
                        case 19309: trigger_spell_id = 28379; break;  // Rank 3
                        case 19310: trigger_spell_id = 28380; break;  // Rank 4
                        case 19311: trigger_spell_id = 28381; break;  // Rank 5
                        case 19312: trigger_spell_id = 28382; break;  // Rank 6
                        case 25477: trigger_spell_id = 28385; break;  // Rank 7
                        default:
                            sLog->outError("Unit::HandleProcTriggerSpell: Spell %u not handled in SG", auraSpellInfo->Id);
                        return false;
                    }
                }
                // Blessed Recovery
                else if (auraSpellInfo->SpellIconID == 1875)
                {
                    switch (auraSpellInfo->Id)
                    {
                        case 27811: trigger_spell_id = 27813; break;
                        case 27815: trigger_spell_id = 27817; break;
                        case 27816: trigger_spell_id = 27818; break;
                        default:
                            sLog->outError("Unit::HandleProcTriggerSpell: Spell %u not handled in BR", auraSpellInfo->Id);
                        return false;
                    }
                    basepoints0 = damage * triggerAmount / 100 / 3;
                    target = this;
                }
                break;
            }
            case SPELLFAMILY_DRUID:
            {
                // Druid Forms Trinket
                if (auraSpellInfo->Id == 37336)
                {
                    switch (m_form)
                    {
                        case 0:             trigger_spell_id = 37344;break;
                        case FORM_CAT:      trigger_spell_id = 37341;break;
                        case FORM_BEAR:
                        case FORM_DIREBEAR: trigger_spell_id = 37340;break;
                        case FORM_TREE:     trigger_spell_id = 37342;break;
                        case FORM_MOONKIN:  trigger_spell_id = 37343;break;
                        default:
                            return false;
                    }
                }
                // Leader of the Pack
                else if (auraSpellInfo->Id == 24932)
                {
                    if (triggerAmount == 0)
                        return false;
                    basepoints0 = triggerAmount * GetMaxHealth() / 100;
                    trigger_spell_id = 34299;
                }
                break;
            }
            case SPELLFAMILY_PALADIN:
            {
                switch (auraSpellInfo->Id)
                {
                    // Healing Discount
                    case 37705:
                    {
                        trigger_spell_id = 37706;
                        target = this;
                        break;
                    }
                    case 31828: // Blessed Life (Rank 1)
                    case 31829: // Blessed Life (Rank 2)
                    case 31830: // Blessed Life (Rank 3)
                    {
                        damage *= 0.5f;
                        return true;
                    }
                    // Lightning Capacitor
                    case 37657:
                    {
                        if (!pVictim || !pVictim->isAlive())
                            return false;
                        // stacking
                        CastSpell(this, 37658, true, NULL, triggeredByAura);

                        Aura * dummy = GetDummyAura(37658);
                        // release at 3 aura in stack (cont contain in basepoint of trigger aura)
                        if (!dummy || dummy->GetStackAmount() <= 2)
                            return false;

                        RemoveAurasDueToSpell(37658);
                        trigger_spell_id = 37661;
                        target = pVictim;
                        break;
                    }
                    default:
                        // Judgement of Light and Judgement of Wisdom
                        if (auraSpellInfo->SpellFamilyFlags & 0x0000000000080000LL)
                        {
                            // Seal of Blood (It will NOT activate Judgement of Light and Judgement of Wisdom)
                            if (procSpell && procSpell->Id == 31893)
                                return false;

                            switch (auraSpellInfo->Id)
                            {
                                // Judgement of Light
                                case 20185: trigger_spell_id = 20267; break;  // Rank 1
                                case 20344: trigger_spell_id = 20341; break;  // Rank 2
                                case 20345: trigger_spell_id = 20342; break;  // Rank 3
                                case 20346: trigger_spell_id = 20343; break;  // Rank 4
                                case 27162: trigger_spell_id = 27163; break;  // Rank 5
                                // Judgement of Wisdom
                                case 20186: trigger_spell_id = 20268; break;  // Rank 1
                                case 20354: trigger_spell_id = 20352; break;  // Rank 2
                                case 20355: trigger_spell_id = 20353; break;  // Rank 3
                                case 27164: trigger_spell_id = 27165; break;  // Rank 4
                                default:
                                    sLog->outError("Unit::HandleProcTriggerSpell: Spell %u miss posibly Judgement of Light/Wisdom", auraSpellInfo->Id);
                                return false;
                            }
                            SpellEntry const *triggerSpell = sSpellStore.LookupEntry(trigger_spell_id);
                            int32 amount = triggerSpell->EffectBasePoints[0] + 1;

                            if (Unit* auraCaster = triggeredByAura->GetCaster())
                            {
                                // Look through the caster's dummy auras for auras that affect Judgement of Light.
                                Unit::AuraList const& casterDummyAuras = auraCaster->GetAurasByType(SPELL_AURA_DUMMY);
                                for (Unit::AuraList::const_iterator i = casterDummyAuras.begin(); i != casterDummyAuras.end(); ++i)
                                {
                                    // Justicar 2-piece bonus (Holy)
                                    if ((*i)->GetSpellProto()->Id == 37182)
                                    {
                                        amount += (*i)->GetModifier()->m_amount;
                                        break;
                                    }
                                }
                            }

                            pVictim->CastCustomSpell(pVictim, trigger_spell_id, &amount, NULL, NULL, true, castItem, triggeredByAura);
                            return false;
                        }
                        // Illumination
                        if (auraSpellInfo->SpellIconID == 241)
                        {
                            if (!procSpell)
                                return false;
                            // procspell is triggered spell but we need mana cost of original casted spell
                            uint32 originalSpellId = procSpell->Id;
                            // Holy Shock heal
                            if (procSpell->SpellFamilyFlags & 0x1000000000000LL)
                            {
                                switch (procSpell->Id)
                                {
                                    case 25914: originalSpellId = 20473; break;
                                    case 25913: originalSpellId = 20929; break;
                                    case 25903: originalSpellId = 20930; break;
                                    case 27175: originalSpellId = 27174; break;
                                    case 33074: originalSpellId = 33072; break;
                                    default:
                                        sLog->outError("Unit::HandleProcTriggerSpell: Spell %u not handled in HShock", procSpell->Id);
                                        return false;
                                }
                            }
                            SpellEntry const *originalSpell = sSpellStore.LookupEntry(originalSpellId);
                            if (!originalSpell)
                            {
                                sLog->outError("Unit::HandleProcTriggerSpell: Spell %u unknown but selected as original in Illu", originalSpellId);
                                return false;
                            }
                            // percent stored in effect 1 (class scripts) base points
                            basepoints0 = originalSpell->manaCost*(auraSpellInfo->EffectBasePoints[1]+1)/100;
                            trigger_spell_id = 20272;
                            target = this;
                        }
                        break;
                }
                break;
            }
            case SPELLFAMILY_SHAMAN:
            {
                switch (auraSpellInfo->Id)
                {
                    // Lightning Shield (The Ten Storms set)
                    case 23551:
                    {
                        trigger_spell_id = 23552;
                        target = pVictim;
                        break;
                    }
                    // Damage from Lightning Shield (The Ten Storms set)
                    case 23552:
                    {
                        trigger_spell_id = 27635;
                        break;
                    }
                    // Mana Surge (The Earthfury set)
                    case 23572:
                    {
                        if (!procSpell)
                            return false;
                        basepoints0 = procSpell->manaCost * 35 / 100;
                        trigger_spell_id = 23571;
                        target = this;
                        break;
                    }
                    default:
                    {
                        // Lightning Shield (overwrite non existing triggered spell call in spell.dbc
                        if (auraSpellInfo->SpellFamilyFlags == 0x00000400 && auraSpellInfo->SpellVisual == 37)
                        {
                            switch (auraSpellInfo->Id)
                            {
                                case   324: trigger_spell_id = 26364; break;  // Rank 1
                                case   325: trigger_spell_id = 26365; break;  // Rank 2
                                case   905: trigger_spell_id = 26366; break;  // Rank 3
                                case   945: trigger_spell_id = 26367; break;  // Rank 4
                                case  8134: trigger_spell_id = 26369; break;  // Rank 5
                                case 10431: trigger_spell_id = 26370; break;  // Rank 6
                                case 10432: trigger_spell_id = 26363; break;  // Rank 7
                                case 25469: trigger_spell_id = 26371; break;  // Rank 8
                                case 25472: trigger_spell_id = 26372; break;  // Rank 9
                                default:
                                    sLog->outError("Unit::HandleProcTriggerSpell: Spell %u not handled in LShield", auraSpellInfo->Id);
                                return false;
                            }
                        }
                        // Nature's Guardian
                        else if (auraSpellInfo->SpellIconID == 2013)
                        {
                            // Check health condition - should drop to less 30% (damage deal after this!)
                            if (!(10*(int32(GetHealth() - damage)) < 3 * GetMaxHealth()))
                                return false;

                             if (pVictim && pVictim->isAlive())
                                 pVictim->getThreatManager().modifyThreatPercent(this, -10);

                            basepoints0 = triggerAmount * GetMaxHealth() / 100;
                            trigger_spell_id = 31616;
                            target = this;
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    // All ok. Check current trigger spell
    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(trigger_spell_id);
    if (triggerEntry == NULL)
    {
        // Not cast unknown spell
        // sLog->outError("Unit::HandleProcTriggerSpell: Spell %u has 0 in EffectTriggered[%d], unhandled custom case?", auraSpellInfo->Id, triggeredByAura->GetEffIndex());
        return false;
    }

    // check if triggering spell can stack with current target's auras (if not - don't proc)
    // don't check if
    // aura is passive (talent's aura)
    // trigger_spell_id's aura is already active (allow to refresh triggered auras)
    // trigger_spell_id's triggeredByAura is already active (for example shaman's shields)
    AuraMap::iterator i, next;
    uint32 aura_id = 0;
    for (i = m_Auras.begin(); i != m_Auras.end(); i = next)
    {
        next = i;
        ++next;
        if (!(*i).second) continue;
            aura_id = (*i).second->GetSpellProto()->Id;
            if (IsPassiveSpell(aura_id) || aura_id == trigger_spell_id || aura_id == triggeredByAura->GetSpellProto()->Id) continue;
        if (sSpellMgr->IsNoStackSpellDueToSpell(trigger_spell_id, (*i).second->GetSpellProto()->Id, ((*i).second->GetCasterGUID() == GetGUID())))
            return false;
    }

    // Custom requirements (not listed in procEx) Warning! damage dealing after this
    // Custom triggered spells
    switch (auraSpellInfo->Id)
    {
        // Persistent Shield (Scarab Brooch trinket)
        // This spell originally trigger 13567 - Dummy Trigger (vs dummy efect)
        case 26467:
        {
            basepoints0 = damage * 15 / 100;
            target = pVictim;
            trigger_spell_id = 26470;
            break;
        }
        // Unyielding Knights (item exploit 29108\29109)
        case 38164:
        {
            if (!pVictim || pVictim->GetEntry() != 19457)  // Proc only if your target is Grillok
                return false;
            break;
        }
        // Cheat Death
        case 28845:
        {
            // When your health drops below 20%
            if (GetHealth() - damage > GetMaxHealth() / 5 || GetHealth() < GetMaxHealth() / 5)
                return false;
            break;
        }
        // Deadly Swiftness (Rank 1)
        case 31255:
        {
            // whenever you deal damage to a target who is below 20% health.
            if (!pVictim || !pVictim->isAlive() || (pVictim->GetHealth() > pVictim->GetMaxHealth() / 5))
                return false;

            target = this;
            trigger_spell_id = 22588;
        }
        // Greater Heal Refund (Avatar Raiment set)
        case 37594:
        {
            if (!pVictim || !pVictim->isAlive())
                return false;

            // Not give if target already have full health
            if (pVictim->GetHealth() == pVictim->GetMaxHealth())
                return false;
            // If your Greater Heal brings the target to full health, you gain $37595s1 mana.
            if (pVictim->GetHealth() + damage < pVictim->GetMaxHealth())
                return false;
            break;
        }
        // Bonus Healing (Crystal Spire of Karabor mace)
        case 40971:
        {
            // If your target is below $s1% health
            if (!pVictim || !pVictim->isAlive() || (pVictim->GetHealth() > pVictim->GetMaxHealth() * triggerAmount / 100))
                return false;
            break;
        }
        // Evasive Maneuvers (Commendation of Kael`thas trinket)
        case 45057:
        {
            // reduce you below $s1% health
            if (GetHealth() - damage > GetMaxHealth() * triggerAmount / 100)
                return false;
            break;
        }
        // Molten Armor / Molten Shields
        case 30482:
        {
            // Always proc on standard melee attack
            if (procFlags & (PROC_FLAG_TAKEN_MELEE_HIT | PROC_FLAG_TAKEN_MELEE_SPELL_HIT))
                break;
            else if (ToPlayer()->HasSpell(11094))
            {
                // 50% chance to proc
                if (rand()%2)
                    break;
                return false;
                break;
            }
            else if (ToPlayer()->HasSpell(13043))
                break;
            else
            {
                return false;
                break;
            }
        }
        case 45054: // Timbal's Focusing Crystal
        {
            if (!isAlive() || !pVictim->isAlive())
                return false;

            break;
        }
    }

    // Costum basepoints/target for exist spell
    // dummy basepoints or other customs
    switch (trigger_spell_id)
    {
        // Cast positive spell on enemy target
        case 7099:  // Curse of Mending
        case 39647: // Curse of Mending
        case 29494: // Temptation
        case 20233: // Improved Lay on Hands (cast on target)
        {
            target = pVictim;
            break;
        }
        // Combo points add triggers (need add combopoint only for main target, and after possible combopoints reset)
        case 15250: // Rogue Setup
        {
            if (!pVictim || pVictim != getVictim())   // applied only for main target
                return false;
            break;                                   // continue normal case
        }
        // Finish movies that add combo
        case 14189: // Seal Fate (Netherblade set)
        case 14157: // Ruthlessness
        {
            if (!pVictim || pVictim == this)
                return false;

            if (procSpell->Id == 26679)
                return false;

            // Need add combopoint AFTER finish movie (or they dropped in finish phase)
            break;
        }
        // Shamanistic Rage triggered spell
        case 30824:
        {
            basepoints0 = int32(GetTotalAttackPowerValue(BASE_ATTACK) * triggerAmount / 100);
            trigger_spell_id = 30824;
            break;
        }
        // Hunter: Expose Weakness
        case 34501:
        {
            basepoints0 = int32(GetStat(STAT_AGILITY) *0.25);
            int32 basepoints1 = int32(GetStat(STAT_AGILITY) *0.25);

            CastCustomSpell(pVictim, trigger_spell_id, &basepoints0, &basepoints1, NULL, true, castItem, triggeredByAura);
            return true;
        }
        // Enlightenment (trigger only from mana cost spells)
        case 35095:
        {
            if (!procSpell || procSpell->powerType != POWER_MANA || procSpell->manaCost == 0 && procSpell->ManaCostPercentage == 0 && procSpell->manaCostPerlevel == 0)
                return false;
            break;
        }
        // Aftermath Self Stun
        case 18118:
        {
            if (pVictim == this)
                return false;
            break;
        }
        // Shadowguard Blackout stun - This has to be hardcoded for now
        case 28378:
        case 28379:
        case 28380:
        case 28381:
        case 28382:
        case 28385:
        {
            if (GetTypeId() == TYPEID_PLAYER && pVictim)
            {
                float chance = 0.0f;
                if (ToPlayer()->HasSpell(15326))        // Rank 5
                    chance = 0.1f;
                else if (ToPlayer()->HasSpell(15325))   // Rank 4
                    chance = 0.08f;
                else if (ToPlayer()->HasSpell(15324))   // Rank 3
                    chance = 0.06f;
                else if (ToPlayer()->HasSpell(15323))   // Rank 2
                    chance = 0.04f;
                else if (ToPlayer()->HasSpell(15268))   // Rank 1
                    chance = 0.02f;
                if (chance > 0.0f)
                {
                    chance = chance * 100.0f;
                    if (chance >= urand(0, 100))
                        CastSpell(pVictim, 15269, true, NULL);
                }
            }
            break;
        }
        // Blackout
        case 15269:
        {
            if (!pVictim || !pVictim->isAlive() || pVictim == this || procSpell == NULL)
                return false;

            // prevent trigger from non damaging spells
            if (procSpell->SpellFamilyFlags & 0x100088000000)
                return false;

            break;
        }
    }

    if (cooldown && GetTypeId() == TYPEID_PLAYER && ToPlayer()->HasSpellCooldown(trigger_spell_id))
        return false;

    // try detect target manually if not set
    if (target == NULL)
       target = !(procFlags & PROC_FLAG_SUCCESSFUL_POSITIVE_SPELL) && IsPositiveSpell(trigger_spell_id) ? this : pVictim;

    // default case
    if (!target || target != this && !target->isAlive())
        return false;

    // apply spell cooldown before casting to prevent triggering spells with SPELL_EFFECT_ADD_EXTRA_ATTACKS if spell has hidden cooldown
    if (cooldown && GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->AddSpellCooldown(trigger_spell_id, 0, time(NULL) + cooldown);

    if (basepoints0)
        CastCustomSpell(target, trigger_spell_id, &basepoints0, NULL, NULL, true, castItem, triggeredByAura);
    else
        CastSpell(target, trigger_spell_id, true, castItem, triggeredByAura);

    return true;
}

bool Unit::HandleOverrideClassScriptAuraProc(Unit *pVictim, Aura *triggeredByAura, SpellEntry const *procSpell, uint32 cooldown)
{
    int32 scriptId = triggeredByAura->GetModifier()->m_miscvalue;

    if (!pVictim || !pVictim->isAlive())
        return false;

    Item* castItem = triggeredByAura->GetCastItemGUID() && GetTypeId() == TYPEID_PLAYER
        ? ToPlayer()->GetItemByGuid(triggeredByAura->GetCastItemGUID()) : NULL;

    uint32 triggered_spell_id = 0;

    switch (scriptId)
    {
        case 836:                                           // Improved Blizzard (Rank 1)
        {
            if (!procSpell || procSpell->SpellVisual != 9487)
                return false;
            triggered_spell_id = 12484;
            break;
        }
        case 988:                                           // Improved Blizzard (Rank 2)
        {
            if (!procSpell || procSpell->SpellVisual != 9487)
                return false;
            triggered_spell_id = 12485;
            break;
        }
        case 989:                                           // Improved Blizzard (Rank 3)
        {
            if (!procSpell || procSpell->SpellVisual != 9487)
                return false;
            triggered_spell_id = 12486;
            break;
        }
        case 4086:                                          // Improved Mend Pet (Rank 1)
        case 4087:                                          // Improved Mend Pet (Rank 2)
        {
            int32 chance = triggeredByAura->GetSpellProto()->EffectBasePoints[triggeredByAura->GetEffIndex()];
            if (!roll_chance_i(chance))
                return false;

            triggered_spell_id = 24406;
            break;
        }
        case 4533:                                          // Dreamwalker Raiment 2 pieces bonus
        {
            // Chance 50%
            if (!roll_chance_i(50))
                return false;

            switch (pVictim->getPowerType())
            {
                case POWER_MANA:   triggered_spell_id = 28722; break;
                case POWER_RAGE:   triggered_spell_id = 28723; break;
                case POWER_ENERGY: triggered_spell_id = 28724; break;
                default:
                    return false;
            }
            break;
        }
        case 4537:                                          // Dreamwalker Raiment 6 pieces bonus
            triggered_spell_id = 28750;                     // Blessing of the Claw
            break;
        case 5497:                                          // Improved Mana Gem
            break;
    }

    // not processed
    if (!triggered_spell_id)
        return false;

    // standard non-dummy case
    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(triggered_spell_id);

    if (!triggerEntry)
    {
        sLog->outError("Unit::HandleOverrideClassScriptAuraProc: Spell %u triggering for class script id %u", triggered_spell_id, scriptId);
        return false;
    }

    if (cooldown && GetTypeId() == TYPEID_PLAYER && ToPlayer()->HasSpellCooldown(triggered_spell_id))
        return false;

    CastSpell(pVictim, triggered_spell_id, true, castItem, triggeredByAura);

    if (cooldown && GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->AddSpellCooldown(triggered_spell_id, 0, time(NULL) + cooldown);

    return true;
}

void Unit::setPowerType(Powers new_powertype)
{
    SetByteValue(UNIT_FIELD_BYTES_0, 3, new_powertype);

    if (GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)this)->m_arenaSpectatorFlags |= (ARENASPEC_POWERTYPE | ARENASPEC_MAXPOWER);
        if (ToPlayer()->GetGroup())
            ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_POWER_TYPE);
    }
    else if (ToCreature()->isPet())
    {
        Pet *pet = ((Pet*)this);
        if (pet->isControlled())
        {
            Unit *owner = GetOwner();
            if (owner && (owner->GetTypeId() == TYPEID_PLAYER) && owner->ToPlayer()->GetGroup())
                owner->ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_POWER_TYPE);
        }
    }

    switch (new_powertype)
    {
        default:
        case POWER_MANA:
            break;
        case POWER_RAGE:
            SetMaxPower(POWER_RAGE, GetCreatePowers(POWER_RAGE));
            SetPower(POWER_RAGE, 0);
            break;
        case POWER_FOCUS:
            SetMaxPower(POWER_FOCUS, GetCreatePowers(POWER_FOCUS));
            SetPower(POWER_FOCUS, GetCreatePowers(POWER_FOCUS));
            break;
        case POWER_ENERGY:
            SetMaxPower(POWER_ENERGY, GetCreatePowers(POWER_ENERGY));
            SetPower(POWER_ENERGY, 0);
            break;
        case POWER_HAPPINESS:
            SetMaxPower(POWER_HAPPINESS, GetCreatePowers(POWER_HAPPINESS));
            SetPower(POWER_HAPPINESS, GetCreatePowers(POWER_HAPPINESS));
            break;
    }
}

FactionTemplateEntry const* Unit::getFactionTemplateEntry() const
{
    FactionTemplateEntry const* entry = sFactionTemplateStore.LookupEntry(getFaction());
    if (!entry)
    {
        static uint64 guid = 0;                             // prevent repeating spam same faction problem

        if (GetGUID() != guid)
        {
            if (GetTypeId() == TYPEID_PLAYER)
                sLog->outError("Player %s has invalid faction (faction template id) #%u", ToPlayer()->GetName(), getFaction());
            else
                sLog->outError("Creature (template id: %u) has invalid faction (faction template id) #%u", ToCreature()->GetCreatureTemplate()->Entry, getFaction());

            guid = GetGUID();
        }
    }
    return entry;
}

bool Unit::IsHostileTo(Unit const* unit) const
{
    // always non-hostile to self
    if (unit == this)
        return false;

    // always non-hostile to GM in GM mode
    if (unit->GetTypeId() == TYPEID_PLAYER && unit->ToPlayer()->isGameMaster())
        return false;

    // always hostile to enemy
    if (getVictim() == unit || unit->getVictim() == this)
        return true;

    // test pet/charm masters instead pers/charmeds
    Unit const* testerOwner = GetCharmerOrOwner();
    Unit const* targetOwner = unit->GetCharmerOrOwner();

    // always hostile to owner's enemy
    if (testerOwner && (testerOwner->getVictim() == unit || unit->getVictim() == testerOwner))
        return true;

    // always hostile to enemy owner
    if (targetOwner && (getVictim() == targetOwner || targetOwner->getVictim() == this))
        return true;

    // always hostile to owner of owner's enemy
    if (testerOwner && targetOwner && (testerOwner->getVictim() == targetOwner || targetOwner->getVictim() == testerOwner))
        return true;

    Unit const* tester = testerOwner ? testerOwner : this;
    Unit const* target = targetOwner ? targetOwner : unit;

    // always non-hostile to target with common owner, or to owner/pet
    if (tester == target)
        return false;

    // special cases (Duel, etc)
    if (tester->GetTypeId() == TYPEID_PLAYER && target->GetTypeId() == TYPEID_PLAYER)
    {
        Player const* pTester = (Player const*)tester;
        Player const* pTarget = (Player const*)target;

        // Duel
        if (pTester->duel && pTester->duel->opponent == pTarget && pTester->duel->startTime != 0)
            return true;

        // Group
        if (pTester->GetGroup() && pTester->GetGroup() == pTarget->GetGroup())
            return false;

        // Sanctuary
        if (pTarget->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY) && pTester->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY))
            return false;

        // Neutral Dueling Zone
        if (pTester->GetZoneId() == 2037 && pTarget->GetZoneId() == 2037)
            return false;
        
        // PvP FFA state
        if (pTester->IsFFAPvP() && pTarget->IsFFAPvP())
            return true;

        //= PvP states
        // Green/Blue (can't attack)
        if (pTester->GetTeam() == pTarget->GetTeam())
            return false;

        // Red (can attack) if true, Blue/Yellow (can't attack) in another case
        return pTester->IsPvP() && pTarget->IsPvP();
    }

    // faction base cases
    FactionTemplateEntry const*tester_faction = tester->getFactionTemplateEntry();
    FactionTemplateEntry const*target_faction = target->getFactionTemplateEntry();
    if (!tester_faction || !target_faction)
        return false;

    if (target->isAttackingPlayer() && tester->IsContestedGuard())
        return true;

    // PvC forced reaction and reputation case
    if (tester->GetTypeId() == TYPEID_PLAYER)
    {
        // forced reaction
        ForcedReactions::const_iterator forceItr = tester->ToPlayer()->m_forcedReactions.find(target_faction->faction);
        if (forceItr != tester->ToPlayer()->m_forcedReactions.end())
            return forceItr->second <= REP_HOSTILE;

        // if faction have reputation then hostile state for tester at 100% dependent from at_war state
        if (FactionEntry const* raw_target_faction = sFactionStore.LookupEntry(target_faction->faction))
            if (raw_target_faction->reputationListID >=0)
                if (FactionState const* factionState = tester->ToPlayer()->GetFactionState(raw_target_faction))
                    return (factionState->Flags & FACTION_FLAG_AT_WAR);
    }
    // CvP forced reaction and reputation case
    else if (target->GetTypeId() == TYPEID_PLAYER)
    {
        // forced reaction
        ForcedReactions::const_iterator forceItr = target->ToPlayer()->m_forcedReactions.find(tester_faction->faction);
        if (forceItr != target->ToPlayer()->m_forcedReactions.end())
            return forceItr->second <= REP_HOSTILE;

        // apply reputation state
        FactionEntry const* raw_tester_faction = sFactionStore.LookupEntry(tester_faction->faction);
        if (raw_tester_faction && raw_tester_faction->reputationListID >=0)
            return target->ToPlayer()->GetReputationRank(raw_tester_faction) <= REP_HOSTILE;
    }

    // common faction based case (CvC, PvC, CvP)
    return tester_faction->IsHostileTo(*target_faction);
}

bool Unit::IsFriendlyTo(Unit const* unit) const
{
    // always friendly to self
    if (unit == this)
        return true;

    // always friendly to GM in GM mode
    if (unit->GetTypeId() == TYPEID_PLAYER && unit->ToPlayer()->isGameMaster())
        return true;

    // always non-friendly to enemy
    if (getVictim() == unit || unit->getVictim() == this)
        return false;

    // test pet/charm masters instead pers/charmeds
    Unit const* testerOwner = GetCharmerOrOwner();
    Unit const* targetOwner = unit->GetCharmerOrOwner();

    // always non-friendly to owner's enemy
    if (testerOwner && (testerOwner->getVictim() == unit || unit->getVictim() == testerOwner))
        return false;

    // always non-friendly to enemy owner
    if (targetOwner && (getVictim() == targetOwner || targetOwner->getVictim() == this))
        return false;

    // always non-friendly to owner of owner's enemy
    if (testerOwner && targetOwner && (testerOwner->getVictim() == targetOwner || targetOwner->getVictim() == testerOwner))
        return false;

    Unit const* tester = testerOwner ? testerOwner : this;
    Unit const* target = targetOwner ? targetOwner : unit;

    // always friendly to target with common owner, or to owner/pet
    if (tester == target)
        return true;

    // special cases (Duel)
    if (tester->GetTypeId() == TYPEID_PLAYER && target->GetTypeId() == TYPEID_PLAYER)
    {
        Player const* pTester = (Player const*)tester;
        Player const* pTarget = (Player const*)target;

        // Duel
        if (pTester->duel && pTester->duel->opponent == target && pTester->duel->startTime != 0)
            return false;

        // Group
        if (pTester->GetGroup() && pTester->GetGroup() == pTarget->GetGroup())
            return true;

        // Sanctuary
        if (pTarget->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY) && pTester->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY))
            return true;

        // Neutral Dueling Zone
        if (pTester->GetZoneId() == 2037 && pTarget->GetZoneId() == 2037)
            return true;

        // PvP FFA state
        if (pTester->IsFFAPvP() && pTarget->IsFFAPvP())
            return false;

        //= PvP states
        // Green/Blue (non-attackable)
        if (pTester->GetTeam() == pTarget->GetTeam())
            return true;

        // Blue (friendly/non-attackable) if not PVP, or Yellow/Red in another case (attackable)
        return !pTarget->IsPvP();
    }

    // faction base cases
    FactionTemplateEntry const*tester_faction = tester->getFactionTemplateEntry();
    FactionTemplateEntry const*target_faction = target->getFactionTemplateEntry();
    if (!tester_faction || !target_faction)
        return false;

    if (target->isAttackingPlayer() && tester->IsContestedGuard())
        return false;

    // PvC forced reaction and reputation case
    if (tester->GetTypeId() == TYPEID_PLAYER)
    {
        // forced reaction
        ForcedReactions::const_iterator forceItr = tester->ToPlayer()->m_forcedReactions.find(target_faction->faction);
        if (forceItr != tester->ToPlayer()->m_forcedReactions.end())
            return forceItr->second >= REP_FRIENDLY;

        // if faction have reputation then friendly state for tester at 100% dependent from at_war state
        if (FactionEntry const* raw_target_faction = sFactionStore.LookupEntry(target_faction->faction))
            if (raw_target_faction->reputationListID >=0)
                if (FactionState const* FactionState = tester->ToPlayer()->GetFactionState(raw_target_faction))
                    return !(FactionState->Flags & FACTION_FLAG_AT_WAR);
    }
    // CvP forced reaction and reputation case
    else if (target->GetTypeId() == TYPEID_PLAYER)
    {
        // forced reaction
        ForcedReactions::const_iterator forceItr = target->ToPlayer()->m_forcedReactions.find(tester_faction->faction);
        if (forceItr != target->ToPlayer()->m_forcedReactions.end())
            return forceItr->second >= REP_FRIENDLY;

        // apply reputation state
        if (FactionEntry const* raw_tester_faction = sFactionStore.LookupEntry(tester_faction->faction))
            if (raw_tester_faction->reputationListID >=0)
                return target->ToPlayer()->GetReputationRank(raw_tester_faction) >= REP_FRIENDLY;
    }

    // common faction based case (CvC, PvC, CvP)
    return tester_faction->IsFriendlyTo(*target_faction);
}

bool Unit::IsHostileToPlayers() const
{
    FactionTemplateEntry const *my_faction = getFactionTemplateEntry();
    if (!my_faction)
        return false;

    FactionEntry const *raw_faction = sFactionStore.LookupEntry(my_faction->faction);
    if (raw_faction && raw_faction->reputationListID >= 0)
        return false;

    return my_faction->IsHostileToPlayers();
}

bool Unit::IsNeutralToAll() const
{
    FactionTemplateEntry const *my_faction = getFactionTemplateEntry();
    if (!my_faction)
        return true;

    FactionEntry const *raw_faction = sFactionStore.LookupEntry(my_faction->faction);
    if (raw_faction && raw_faction->reputationListID >= 0)
        return false;

    return my_faction->IsNeutralToAll();
}

bool Unit::Attack(Unit *victim, bool meleeAttack)
{
    if (!victim || victim == this)
        return false;

    // dead units can neither attack nor be attacked
    if (!isAlive() || !victim->IsInWorld() || !victim->isAlive())
        return false;

    // player cannot attack in mount state
    if (GetTypeId() == TYPEID_PLAYER && IsMounted())
        return false;

    // nobody can attack GM in GM-mode
    if (victim->GetTypeId() == TYPEID_PLAYER)
    {
        if (victim->ToPlayer()->isGameMaster())
            return false;
    }
    else
    {
        if (victim->ToCreature()->IsInEvadeMode())
            return false;
    }

    // remove SPELL_AURA_MOD_UNATTACKABLE at attack (in case non-interruptible spells stun aura applied also that not let attack)
    if (HasAuraType(SPELL_AURA_MOD_UNATTACKABLE))
        RemoveSpellsCausingAura(SPELL_AURA_MOD_UNATTACKABLE);

    if (m_attacking)
    {
        if (m_attacking == victim)
        {
            // switch to melee attack from ranged/magic
            if (meleeAttack && !hasUnitState(UNIT_STAT_MELEE_ATTACKING))
            {
                addUnitState(UNIT_STAT_MELEE_ATTACKING);
                SendMeleeAttackStart(victim);
                return true;
            }
            return false;
        }
        AttackStop();
    }

    // Set our target
    SetUInt64Value(UNIT_FIELD_TARGET, victim->GetGUID());

    if (meleeAttack)
        addUnitState(UNIT_STAT_MELEE_ATTACKING);

    m_attacking = victim;
    m_attacking->_addAttacker(this);

    if (GetTypeId() == TYPEID_UNIT && !ToCreature()->isPet())
    {
        // should not let player enter combat by right clicking target
        SetInCombatWith(victim);
        if (victim->GetTypeId() == TYPEID_PLAYER)
            victim->SetInCombatWith(this);
        AddThreat(victim, 0.0f);

        WorldPacket data(SMSG_AI_REACTION, 12);
        data << uint64(GetGUID());
        data << uint32(AI_REACTION_AGGRO);                  // Aggro sound
        ((WorldObject*)this)->SendMessageToSet(&data, true);

        ToCreature()->CallAssistance();
        SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
    }

    // delay offhand weapon attack to next attack time
    if (haveOffhandWeapon())
        resetAttackTimer(OFF_ATTACK);

    if (meleeAttack)
        SendMeleeAttackStart(victim);

    // Let the pet know we've started attacking someting. Handles melee attacks only
    // Spells such as auto-shot and others handled in WorldSession::HandleCastSpellOpcode
    if (this->GetTypeId() == TYPEID_PLAYER)
    {
        Pet* playerPet = this->ToPlayer()->GetPet();

        if (playerPet && playerPet->isAlive())
            playerPet->AI()->OwnerAttacked(victim);
    }

    return true;
}

bool Unit::AttackStop()
{
    if (!m_attacking)
        return false;

    Unit* victim = m_attacking;

    m_attacking->_removeAttacker(this);
    m_attacking = NULL;

    // Clear our target
    SetUInt64Value(UNIT_FIELD_TARGET, 0);

    clearUnitState(UNIT_STAT_MELEE_ATTACKING);

    InterruptSpell(CURRENT_MELEE_SPELL);

    if (GetTypeId() == TYPEID_UNIT)
    {
        // reset call assistance
        ToCreature()->SetNoCallAssistance(false);
        ToCreature()->SetNoSearchAssistance(false);
    }

    SendMeleeAttackStop(victim);

    return true;
}

void Unit::CombatStop(bool cast)
{
    if (cast && IsNonMeleeSpellCasted(false))
        InterruptNonMeleeSpells(false);

    AttackStop();
    RemoveAllAttackers();
    if (GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->SendAttackSwingCancelAttack();     // melee and ranged forced attack cancel
    ClearInCombat();
}

void Unit::CombatStopWithPets(bool cast)
{
    CombatStop(cast);

    for (ControlList::const_iterator itr = m_Controlled.begin(); itr != m_Controlled.end(); ++itr)
        (*itr)->CombatStop(cast);
}

bool Unit::isAttackingPlayer() const
{
    if (hasUnitState(UNIT_STAT_ATTACK_PLAYER))
        return true;

    for (ControlList::const_iterator itr = m_Controlled.begin(); itr != m_Controlled.end(); ++itr)
        if ((*itr)->isAttackingPlayer())
            return true;

    for (uint8 i = 0; i < MAX_SUMMON_SLOT; ++i)
        if (m_SummonSlot[i])
            if (Creature *summon = GetMap()->GetCreature(m_SummonSlot[i]))
                if (summon->isAttackingPlayer())
                    return true;

    return false;
}

void Unit::RemoveAllAttackers()
{
    while (!m_attackers.empty())
    {
        AttackerSet::iterator iter = m_attackers.begin();
        if (!(*iter)->AttackStop())
        {
            sLog->outError("WORLD: Unit has an attacker that isn't attacking it!");
            m_attackers.erase(iter);
        }
    }
}

void Unit::ModifyAuraState(AuraState flag, bool apply)
{
    if (apply)
    {
        if (!HasFlag(UNIT_FIELD_AURASTATE, 1<<(flag-1)))
        {
            SetFlag(UNIT_FIELD_AURASTATE, 1<<(flag-1));
            if (GetTypeId() == TYPEID_PLAYER)
            {
                const PlayerSpellMap& sp_list = ToPlayer()->GetSpellMap();
                for (PlayerSpellMap::const_iterator itr = sp_list.begin(); itr != sp_list.end(); ++itr)
                {
                    if (itr->second->state == PLAYERSPELL_REMOVED) continue;
                    SpellEntry const *spellInfo = sSpellStore.LookupEntry(itr->first);
                    if (!spellInfo || !IsPassiveSpell(itr->first)) continue;
                    if (spellInfo->CasterAuraState == flag)
                        CastSpell(this, itr->first, true, NULL);
                }
            }
        }
    }
    else
    {
        if (HasFlag(UNIT_FIELD_AURASTATE, 1<<(flag-1)))
        {
            RemoveFlag(UNIT_FIELD_AURASTATE, 1<<(flag-1));
            Unit::AuraMap& tAuras = GetAuras();
            for (Unit::AuraMap::iterator itr = tAuras.begin(); itr != tAuras.end();)
            {
                SpellEntry const* spellProto = (*itr).second->GetSpellProto();
                if (spellProto->CasterAuraState == flag)
                {
                    // exceptions (applied at state but not removed at state change)
                    // Rampage
                    if (spellProto->SpellIconID == 2006 && spellProto->SpellFamilyName == SPELLFAMILY_WARRIOR && spellProto->SpellFamilyFlags == 0x100000)
                    {
                        ++itr;
                        continue;
                    }

                    RemoveAura(itr);
                }
                else
                    ++itr;
            }
        }
    }
}

Unit *Unit::GetOwner() const
{
    if (uint64 ownerid = GetOwnerGUID())
        return ObjectAccessor::GetUnit(*this, ownerid);
    return NULL;
}

Unit *Unit::GetCharmer() const
{
    if (uint64 charmerid = GetCharmerGUID())
        return ObjectAccessor::GetUnit(*this, charmerid);
    return NULL;
}

Player* Unit::GetCharmerOrOwnerPlayerOrPlayerItself() const
{
    uint64 guid = GetCharmerOrOwnerGUID();
    if (IS_PLAYER_GUID(guid))
        return ObjectAccessor::GetPlayer(*this, guid);

    return GetTypeId() == TYPEID_PLAYER ? (Player*)this : NULL;
}

Minion *Unit::GetFirstMinion() const
{
    if (uint64 pet_guid = GetMinionGUID())
    {
        if (Creature* pet = ObjectAccessor::GetCreatureOrPet(*this, pet_guid))
            if (pet->HasSummonMask(SUMMON_MASK_MINION))
                return (Minion*)pet;

        sLog->outError("Unit::GetFirstMinion: Minion %u not exist.", GUID_LOPART(pet_guid));
        const_cast<Unit*>(this)->SetMinionGUID(0);
    }

    return NULL;
}

Guardian* Unit::GetGuardianPet() const
{
    if (uint64 pet_guid = GetPetGUID())
    {
        if (Creature* pet = ObjectAccessor::GetCreatureOrPet(*this, pet_guid))
            if (pet->HasSummonMask(SUMMON_MASK_GUARDIAN))
                return (Guardian*)pet;

        sLog->outError("Unit::GetGuardianPet: Guardian %u not exist.", GUID_LOPART(pet_guid));
        const_cast<Unit*>(this)->SetPetGUID(0);
    }

    return NULL;
}

Unit* Unit::GetCharm() const
{
    if (uint64 charm_guid = GetCharmGUID())
    {
        if (Unit* pet = ObjectAccessor::GetUnit(*this, charm_guid))
            return pet;

        sLog->outError("Unit::GetCharm: Charmed creature %u not exist.", GUID_LOPART(charm_guid));
        const_cast<Unit*>(this)->SetUInt64Value(UNIT_FIELD_CHARM, 0);
    }

    return NULL;
}

void Unit::SetMinion(Minion *minion, bool apply)
{
    sLog->outDebug("SetMinion %u for %u, apply %u", minion->GetEntry(), GetEntry(), apply);

    if (apply)
    {
        if (!minion->AddUInt64Value(UNIT_FIELD_SUMMONEDBY, GetGUID()))
        {
            sLog->outCrash("SetMinion: Minion %u is not the minion of owner %u", minion->GetEntry(), GetEntry());
            return;
        }

        m_Controlled.insert(minion);

        if (GetTypeId() == TYPEID_PLAYER)
        {
            minion->m_ControlledByPlayer = true;
            minion->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);
        }

        // Can only have one pet. If a new one is summoned, dismiss the old one.
        if (minion->isPet() || minion->m_Properties && minion->m_Properties->Category == SUMMON_CATEGORY_PET)
        {
            if (Guardian* oldPet = GetGuardianPet())
            {
                if (oldPet != minion && (oldPet->isPet() || minion->isPet() || oldPet->GetEntry() != minion->GetEntry()))
                {
                    // remove existing minion pet
                    if (oldPet->isPet())
                        ((Pet*)oldPet)->Remove(PET_SAVE_AS_CURRENT);
                    else
                        oldPet->UnSummon();
                    SetPetGUID(minion->GetGUID());
                    SetMinionGUID(0);
                }
            }
            else
            {
                SetPetGUID(minion->GetGUID());
                SetMinionGUID(0);
            }
        }

        if (minion->HasSummonMask(SUMMON_MASK_CONTROLABLE_GUARDIAN))
            AddUInt64Value(UNIT_FIELD_SUMMON, minion->GetGUID());

        // PvP, FFAPvP
        minion->SetByteValue(UNIT_FIELD_BYTES_2, 1, GetByteValue(UNIT_FIELD_BYTES_2, 1));
    }
    else
    {
        if (minion->GetOwnerGUID() != GetGUID())
        {
            sLog->outCrash("SetMinion: Minion %u is not the minion of owner %u", minion->GetEntry(), GetEntry());
            return;
        }

        m_Controlled.erase(minion);

        if (minion->isPet() || minion->m_Properties && minion->m_Properties->Category == SUMMON_CATEGORY_PET)
        {
            if (GetPetGUID() == minion->GetGUID())
                SetPetGUID(0);
        }
        else if (minion->isTotem())
        {
            // All summoned by totem minions must disappear when it is removed.
            if (const SpellEntry* spInfo = sSpellStore.LookupEntry(((Totem*)minion)->GetSpell()))
                for (int i = 0; i < MAX_SPELL_EFFECTS; ++i)
                {
                    if (spInfo->Effect[i] != SPELL_EFFECT_SUMMON)
                        continue;

                    this->RemoveAllMinionsByEntry(spInfo->EffectMiscValue[i]);
                }
        }

        if (RemoveUInt64Value(UNIT_FIELD_SUMMON, minion->GetGUID()))
        {
            //Check if there is another minion
            for (ControlList::iterator itr = m_Controlled.begin(); itr != m_Controlled.end(); ++itr)
            {
                // do not use this check, creature do not have charm guid
                //if (GetCharmGUID() == (*itr)->GetGUID())
                if (GetGUID() == (*itr)->GetCharmerGUID())
                    continue;

                ASSERT((*itr)->GetOwnerGUID() == GetGUID());
                ASSERT((*itr)->GetTypeId() == TYPEID_UNIT);

                if (!(*itr)->ToCreature()->HasSummonMask(SUMMON_MASK_CONTROLABLE_GUARDIAN))
                    continue;

                if (AddUInt64Value(UNIT_FIELD_SUMMON, (*itr)->GetGUID()))
                {
                    //show another pet bar if there is no charm bar
                    if (GetTypeId() == TYPEID_PLAYER && !GetCharmGUID())
                    {
                        if ((*itr)->ToCreature()->isPet())
                            ToPlayer()->PetSpellInitialize();
                        else
                            ToPlayer()->CharmSpellInitialize();
                    }
                }
                break;
            }
        }
    }
}

void Unit::RemoveAllMinionsByEntry(uint32 entry)
{
    for (Unit::ControlList::iterator itr = m_Controlled.begin(); itr != m_Controlled.end();)
    {
        Unit *unit = *itr;
        ++itr;
        if (unit->GetEntry() == entry && unit->GetTypeId() == TYPEID_UNIT
            && unit->ToCreature()->isSummon()) // minion, actually
            ((TempSummon*)unit)->UnSummon();
        // i think this is safe because i have never heard that a despawned minion will trigger a same minion
    }
}

void Unit::SetCharm(Unit* charm, bool apply)
{
    if (apply)
    {
        if (GetTypeId() == TYPEID_PLAYER)
        {
            if (!AddUInt64Value(UNIT_FIELD_CHARM, charm->GetGUID()))
                sLog->outCrash("Player %s is trying to charm unit %u, but it already has a charmed unit %u", GetName(), charm->GetEntry(), GetCharmGUID());

            charm->m_ControlledByPlayer = true;
            // TODO: maybe we can use this flag to check if controlled by player
            charm->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);
        }
        else
            charm->m_ControlledByPlayer = false;

        // PvP, FFAPvP
        charm->SetByteValue(UNIT_FIELD_BYTES_2, 1, GetByteValue(UNIT_FIELD_BYTES_2, 1));

        if (!charm->AddUInt64Value(UNIT_FIELD_CHARMEDBY, GetGUID()))
            sLog->outCrash("Unit %u is being charmed, but it already has a charmer %u", charm->GetEntry(), charm->GetCharmerGUID());

        if (charm->HasUnitMovementFlag(MOVEFLAG_WALK_MODE))
        {
            charm->RemoveUnitMovementFlag(MOVEFLAG_WALK_MODE);
            charm->SendMovementFlagUpdate();
        }

        m_Controlled.insert(charm);
    }
    else
    {
        if (GetTypeId() == TYPEID_PLAYER)
        {
            if (!RemoveUInt64Value(UNIT_FIELD_CHARM, charm->GetGUID()))
                sLog->outCrash("Player %s is trying to uncharm unit %u, but it has another charmed unit %u", GetName(), charm->GetEntry(), GetCharmGUID());
        }

        if (!charm->RemoveUInt64Value(UNIT_FIELD_CHARMEDBY, GetGUID()))
            sLog->outCrash("Unit %u is being uncharmed, but it has another charmer %u", charm->GetEntry(), charm->GetCharmerGUID());

        if (charm->GetTypeId() == TYPEID_PLAYER)
        {
            charm->m_ControlledByPlayer = true;
            charm->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);
            charm->ToPlayer()->UpdatePvPState();
        }
        else if (Player* player = charm->GetCharmerOrOwnerPlayerOrPlayerItself())
        {
            charm->m_ControlledByPlayer = true;
            charm->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);
            charm->SetByteValue(UNIT_FIELD_BYTES_2, 1, player->GetByteValue(UNIT_FIELD_BYTES_2, 1));
        }
        else
        {
            charm->m_ControlledByPlayer = false;
            charm->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);
            charm->SetByteValue(UNIT_FIELD_BYTES_2, 1, 0);
        }

        if (charm->GetTypeId() == TYPEID_PLAYER
            || !charm->ToCreature()->HasSummonMask(SUMMON_MASK_MINION)
            || charm->GetOwnerGUID() != GetGUID())
            m_Controlled.erase(charm);
    }
}

Unit* Unit::GetFirstControlled() const
{
    Unit *unit = GetCharm();
    if (!unit)
    {
        if (uint64 guid = GetUInt64Value(UNIT_FIELD_SUMMON))
            unit = ObjectAccessor::GetUnit(*this, guid);
    }
    return unit;
}

void Unit::RemoveAllControlled()
{
    // possessed pet
    if (GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->StopCastingCharm();

    while (!m_Controlled.empty())
    {
        Unit *target = *m_Controlled.begin();
        m_Controlled.erase(m_Controlled.begin());
        if (target->GetCharmerGUID() == GetGUID())
            target->RemoveCharmAuras();
        // robinsch: ///@todo: Should creature unsummon their current tempsummons? https://github.com/robinsch/TBCPvP/issues/45
        else if (GetTypeId() == TYPEID_PLAYER && target->GetOwnerGUID() == GetGUID() && target->GetTypeId() == TYPEID_UNIT && ((Creature*)target)->HasSummonMask(SUMMON_MASK_SUMMON))
            ((TempSummon*)target)->UnSummon();
        else
            sLog->outError("Unit %u is trying to release unit %u which is neither charmed nor owned by it", GetEntry(), target->GetEntry());
    }
    if (GetPetGUID() != GetUInt64Value(UNIT_FIELD_SUMMON))
        sLog->outCrash("Unit %u is not able to release its summon %u", GetEntry(), GetPetGUID());
    if (GetCharmGUID())
        sLog->outCrash("Unit %u is not able to release its charm %u", GetEntry(), GetCharmGUID());
}

void Unit::AddPlayerToVision(Player* plr)
{
    if (m_sharedVision.empty())
    {
        setActive(true);
        SetWorldObject(true);
    }
    m_sharedVision.push_back(plr);
}

void Unit::RemovePlayerFromVision(Player* plr)
{
    m_sharedVision.remove(plr);
    if (m_sharedVision.empty())
    {
        setActive(false);
        SetWorldObject(false);
    }
}

void Unit::RemoveBindSightAuras()
{
    RemoveSpellsCausingAura(SPELL_AURA_BIND_SIGHT);
}

void Unit::RemoveCharmAuras()
{
    RemoveSpellsCausingAura(SPELL_AURA_MOD_CHARM);
    RemoveSpellsCausingAura(SPELL_AURA_MOD_POSSESS_PET);
    RemoveSpellsCausingAura(SPELL_AURA_MOD_POSSESS);
}

void Unit::UnsummonAllTotems()
{
    for (uint8 i = 0; i < MAX_SUMMON_SLOT; ++i)
    {
        if (!m_SummonSlot[i])
            continue;

        if (Creature *OldTotem = GetMap()->GetCreature(m_SummonSlot[i]))
            if (OldTotem->isSummon())
                ((TempSummon*)OldTotem)->UnSummon();
    }
}

void Unit::SendHealSpellLog(Unit *pVictim, uint32 SpellID, uint32 Damage, bool critical)
{
    // we guess size
    WorldPacket data(SMSG_SPELLHEALLOG, (8+8+4+4+1));
    data << pVictim->GetPackGUID();
    data << GetPackGUID();
    data << uint32(SpellID);
    data << uint32(Damage);
    data << uint8(critical ? 1 : 0);
    data << uint8(0);                                       // unused in client?
    SendMessageToSet(&data, true);
}

void Unit::SendEnergizeSpellLog(Unit *pVictim, uint32 SpellID, uint32 Damage, Powers powertype)
{
    WorldPacket data(SMSG_SPELLENERGIZELOG, (8+8+4+4+4+1));
    data << pVictim->GetPackGUID();
    data << GetPackGUID();
    data << uint32(SpellID);
    data << uint32(powertype);
    data << uint32(Damage);
    SendMessageToSet(&data, true);
}

/** Calculate spell coefficents and level penalties for spell/melee damage or heal
 *
 * this is the caster of the spell/ melee attacker
 * @param spellProto SpellEntry of the used spell
 * @param total current value onto which the Bonus and level penalty will be calculated
 * @param benefit additional benefit from ie spellpower-auras
 * @param ap_benefit additional melee attackpower benefit from auras
 * @param damagetype what kind of damage
 * @param donePart calculate for done or taken
 */
int32 Unit::SpellBonusWithCoeffs(SpellEntry const* spellProto, int32 total, int32 benefit, int32 ap_benefit,  DamageEffectType damagetype, bool donePart)
{
    // Distribute Damage over multiple effects, reduce by AoE
    float coeff = 1.0f;

    // Not apply this to creature casted spells
    if (GetTypeId() == TYPEID_UNIT && !((Creature*)this)->isPet())
        coeff = 1.0f;

    // Check for table values
    else if (SpellBonusEntry const* bonus = sSpellMgr->GetSpellBonusData(spellProto->Id))
    {
        coeff = damagetype == DOT ? bonus->dot_damage : bonus->direct_damage;

        // apply ap bonus at done part calculation only (it flat total mod so common with taken)
        if (donePart && (bonus->ap_bonus || bonus->ap_dot_bonus))
        {
            float ap_bonus = damagetype == DOT ? bonus->ap_dot_bonus : bonus->ap_bonus;
            total += int32(ap_bonus * (GetTotalAttackPowerValue(IsSpellRequiresRangedAP(spellProto) ? RANGED_ATTACK : BASE_ATTACK) + ap_benefit));
        }
    }
    // Default calculation
    else if (benefit)
        coeff = CalculateDefaultCoefficient(spellProto, damagetype);

    if (benefit)
    {
        float LvlPenalty = CalculateLevelPenalty(spellProto);

        // Spellmod SpellDamage
        if (Player* modOwner = GetSpellModOwner())
        {
            coeff *= 100.0f;
            modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_SPELL_BONUS_DAMAGE, coeff);
            coeff /= 100.0f;
        }

        total += int32(benefit * coeff * LvlPenalty);
    }

    return total;
};

/**
 * Calculates caster part of spell damage bonuses,
 * also includes different bonuses dependent from target auras
 */
uint32 Unit::SpellDamageBonusDone(Unit *pVictim, SpellEntry const *spellProto, uint32 pdamage, DamageEffectType damagetype)
{
    if (!spellProto || !pVictim || damagetype == DIRECT_DAMAGE)
        return pdamage;

    // For totems get damage bonus from owner (statue isn't totem in fact)
    if (GetTypeId() == TYPEID_UNIT && ((Creature*)this)->isTotem())
    {
        if (Unit* owner = GetOwner())
            return owner->SpellDamageBonusDone(pVictim, spellProto, pdamage, damagetype);
    }

    uint32 creatureTypeMask = pVictim->GetCreatureTypeMask();
    float DoneTotalMod = 1.0f;
    int32 DoneTotal = 0;

    // Implemented in PeriodicTick()
    if (damagetype != DOT)
    {
        // ..done
        AuraList const& mModDamagePercentDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_PERCENT_DONE);
        for (AuraList::const_iterator i = mModDamagePercentDone.begin(); i != mModDamagePercentDone.end(); ++i)
        {
            if (((*i)->GetModifier()->m_miscvalue & GetSpellSchoolMask(spellProto)) &&
                (*i)->GetSpellProto()->EquippedItemClass == -1 &&
                // -1 == any item class (not wand then)
                (*i)->GetSpellProto()->EquippedItemInventoryTypeMask == 0)
                // 0 == any inventory type (not wand then)
            {
                DoneTotalMod *= ((*i)->GetModifierValue() + 100.0f) / 100.0f;
            }
        }
    }

    // Add flat bonus from spell damage versus
    DoneTotal += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_DAMAGE_DONE_VERSUS, creatureTypeMask);

    // Add pct bonus from spell damage versus
    DoneTotalMod *= GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_DAMAGE_DONE_VERSUS, creatureTypeMask);

    // Add flat bonus from spell damage creature
    DoneTotal += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_DAMAGE_DONE_CREATURE, creatureTypeMask);

    // done scripted mod (take it from owner)
    Unit* owner = GetOwner();
    if (!owner)
        owner = this;

    AuraList const& mOverrideClassScript = GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
    for (AuraList::const_iterator i = mOverrideClassScript.begin(); i != mOverrideClassScript.end(); ++i)
    {
        switch ((*i)->GetModifier()->m_miscvalue)
        {
            // Molten Fury
            case 4920:
            case 4919:
                if (pVictim->HasAuraState(AURA_STATE_HEALTHLESS_20_PERCENT))
                    DoneTotal *= (100.0f + (*i)->GetModifier()->m_amount) / 100.0f;
                break;
            // Soul Siphon
            case 4992:
            case 4993:
            {
                // effect 1 m_amount
                int32 maxPercent = (*i)->GetModifier()->m_amount;
                // effect 0 m_amount
                int32 stepPercent = CalculateSpellDamage(this, (*i)->GetSpellProto(), EFFECT_INDEX_0);
                // count affliction effects and calc additional damage in percentage
                int32 modPercent = 0;
                Unit::AuraMap const& victimAuras = GetAuras();
                for (Unit::AuraMap::const_iterator itr = victimAuras.begin(); itr != victimAuras.end(); ++itr)
                {
                    SpellEntry const* m_spell = itr->second->GetSpellProto();
                    if (m_spell->SpellFamilyName != SPELLFAMILY_WARLOCK || !(m_spell->SpellFamilyFlags & UI64LIT(0x0000871B804CC41A)))
                        continue;
                    modPercent += stepPercent * itr->second->GetStackAmount();
                    if (modPercent >= maxPercent)
                    {
                        modPercent = maxPercent;
                        break;
                    }
                }
                DoneTotalMod *= (modPercent + 100.0f) / 100.0f;
                break;
            }
            // Starfire Bonus
            case 5481:
            {
                if (pVictim->HasAuraTypeWithFamilyFlags(SPELL_AURA_PERIODIC_DAMAGE, SPELLFAMILY_DRUID, 0x0000000000200002LL))
                    DoneTotalMod *= ((*i)->GetModifier()->m_amount + 100.0f) / 100.0f;
                break;
            }
            case 4418: // Increased Shock Damage
            case 4554: // Increased Lightning Damage
            case 4555: // Improved Moonfire
            case 5142: // Increased Lightning Damage
            case 5147: // Improved Consecration
            case 5148: // Idol of the Shooting Star
            case 6008: // Increased Lightning Damage / Totem of Hex
            {
                DoneTotal += (*i)->GetModifier()->m_amount;
                break;
            }
        }
    }

    // Custom scripted damage
    switch (spellProto->SpellFamilyName)
    {
        case SPELLFAMILY_MAGE:
        {
            // Ice Lance
            if (spellProto->SpellIconID == 186)
            {
                if (pVictim->isFrozen())
                    DoneTotalMod *= 3.0f;
            }
            break;
        }
        default:
            break;
    }

    // Done fixed damage bonus auras
    int32 DoneAdvertisedBenefit = SpellBaseDamageBonusDone(GetSpellSchoolMask(spellProto));

    // Pets just add their bonus damage to their spell damage
    // note that their spell damage is just gain of their own auras
    if (GetTypeId() == TYPEID_UNIT && ((Creature*)this)->isPet())
        DoneAdvertisedBenefit += ((Pet*)this)->GetBonusDamage();

    // apply ap bonus and benefit affected by spell power implicit coeffs and spell level penalties
    DoneTotal = SpellBonusWithCoeffs(spellProto, DoneTotal, DoneAdvertisedBenefit, 0, damagetype, true);

    float tmpDamage = (int32(pdamage) + DoneTotal) * DoneTotalMod;
    // apply spellmod to Done damage (flat and pct)
    if (Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, damagetype == DOT ? SPELLMOD_DOT : SPELLMOD_DAMAGE, tmpDamage);

    return tmpDamage > 0 ? uint32(tmpDamage) : 0;
}

/**
 * Calculates target part of spell damage bonuses,
 * will be called on each tick for periodic damage over time auras
 */
uint32 Unit::SpellDamageBonusTaken(Unit* pCaster, SpellEntry const* spellProto, uint32 pdamage, DamageEffectType damagetype)
{
    if (!spellProto || !pCaster || damagetype == DIRECT_DAMAGE)
        return pdamage;

    uint32 schoolMask = spellProto->SchoolMask;

    // Taken total percent damage auras
    float TakenTotalMod = 1.0f;
    int32 TakenTotal = 0;

    // ..taken
    TakenTotalMod *= GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, schoolMask);

    // .. taken pct: dummy auras
    AuraList const& mDummyAuras = GetAurasByType(SPELL_AURA_DUMMY);
    for (AuraList::const_iterator i = mDummyAuras.begin(); i != mDummyAuras.end(); ++i)
    {
        switch ((*i)->GetSpellProto()->SpellIconID)
        {
            // Cheat Death
            case 2109:
                if (((*i)->GetModifier()->m_miscvalue & GetSpellSchoolMask(spellProto)))
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                        continue;
                    float mod = -((Player*)this)->GetRatingBonusValue(CR_CRIT_TAKEN_SPELL) * 2 * 4;
                    if (mod < float((*i)->GetModifier()->m_amount))
                        mod = float((*i)->GetModifier()->m_amount);
                    TakenTotalMod *= (mod + 100.0f) / 100.0f;
                }
                break;
        }
    }

    // ..taken from SpellMod - Righteous Fury
    if (GetTypeId() == TYPEID_PLAYER && HasAura(25780, 0))
    {
        uint32 impRighteousFury[] = { 20470, 20469, 20468 };
        uint32 talentId = ToPlayer()->GetMaxRankSpellFromArray(impRighteousFury, 2);

        if (Aura* spellModAura = GetAura(talentId, 0))
            TakenTotalMod *= (spellModAura->GetSpellProto()->EffectBasePoints[1] + 100.0f) / 100.0f;
    }

    // From caster spells
    // Mod damage taken from AoE spells
    if (IsAreaOfEffectSpell(spellProto))
        TakenTotalMod *= GetTotalAuraMultiplier(SPELL_AURA_MOD_AOE_DAMAGE_AVOIDANCE);

    // Taken fixed damage bonus auras
    int32 TakenAdvertisedBenefit = SpellBaseDamageBonusTaken(GetSpellSchoolMask(spellProto));

    // apply benefit affected by spell power implicit coeffs and spell level penalties
    TakenTotal = pCaster->SpellBonusWithCoeffs(spellProto, TakenTotal, TakenAdvertisedBenefit, 0, damagetype, false);

    float tmpDamage = (int32(pdamage) + TakenTotal) * TakenTotalMod;

    return tmpDamage > 0 ? uint32(tmpDamage) : 0;
}
    
int32 Unit::SpellBaseDamageBonusDone(SpellSchoolMask schoolMask)
{
    int32 DoneAdvertisedBenefit = 0;

    // ..done
    AuraList const& mDamageDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_DONE);
    for (AuraList::const_iterator i = mDamageDone.begin(); i != mDamageDone.end(); ++i)
        if (((*i)->GetModifier()->m_miscvalue & schoolMask) != 0 &&
        (*i)->GetSpellProto()->EquippedItemClass == -1 &&
                                                            // -1 == any item class (not wand then)
        (*i)->GetSpellProto()->EquippedItemInventoryTypeMask == 0)
                                                            // 0 == any inventory type (not wand then)
            DoneAdvertisedBenefit += (*i)->GetModifierValue();

    if (GetTypeId() == TYPEID_PLAYER)
    {
        // Damage bonus from stats
        AuraList const& mDamageDoneOfStatPercent = GetAurasByType(SPELL_AURA_MOD_SPELL_DAMAGE_OF_STAT_PERCENT);
        for (AuraList::const_iterator i = mDamageDoneOfStatPercent.begin();i != mDamageDoneOfStatPercent.end(); ++i)
        {
            if ((*i)->GetModifier()->m_miscvalue & schoolMask)
            {
                SpellEntry const* iSpellProto = (*i)->GetSpellProto();
                uint8 eff = (*i)->GetEffIndex();

                // stat used dependent from next effect aura SPELL_AURA_MOD_SPELL_HEALING presence and misc value (stat index)
                Stats usedStat = STAT_INTELLECT;
                if (eff < 2 && iSpellProto->EffectApplyAuraName[eff+1] == SPELL_AURA_MOD_SPELL_HEALING_OF_STAT_PERCENT)
                    usedStat = Stats(iSpellProto->EffectMiscValue[eff+1]);

                DoneAdvertisedBenefit += int32(GetStat(usedStat) * (*i)->GetModifierValue() / 100.0f);
            }
        }
        // ... and attack power
        AuraList const& mDamageDonebyAP = GetAurasByType(SPELL_AURA_MOD_SPELL_DAMAGE_OF_ATTACK_POWER);
        for (AuraList::const_iterator i =mDamageDonebyAP.begin();i != mDamageDonebyAP.end(); ++i)
            if ((*i)->GetModifier()->m_miscvalue & schoolMask)
                DoneAdvertisedBenefit += int32(GetTotalAttackPowerValue(BASE_ATTACK) * (*i)->GetModifierValue() / 100.0f);
    }

    return DoneAdvertisedBenefit;
}

int32 Unit::SpellBaseDamageBonusTaken(SpellSchoolMask schoolMask)
{
    int32 TakenAdvertisedBenefit = 0;

    // ..taken
    AuraList const& mDamageTaken = GetAurasByType(SPELL_AURA_MOD_DAMAGE_TAKEN);
    for (AuraList::const_iterator i = mDamageTaken.begin();i != mDamageTaken.end(); ++i)
        if (((*i)->GetModifier()->m_miscvalue & schoolMask) != 0)
            TakenAdvertisedBenefit += (*i)->GetModifierValue();

    return TakenAdvertisedBenefit;
}

bool Unit::isSpellCrit(Unit *pVictim, SpellEntry const *spellProto, SpellSchoolMask schoolMask, WeaponAttackType attackType)
{
    // not critting spell
    if (spellProto->AttributesEx2 & SPELL_ATTR_EX2_CANT_CRIT)
        return false;

    // Creatures do not crit with their spells or abilities, unless it is owned by a player
    if (GetTypeId() == TYPEID_UNIT)
    {
        Unit* owner = GetOwner();
        if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
            return false;
    }

    float crit_chance = 0.0f;
    switch (spellProto->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_NONE:  // Exception for Earth Shield, Lifebloom Final Bloom, Vessel of the Naaru
            if (spellProto->Id != 379 && spellProto->Id != 33778 && spellProto->Id != 45064) // We need more spells to find a general way (if there is any)
                return false;
        case SPELL_DAMAGE_CLASS_MAGIC:
        {
            if (schoolMask & SPELL_SCHOOL_MASK_NORMAL)
                crit_chance = 0.0f;
            // For other schools
            else if (GetTypeId() == TYPEID_PLAYER)
                crit_chance = GetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + GetFirstSchoolInMask(schoolMask));
            else
            {
                crit_chance = (float)m_baseSpellCritChance;
                crit_chance += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL, schoolMask);
            }
            // taken
            if (pVictim && !IsPositiveSpell(spellProto->Id))
            {
                // Modify critical chance by victim SPELL_AURA_MOD_ATTACKER_SPELL_CRIT_CHANCE
                crit_chance += pVictim->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_ATTACKER_SPELL_CRIT_CHANCE, schoolMask);
                // Modify critical chance by victim SPELL_AURA_MOD_ATTACKER_SPELL_AND_WEAPON_CRIT_CHANCE
                crit_chance += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_SPELL_AND_WEAPON_CRIT_CHANCE);
                // Modify by player victim resilience
                if (pVictim->GetTypeId() == TYPEID_PLAYER)
                    crit_chance -= pVictim->ToPlayer()->GetRatingBonusValue(CR_CRIT_TAKEN_SPELL);
                // scripted (increase crit chance ... against ... target by x%
                if (pVictim->isFrozen()) // Shatter
                {
                    AuraList const& mOverrideClassScript = GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
                    for (AuraList::const_iterator i = mOverrideClassScript.begin(); i != mOverrideClassScript.end(); ++i)
                    {
                        switch ((*i)->GetModifier()->m_miscvalue)
                        {
                            case 849: crit_chance+= 10.0f; break; //Shatter Rank 1
                            case 910: crit_chance+= 20.0f; break; //Shatter Rank 2
                            case 911: crit_chance+= 30.0f; break; //Shatter Rank 3
                            case 912: crit_chance+= 40.0f; break; //Shatter Rank 4
                            case 913: crit_chance+= 50.0f; break; //Shatter Rank 5
                        }
                    }
                }
            }
            break;
        }
        case SPELL_DAMAGE_CLASS_MELEE:
        case SPELL_DAMAGE_CLASS_RANGED:
        {
            if (pVictim)
            {
                crit_chance = GetUnitCriticalChance(attackType, pVictim);
                crit_chance += (int32(GetMaxSkillValueForLevel(pVictim)) - int32(pVictim->GetDefenseSkillValue(this))) * 0.04f;
                crit_chance += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL, schoolMask);
            }
            break;
        }
        default:
            return false;
    }
    // percent done
    // only players use intelligence for critical chance computations
    if (Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_CRITICAL_CHANCE, crit_chance);

    crit_chance = crit_chance > 0.0f ? crit_chance : 0.0f;
    if (roll_chance_f(crit_chance))
        return true;
    return false;
}

uint32 Unit::SpellCriticalDamageBonus(SpellEntry const* spellProto, uint32 damage, Unit* pVictim)
{
    // Calculate critical bonus
    int32 crit_bonus;
    switch (spellProto->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_MELEE:                      // for melee based spells is 100%
        case SPELL_DAMAGE_CLASS_RANGED:
            crit_bonus = damage;
            break;
        default:
            crit_bonus = damage / 2;                        // for spells is 50%
            break;
    }

    // adds additional damage to crit_bonus (from talents)
    if (Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_CRIT_DAMAGE_BONUS, crit_bonus);

    if (!pVictim)
        return damage += crit_bonus;

    int32 critPctDamageMod = 0;
    if (spellProto->DmgClass >= SPELL_DAMAGE_CLASS_MELEE)
    {
        if (GetWeaponAttackType(spellProto) == RANGED_ATTACK)
            critPctDamageMod += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_DAMAGE);
        else
            critPctDamageMod += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_DAMAGE);
    }
    else
        critPctDamageMod += pVictim->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_ATTACKER_SPELL_CRIT_DAMAGE, GetSpellSchoolMask(spellProto));

    critPctDamageMod += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_CRIT_DAMAGE_BONUS, GetSpellSchoolMask(spellProto));

    uint32 creatureTypeMask = pVictim->GetCreatureTypeMask();
    critPctDamageMod += GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_CRIT_PERCENT_VERSUS, creatureTypeMask);

    if (critPctDamageMod != 0)
        crit_bonus = int32(crit_bonus * float((100.0f + critPctDamageMod) / 100.0f));

    if (crit_bonus > 0)
        damage += crit_bonus;

    return damage;
}

uint32 Unit::SpellCriticalHealingBonus(SpellEntry const* spellProto, uint32 damage, Unit* pVictim)
{
    // Calculate critical bonus
    int32 crit_bonus;
    switch (spellProto->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_MELEE:                      // for melee based spells is 100%
        case SPELL_DAMAGE_CLASS_RANGED:
            // TODO: write here full calculation for melee/ranged spells
            crit_bonus = damage;
            break;
        default:
            crit_bonus = damage / 2;                        // for spells is 50%
            break;
    }

    if (pVictim)
    {
        uint32 creatureTypeMask = pVictim->GetCreatureTypeMask();
        crit_bonus = int32(crit_bonus * GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_CRIT_PERCENT_VERSUS, creatureTypeMask));
    }

    if (crit_bonus > 0)
        damage += crit_bonus;

    return damage;
}

/**
 * Calculates caster part of healing spell bonuses,
 * also includes different bonuses dependent from target auras
 */
uint32 Unit::SpellHealingBonusDone(Unit* pVictim, SpellEntry const* spellProto, uint32 healamount, DamageEffectType damagetype)
{
    // For totems get healing bonus from owner (statue isn't totem in fact)
    if (GetTypeId() == TYPEID_UNIT && ToCreature()->isTotem())
        if (Unit* owner = GetOwner())
            return owner->SpellHealingBonusDone(pVictim, spellProto, healamount, damagetype);

    // No heal amount for this class spells
    if (spellProto->DmgClass == SPELL_DAMAGE_CLASS_NONE)
        return healamount < 0 ? 0 : healamount;

    // Healing Done
    // Done total percent damage auras
    float  DoneTotalMod = 1.0f;
    int32  DoneTotal = 0;

    // Healing done percent
    AuraList const& mHealingDonePct = GetAurasByType(SPELL_AURA_MOD_HEALING_DONE_PERCENT);
    for (AuraList::const_iterator i = mHealingDonePct.begin(); i != mHealingDonePct.end(); ++i)
        DoneTotalMod *= (100.0f + (*i)->GetModifier()->m_amount) / 100.0f;

    // Some spells are not healing spells itself, but they just trigger other healing spell (with fixed amount of healing)
    // Don't apply SPELL_AURA_MOD_HEALING_PCT on them (it will be applied on triggered spell)
    if ((spellProto->Id == 33763 && damagetype != DOT) ||                                                         // Lifebloom
        (spellProto->SpellFamilyName == SPELLFAMILY_SHAMAN && spellProto->SpellFamilyFlags == 0x40000000000l) ||  // Earth Shield
        spellProto->Id == 41635)                                                                                  // Prayer of Mending
        DoneTotalMod = 1.0f;

    // done scripted mod (take it from owner)
    Unit* owner = GetOwner();
    if (!owner) owner = this;
    AuraList const& mOverrideClassScript = owner->GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
    for (AuraList::const_iterator i = mOverrideClassScript.begin(); i != mOverrideClassScript.end(); ++i)
    {
        switch ((*i)->GetModifier()->m_miscvalue)
        {
            case 4415: // Increased Rejuvenation Healing
            case 4953:
            case 3736: // Hateful Totem of the Third Wind / Increased Lesser Healing Wave / LK Arena (4/5/6) Totem of the Third Wind / Savage Totem of the Third Wind
                DoneTotal += (*i)->GetModifier()->m_amount;
                break;
            default:
                break;
        }
    }

    // Done fixed damage bonus auras
    int32 DoneAdvertisedBenefit  = SpellBaseHealingBonusDone(GetSpellSchoolMask(spellProto));

    // apply ap bonus and benefit affected by spell power implicit coeffs and spell level penalties
    DoneTotal = SpellBonusWithCoeffs(spellProto, DoneTotal, DoneAdvertisedBenefit, 0, damagetype, true);

    // use float as more appropriate for negative values and percent applying
    float heal = (healamount + DoneTotal) * DoneTotalMod;
    // apply spellmod to Done amount
    if (Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, damagetype == DOT ? SPELLMOD_DOT : SPELLMOD_DAMAGE, heal);

    return heal < 0 ? 0 : uint32(heal);
}

/**
 * Calculates target part of healing spell bonuses,
 * will be called on each tick for periodic damage over time auras
 */
uint32 Unit::SpellHealingBonusTaken(Unit* pCaster, SpellEntry const* spellProto, uint32 healamount, DamageEffectType damagetype)
{
    float  TakenTotalMod = 1.0f;

    // Healing taken percent
    float minval = float(GetMaxNegativeAuraModifier(SPELL_AURA_MOD_HEALING_PCT));
    if (minval)
        TakenTotalMod *= (100.0f + minval) / 100.0f;

    float maxval = float(GetMaxPositiveAuraModifier(SPELL_AURA_MOD_HEALING_PCT));
    if (maxval)
        TakenTotalMod *= (100.0f + maxval) / 100.0f;

    // No heal amount for this class spells
    if (spellProto->DmgClass == SPELL_DAMAGE_CLASS_NONE)
    {
        healamount = int32(healamount * TakenTotalMod);
        return healamount < 0 ? 0 : healamount;
    }

    // Healing Done
    // Done total percent damage auras
    int32  TakenTotal = 0;

    // Taken fixed damage bonus auras
    int32 TakenAdvertisedBenefit = SpellBaseHealingBonusTaken(GetSpellSchoolMask(spellProto));

    // Blessing of Light dummy affects healing taken from Holy Light and Flash of Light
    if (spellProto->SpellFamilyName == SPELLFAMILY_PALADIN && (spellProto->SpellFamilyFlags & 0x00000000C0000000LL))
    {
        AuraList const& auraDummy = GetAurasByType(SPELL_AURA_DUMMY);
        for (AuraList::const_iterator i = auraDummy.begin(); i != auraDummy.end(); ++i)
        {
            if ((*i)->GetSpellProto()->SpellVisual == 9180)
            {
                uint32 bonusFlat = 0;
                // Increased Blessing of Light Healing
                if (Aura* aura = pCaster->GetAura(38320, EFFECT_INDEX_0))
                    bonusFlat += aura->GetModifier()->m_amount;

                if ((spellProto->SpellFamilyFlags & 0x0000000040000000LL) && (*i)->GetEffIndex() == EFFECT_INDEX_1)      // Flash of Light
                    TakenTotal += (*i)->GetModifier()->m_amount + (bonusFlat * 0.5f);
                else if ((spellProto->SpellFamilyFlags & 0x0000000080000000LL) && (*i)->GetEffIndex() == EFFECT_INDEX_0) // Holy Light
                    TakenTotal += (*i)->GetModifier()->m_amount + bonusFlat;
            }
        }
    }

    // apply benefit affected by spell power implicit coeffs and spell level penalties
    TakenTotal = pCaster->SpellBonusWithCoeffs(spellProto, TakenTotal, TakenAdvertisedBenefit, 0, damagetype, false);

    // Healing Way dummy affects healing taken from Healing Wave
    if (spellProto->SpellFamilyName == SPELLFAMILY_SHAMAN && (spellProto->SpellFamilyFlags & 0x0000000000000040LL))
    {
        AuraList const& auraDummy = GetAurasByType(SPELL_AURA_DUMMY);
        for (AuraList::const_iterator i = auraDummy.begin(); i != auraDummy.end(); ++i)
            if ((*i)->GetId() == 29203)
                TakenTotalMod *= ((*i)->GetModifier()->m_amount + 100.0f) / 100.0f;
    }

    // use float as more appropriate for negative values and percent applying
    float heal = (healamount + TakenTotal) * TakenTotalMod;

    return heal < 0 ? 0 : uint32(heal);
}

int32 Unit::SpellBaseHealingBonusDone(SpellSchoolMask schoolMask)
{
    int32 AdvertisedBenefit = 0;

    AuraList const& mHealingDone = GetAurasByType(SPELL_AURA_MOD_HEALING_DONE);
    for (AuraList::const_iterator i = mHealingDone.begin(); i != mHealingDone.end(); ++i)
        if (((*i)->GetModifier()->m_miscvalue & schoolMask) != 0)
            AdvertisedBenefit += (*i)->GetModifierValue();

    // Healing bonus of spirit, intellect and strength
    if (GetTypeId() == TYPEID_PLAYER)
    {
        // Healing bonus from stats
        AuraList const& mHealingDoneOfStatPercent = GetAurasByType(SPELL_AURA_MOD_SPELL_HEALING_OF_STAT_PERCENT);
        for (AuraList::const_iterator i = mHealingDoneOfStatPercent.begin();i != mHealingDoneOfStatPercent.end(); ++i)
        {
            // stat used dependent from misc value (stat index)
            Stats usedStat = Stats((*i)->GetSpellProto()->EffectMiscValue[(*i)->GetEffIndex()]);
            AdvertisedBenefit += int32(GetStat(usedStat) * (*i)->GetModifierValue() / 100.0f);
        }

        // ... and attack power
        AuraList const& mHealingDonebyAP = GetAurasByType(SPELL_AURA_MOD_SPELL_HEALING_OF_ATTACK_POWER);
        for (AuraList::const_iterator i = mHealingDonebyAP.begin();i != mHealingDonebyAP.end(); ++i)
            if ((*i)->GetModifier()->m_miscvalue & schoolMask)
                AdvertisedBenefit += int32(GetTotalAttackPowerValue(BASE_ATTACK) * (*i)->GetModifierValue() / 100.0f);
    }

    return AdvertisedBenefit;
}

int32 Unit::SpellBaseHealingBonusTaken(SpellSchoolMask schoolMask)
{
    int32 AdvertisedBenefit = 0;
    AuraList const& mDamageTaken = GetAurasByType(SPELL_AURA_MOD_HEALING);
    for (AuraList::const_iterator i = mDamageTaken.begin(); i != mDamageTaken.end(); ++i)
        if ((*i)->GetModifier()->m_miscvalue & schoolMask)
            AdvertisedBenefit += (*i)->GetModifier()->m_amount;

    return AdvertisedBenefit;
}

/**
 * Calculates caster part of melee damage bonuses,
 * also includes different bonuses dependent from target auras
 */
uint32 Unit::MeleeDamageBonusDone(Unit* pVictim, uint32 pdamage, WeaponAttackType attType, SpellEntry const* spellProto, DamageEffectType damagetype)
{
    if (!pVictim || pdamage == 0)
        return pdamage;

    // differentiate for weapon damage based spells
    bool isWeaponDamageBasedSpell = !(spellProto && (damagetype == DOT || IsSpellHaveEffect(spellProto, SPELL_EFFECT_SCHOOL_DAMAGE)));
    Item*  pWeapon          = GetTypeId() == TYPEID_PLAYER ? ((Player*)this)->GetWeaponForAttack(attType, true) : nullptr;
    uint32 creatureTypeMask = pVictim->GetCreatureTypeMask();
    uint32 schoolMask       = spellProto ? spellProto->SchoolMask : uint32(GetMeleeDamageSchoolMask());

    // FLAT damage bonus auras
    // =======================
    int32 DoneFlat  = 0;
    int32 APbonus   = 0;

    // ..done flat, already included in weapon damage based spells
    if (!isWeaponDamageBasedSpell)
    {
        AuraList const& mModDamageDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_DONE);
        for (AuraList::const_iterator i = mModDamageDone.begin(); i != mModDamageDone.end(); ++i)
        {
            if ((*i)->GetModifier()->m_miscvalue & schoolMask &&                             // schoolmask has to fit with the intrinsic spell school
                    (*i)->GetModifier()->m_miscvalue & GetMeleeDamageSchoolMask() &&         // AND schoolmask has to fit with weapon damage school (essential for non-physical spells)
                    (((*i)->GetSpellProto()->EquippedItemClass == -1) ||                     // general, weapon independent
                     (pWeapon && pWeapon->IsFitToSpellRequirements((*i)->GetSpellProto())))) // OR used weapon fits aura requirements
            {
                DoneFlat += (*i)->GetModifier()->m_amount;
            }
        }

        // Pets just add their bonus damage to their melee damage
        if (GetTypeId() == TYPEID_UNIT && ((Creature*)this)->isPet())
            DoneFlat += ((Pet*)this)->GetBonusDamage();
    }

    // ..done flat (by creature type mask)
    DoneFlat += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_DAMAGE_DONE_CREATURE, creatureTypeMask);

    // ..done flat (base at attack power for marked target and base at attack power for creature type)
    if (attType == RANGED_ATTACK)
    {
        APbonus += pVictim->GetTotalAuraModifier(SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS);
        APbonus += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_RANGED_ATTACK_POWER_VERSUS, creatureTypeMask);
    }
    else
    {
        APbonus += pVictim->GetTotalAuraModifier(SPELL_AURA_MELEE_ATTACK_POWER_ATTACKER_BONUS);
        APbonus += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_MELEE_ATTACK_POWER_VERSUS, creatureTypeMask);
    }

    // PERCENT damage auras
    // ====================
    float DonePercent   = 1.0f;

    // ..done pct, already included in weapon damage based spells
    if (!isWeaponDamageBasedSpell)
    {
        AuraList const& mModDamagePercentDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_PERCENT_DONE);
        for (AuraList::const_iterator i = mModDamagePercentDone.begin(); i != mModDamagePercentDone.end(); ++i)
        {
            if ((*i)->GetModifier()->m_miscvalue & schoolMask &&                             // schoolmask has to fit with the intrinsic spell school
                    (*i)->GetModifier()->m_miscvalue & GetMeleeDamageSchoolMask() &&         // AND schoolmask has to fit with weapon damage school (essential for non-physical spells)
                    (((*i)->GetSpellProto()->EquippedItemClass == -1) ||                     // general, weapon independent
                     (pWeapon && pWeapon->IsFitToSpellRequirements((*i)->GetSpellProto())))) // OR used weapon fits aura requirements
            {
                DonePercent *= ((*i)->GetModifier()->m_amount + 100.0f) / 100.0f;
            }
        }

        if (attType == OFF_ATTACK)
            DonePercent *= GetModifierValue(UNIT_MOD_DAMAGE_OFFHAND, TOTAL_PCT);                    // no school check required
    }

    // ..done pct (by creature type mask)
    DonePercent *= GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_DAMAGE_DONE_VERSUS, creatureTypeMask);

    // special dummys/class scripts and other effects
    // =============================================
    Unit* owner = GetOwner();
    if (!owner)
        owner = this;

    // .. done (class scripts)
    AuraList const& mclassScritAuras = GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
    for (AuraList::const_iterator i = mclassScritAuras.begin(); i != mclassScritAuras.end(); ++i)
    {
        switch ((*i)->GetMiscValue())
        {
            // Dirty Deeds
            case 6427:
            case 6428:
                if (pVictim->HasAuraState(AURA_STATE_HEALTHLESS_35_PERCENT))
                {
                    Aura* eff0 = GetAura((*i)->GetId(), 0);
                    if (!eff0 || (*i)->GetEffIndex() != 1)
                    {
                        sLog->outError("Spell structure of DD (%u) changed.", (*i)->GetId());
                        continue;
                    }

                    // effect 0 have expected value but in negative state
                    DonePercent *= (-eff0->GetModifier()->m_amount + 100.0f) / 100.0f;
                }
                break;
        }
    }

    // final calculation
    // =================

    float DoneTotal = 0.0f;
    // scaling of non weapon based spells
    if (!isWeaponDamageBasedSpell)
    {
        // apply ap bonus and benefit affected by spell power implicit coeffs and spell level penalties
        DoneTotal = SpellBonusWithCoeffs(spellProto, DoneTotal, DoneFlat, APbonus, damagetype, true);
    }
    // weapon damage based spells
    else if (APbonus || DoneFlat)
    {
        bool normalized = spellProto ? IsSpellHaveEffect(spellProto, SPELL_EFFECT_NORMALIZED_WEAPON_DMG) : false;
        DoneTotal += int32(APbonus / 14.0f * GetAPMultiplier(attType, normalized));

        // for weapon damage based spells we still have to apply damage done percent mods
        // (that are already included into pdamage) to not-yet included DoneFlat
        // e.g. from doneVersusCreature, apBonusVs...
        UnitMods unitMod;
        switch (attType)
        {
            default:
            case BASE_ATTACK:   unitMod = UNIT_MOD_DAMAGE_MAINHAND; break;
            case OFF_ATTACK:    unitMod = UNIT_MOD_DAMAGE_OFFHAND;  break;
            case RANGED_ATTACK: unitMod = UNIT_MOD_DAMAGE_RANGED;   break;
        }

        DoneTotal += DoneFlat;

        DoneTotal *= GetModifierValue(unitMod, TOTAL_PCT);
    }

    float tmpDamage = float(int32(pdamage) + DoneTotal) * DonePercent;

    // apply spellmod to Done damage
    if (spellProto)
    {
        if (Player* modOwner = GetSpellModOwner())
            modOwner->ApplySpellMod(spellProto->Id, damagetype == DOT ? SPELLMOD_DOT : SPELLMOD_DAMAGE, tmpDamage);
    }

    // bonus result can be negative
    return tmpDamage > 0 ? uint32(tmpDamage) : 0;
}

/**
 * Calculates target part of melee damage bonuses,
 * will be called on each tick for periodic damage over time auras
 */
uint32 Unit::MeleeDamageBonusTaken(Unit* pCaster, uint32 pdamage, WeaponAttackType attType, SpellEntry const* spellProto, DamageEffectType damagetype)
{
    if (!pCaster)
        return pdamage;

    if (pdamage == 0)
        return pdamage;

    // differentiate for weapon damage based spells
    bool isWeaponDamageBasedSpell = !(spellProto && (damagetype == DOT || IsSpellHaveEffect(spellProto, SPELL_EFFECT_SCHOOL_DAMAGE)));
    uint32 schoolMask       = spellProto ? spellProto->SchoolMask : uint32(GetMeleeDamageSchoolMask());
    uint32 mechanicMask     = spellProto ? GetAllSpellMechanicMask(spellProto) : 0;

    // Shred also have bonus as MECHANIC_BLEED damages
    if (spellProto && spellProto->SpellFamilyName == SPELLFAMILY_DRUID && spellProto->SpellFamilyFlags & UI64LIT(0x00008000))
        mechanicMask |= (1 << (MECHANIC_BLEED - 1));

    // FLAT damage bonus auras
    // =======================
    int32 TakenFlat = 0;

    // ..taken flat (base at attack power for marked target and base at attack power for creature type)
    if (attType == RANGED_ATTACK)
        TakenFlat += GetTotalAuraModifier(SPELL_AURA_MOD_RANGED_DAMAGE_TAKEN);
    else
        TakenFlat += GetTotalAuraModifier(SPELL_AURA_MOD_MELEE_DAMAGE_TAKEN);

    // ..taken flat (by school mask)
    TakenFlat += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_DAMAGE_TAKEN, schoolMask);

    // PERCENT damage auras
    // ====================
    float TakenPercent  = 1.0f;

    // ..taken pct (by school mask)
    TakenPercent *= GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, schoolMask);

    // ..taken pct (melee/ranged)
    if (attType == RANGED_ATTACK)
        TakenPercent *= GetTotalAuraMultiplier(SPELL_AURA_MOD_RANGED_DAMAGE_TAKEN_PCT);
    else
        TakenPercent *= GetTotalAuraMultiplier(SPELL_AURA_MOD_MELEE_DAMAGE_TAKEN_PCT);

    // ..taken pct (aoe avoidance)
    if (spellProto && IsAreaOfEffectSpell(spellProto))
        TakenPercent *= GetTotalAuraMultiplier(SPELL_AURA_MOD_AOE_DAMAGE_AVOIDANCE);

    // special dummys/class scripts and other effects
    // =============================================

    // .. taken (dummy auras)
    AuraList const& mDummyAuras = GetAurasByType(SPELL_AURA_DUMMY);
    for (AuraList::const_iterator i = mDummyAuras.begin(); i != mDummyAuras.end(); ++i)
    {
        switch ((*i)->GetSpellProto()->SpellIconID)
        {
            case 2109:                                      // Cheating Death
                if ((*i)->GetModifier()->m_miscvalue & SPELL_SCHOOL_MASK_NORMAL)
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                        continue;

                    float mod = ((Player*)this)->GetRatingBonusValue(CR_CRIT_TAKEN_MELEE) * (-8.0f);
                    if (mod < float((*i)->GetModifier()->m_amount))
                        mod = float((*i)->GetModifier()->m_amount);

                    TakenPercent *= (mod + 100.0f) / 100.0f;
                }
                break;
            case 2312:                                      // Mangle
                if (mechanicMask & (1 << (MECHANIC_BLEED - 1)))
                    TakenPercent *= (100.0f + (*i)->GetModifier()->m_amount) / 100.0f;
                break;
        }
    }

    // final calculation
    // =================

    // scaling of non weapon based spells
    if (!isWeaponDamageBasedSpell)
    {
        // apply benefit affected by spell power implicit coeffs and spell level penalties
        TakenFlat = pCaster->SpellBonusWithCoeffs(spellProto, 0, TakenFlat, 0, damagetype, false);
    }

    float tmpDamage = float(int32(pdamage) + TakenFlat) * TakenPercent;

    // bonus result can be negative
    return tmpDamage > 0 ? uint32(tmpDamage) : 0;
}

bool Unit::IsImmunedToDamage(SpellSchoolMask shoolMask, bool useCharges)
{
    //If m_immuneToSchool type contain this school type, IMMUNE damage.
    SpellImmuneList const& schoolList = m_spellImmune[IMMUNITY_SCHOOL];
    for (SpellImmuneList::const_iterator itr = schoolList.begin(); itr != schoolList.end(); ++itr)
        if (itr->type & shoolMask)
            return true;

    //If m_immuneToDamage type contain magic, IMMUNE damage.
    SpellImmuneList const& damageList = m_spellImmune[IMMUNITY_DAMAGE];
    for (SpellImmuneList::const_iterator itr = damageList.begin(); itr != damageList.end(); ++itr)
        if (itr->type & shoolMask)
            return true;

    return false;
}

bool Unit::IsImmunedToSpell(SpellEntry const* spellInfo, bool useCharges)
{
    if (!spellInfo)
        return false;

    // If Spell has this flag cannot be resisted/immuned/etc
    if (spellInfo->Attributes & SPELL_ATTR_UNAFFECTED_BY_INVULNERABILITY)
        return false;

    SpellImmuneList const& dispelList = m_spellImmune[IMMUNITY_DISPEL];
    for (SpellImmuneList::const_iterator itr = dispelList.begin(); itr != dispelList.end(); ++itr)
        if (itr->type == spellInfo->Dispel)
            return true;

    if (!(spellInfo->AttributesEx & SPELL_ATTR_EX_UNAFFECTED_BY_SCHOOL_IMMUNE) &&         // unaffected by school immunity
        !(spellInfo->AttributesEx & SPELL_ATTR_EX_DISPEL_AURAS_ON_IMMUNITY)               // can remove immune (by dispell or immune it)
        && (spellInfo->Id != 42292))
    {
        SpellImmuneList const& schoolList = m_spellImmune[IMMUNITY_SCHOOL];
        for (SpellImmuneList::const_iterator itr = schoolList.begin(); itr != schoolList.end(); ++itr)
            if (!(IsPositiveSpell(itr->spellId) && IsPositiveSpell(spellInfo->Id)) &&
                (itr->type & GetSpellSchoolMask(spellInfo)))
                return true;
    }

    SpellImmuneList const& mechanicList = m_spellImmune[IMMUNITY_MECHANIC];
    for (SpellImmuneList::const_iterator itr = mechanicList.begin(); itr != mechanicList.end(); ++itr)
        if (itr->type == spellInfo->Mechanic)
            return true;

    SpellImmuneList const& idList = m_spellImmune[IMMUNITY_ID];
    for (SpellImmuneList::const_iterator itr = idList.begin(); itr != idList.end(); ++itr)
        if (itr->type == spellInfo->Id)
            return true;

    return false;
}

bool Unit::IsImmunedToSpellEffect(SpellEntry const* spellInfo, uint32 index) const
{
    //If m_immuneToEffect type contain this effect type, IMMUNE effect.
    uint32 effect = spellInfo->Effect[index];
    SpellImmuneList const& effectList = m_spellImmune[IMMUNITY_EFFECT];
    for (SpellImmuneList::const_iterator itr = effectList.begin(); itr != effectList.end(); ++itr)
        if (itr->type == effect)
            return true;

    uint32 mechanic = spellInfo->EffectMechanic[index];
    if (mechanic)
    {
        SpellImmuneList const& mechanicList = m_spellImmune[IMMUNITY_MECHANIC];
        for (SpellImmuneList::const_iterator itr = mechanicList.begin(); itr != mechanicList.end(); ++itr)
            if (itr->type == mechanic)
                return true;
    }

    uint32 aura = spellInfo->EffectApplyAuraName[index];
    if (aura)
    {
        SpellImmuneList const& list = m_spellImmune[IMMUNITY_STATE];
        for (SpellImmuneList::const_iterator itr = list.begin(); itr != list.end(); ++itr)
            if (itr->type == aura)
                if (!(spellInfo->AttributesEx3 & SPELL_ATTR_EX3_IGNORE_HIT_RESULT))
                    return true;
    }

    return false;
}

bool Unit::IsDamageToThreatSpell(SpellEntry const * spellInfo) const
{
    if (!spellInfo)
        return false;

    uint32 family = spellInfo->SpellFamilyName;
    uint64 flags = spellInfo->SpellFamilyFlags;

    if ((family == 5 && flags == 256) ||                     //Searing Pain
        (family == SPELLFAMILY_SHAMAN && flags == SPELLFAMILYFLAG_SHAMAN_FROST_SHOCK))
        return true;

    return false;
}

void Unit::ApplySpellImmune(uint32 spellId, uint32 op, uint32 type, bool apply)
{
    if (apply)
    {
        // Check if we have another spell in this Immunity still active on us so don't remove the actual immunity
        Unit::AuraList const& ImmunityAuras = GetAurasByType(SPELL_AURA_MECHANIC_IMMUNITY);
        if (!ImmunityAuras.empty())
        {
            for (AuraList::const_iterator iter = ImmunityAuras.begin(); iter != ImmunityAuras.end(); ++iter)
            {
                if ((*iter)->GetId() == spellId)
                    continue;
                if ((*iter)->GetMiscValue() == type) // Don't add the immunity when we found ANOTHER spell with this Immunity Type
                    return;
            }
        }

        for (SpellImmuneList::iterator itr = m_spellImmune[op].begin(), next; itr != m_spellImmune[op].end(); itr = next)
        {
            next = itr; ++next;
            if (itr->type == type)
            {
                m_spellImmune[op].erase(itr);
                next = m_spellImmune[op].begin();
            }
        }
        SpellImmune Immune;
        Immune.spellId = spellId;
        Immune.type = type;
        m_spellImmune[op].push_back(Immune);
    }
    else
    {
        for (SpellImmuneList::iterator itr = m_spellImmune[op].begin(); itr != m_spellImmune[op].end(); ++itr)
        {
            if (itr->spellId == spellId)
            {
                m_spellImmune[op].erase(itr);
                break;
            }
        }
    }
}

void Unit::ApplySpellDispelImmunity(const SpellEntry * spellProto, DispelType type, bool apply)
{
    ApplySpellImmune(spellProto->Id, IMMUNITY_DISPEL, type, apply);

    if (apply && spellProto->AttributesEx & SPELL_ATTR_EX_DISPEL_AURAS_ON_IMMUNITY)
        RemoveAurasWithDispelType(type);
}

float Unit::GetWeaponProcChance() const
{
    // normalized proc chance for weapon attack speed
    // (odd formula...)
    if (isAttackReady(BASE_ATTACK))
        return (GetAttackTime(BASE_ATTACK) * 1.8f / 1000.0f);
    else if (haveOffhandWeapon() && isAttackReady(OFF_ATTACK))
        return (GetAttackTime(OFF_ATTACK) * 1.6f / 1000.0f);
    return 0;
}

float Unit::GetPPMProcChance(uint32 WeaponSpeed, float PPM) const
{
    // proc per minute chance calculation
    if (PPM <= 0) return 0.0f;
    uint32 result = uint32((WeaponSpeed * PPM) / 600.0f);   // result is chance in percents (probability = Speed_in_sec * (PPM / 60))
    return result;
}

void Unit::Mount(uint32 mount)
{
    if (!mount)
        return;

    RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_MOUNT);

    SetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID, mount);

    SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_MOUNT);

    // unsummon pet
    if (GetTypeId() == TYPEID_PLAYER)
    {
        Pet* pet = ToPlayer()->GetPet();
        if (pet)
        {
            BattleGround *bg = ToPlayer()->GetBattleGround();
            // don't unsummon pet in arena but SetFlag UNIT_FLAG_STUNNED to disable pet's interface
            if (bg && bg->isArena())
                pet->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_STUNNED);
            else
            {
                if (pet->isControlled())
                {
                    ToPlayer()->SetTemporaryUnsummonedPetNumber(pet->GetCharmInfo()->GetPetNumber());
                    ToPlayer()->SetOldPetSpell(pet->GetUInt32Value(UNIT_CREATED_BY_SPELL));
                }
                ToPlayer()->RemovePet(NULL, PET_SAVE_AS_CURRENT);
                return;
            }
        }
        ToPlayer()->SetTemporaryUnsummonedPetNumber(0);
    }
}

void Unit::Unmount()
{
    if (!IsMounted())
        return;

    RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_NOT_MOUNTED);

    SetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID, 0);
    RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_MOUNT);

    // only resummon old pet if the player is already added to a map
    // this prevents adding a pet to a not created map which would otherwise cause a crash
    // (it could probably happen when logging in after a previous crash)
    if (GetTypeId() == TYPEID_PLAYER && IsInWorld() && isAlive())
    {
        if (ToPlayer()->GetTemporaryUnsummonedPetNumber())
        {
            Pet* NewPet = new Pet(ToPlayer());
            if (!NewPet->LoadPetFromDB(ToPlayer(), 0, ToPlayer()->GetTemporaryUnsummonedPetNumber(), true))
                delete NewPet;
            ToPlayer()->SetTemporaryUnsummonedPetNumber(0);
        }
        else
           if (Guardian *pPet = GetGuardianPet())
               if (pPet->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_STUNNED) && !pPet->hasUnitState(UNIT_STAT_STUNNED))
                   pPet->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_STUNNED);
    }
}

void Unit::SetInCombatWith(Unit* enemy, uint32 spellId)
{
    Unit* eOwner = enemy->GetCharmerOrOwnerOrSelf();
    if (!InSamePhase(enemy))
        return;

    if (eOwner->IsPvP())
    {
        SetInCombatState(true, enemy, spellId);
        return;
    }

    //check for duel
    if (eOwner->GetTypeId() == TYPEID_PLAYER && eOwner->ToPlayer()->duel)
    {
        Unit const* myOwner = GetCharmerOrOwnerOrSelf();
        if (eOwner->ToPlayer()->duel->opponent == myOwner)
        {
            SetInCombatState(true, enemy, spellId);
            return;
        }
    }
    SetInCombatState(false, enemy, spellId);
}

void Unit::CombatStart(Unit* target, bool initialAggro, uint32 spellId)
{
    if (initialAggro && !target->hasUnitState(UNIT_STAT_EVADE))
    {
        if (!target->IsStandState())
            target->SetStandState(UNIT_STAND_STATE_STAND);

        if (!target->isInCombat() && target->GetTypeId() != TYPEID_PLAYER
            && !target->ToCreature()->HasReactState(REACT_PASSIVE) && target->ToCreature()->IsAIEnabled)
        {
            target->ToCreature()->AI()->AttackStart(this);
            if (target->ToCreature()->GetFormation())
            {
                target->ToCreature()->GetFormation()->MemberAttackStart(target->ToCreature(), this);
                sLog->outDebug("Unit::CombatStart() calls CreatureGroups::MemberHasAttacked(this);");
            }
        }

        SetInCombatWith(target);

        // Don't set target in combat when isolated
        if (!target->hasUnitState(UNIT_STAT_ISOLATED))
            target->SetInCombatWith(this);
    }
    Unit *who = target->GetCharmerOrOwnerOrSelf();
    if (who->GetTypeId() == TYPEID_PLAYER)
        SetContestedPvP(who->ToPlayer());

    Player *me = GetCharmerOrOwnerPlayerOrPlayerItself();
    if (me && who->IsPvP()
        && (who->GetTypeId() != TYPEID_PLAYER
        || !me->duel || me->duel->opponent != who))
    {
        me->UpdatePvP(true);
        me->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
    }

    if (GetTypeId() != TYPEID_PLAYER && ToCreature()->isPet())
    {
        if (Unit *owner = GetOwner())
            for (uint8 i = 0; i < MAX_MOVE_TYPE; ++i)
                SetSpeed(UnitMoveType(i), GetSpeedRate(UnitMoveType(i)), true);
    }
}

void Unit::SetInCombatState(bool PvP, Unit* enemy, uint32 spellId)
{
    // only alive units can be in combat
    if (!isAlive())
        return;

    if (PvP)
    {
        m_CombatTimer = 5000;

        // Apply 1 second extra combat timer to gouge
        if (spellId == 38764)
            m_CombatTimer += 1000;
    }

    // Pet and Guardian linking combat to owner
    if (GetTypeId() == TYPEID_UNIT)
        if (ToCreature()->isPet() || ToCreature()->isGuardian())
            if (Unit* owner = GetCharmerOrOwnerPlayerOrPlayerItself())
                owner->SetInCombatState(PvP);

    if (isInCombat() || hasUnitState(UNIT_STAT_EVADE))
        return;

    SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);

    if (GetTypeId() == TYPEID_PLAYER)
    {
        if (m_currentSpells[CURRENT_GENERIC_SPELL] && m_currentSpells[CURRENT_GENERIC_SPELL]->getState() != SPELL_STATE_FINISHED)
        {
            if (m_currentSpells[CURRENT_GENERIC_SPELL]->m_spellInfo->Attributes & SPELL_ATTR_CANT_USED_IN_COMBAT)
                InterruptSpell(CURRENT_GENERIC_SPELL);
        }
    }
    else
    {
        if ((IsAIEnabled && ToCreature()->AI()->IsEscorted()) ||
            GetMotionMaster()->GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE ||
            GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
            ToCreature()->SetHomePosition(GetPositionX(), GetPositionY(), GetPositionZ(), GetOrientation());

        if (enemy)
        {
            if (IsAIEnabled && ToCreature()->AI())
                ToCreature()->AI()->EnterCombat(enemy);

            if (ToCreature()->GetFormation())
                ToCreature()->GetFormation()->MemberAttackStart((Creature*)this, enemy);

            RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_OOC_NOT_ATTACKABLE);
        }
        
        if (ToCreature()->isPet())
        {
            UpdateSpeed(MOVE_RUN, true);
            UpdateSpeed(MOVE_SWIM, true);
            UpdateSpeed(MOVE_FLIGHT, true);
        }
    }

    for (Unit::ControlList::iterator itr = m_Controlled.begin(); itr != m_Controlled.end(); ++itr)
    {
        (*itr)->SetInCombatState(PvP, enemy);
        (*itr)->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PET_IN_COMBAT);
    }
}

void Unit::ClearInCombat()
{
    m_CombatTimer = 0;
    RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);

    // Player's state will be cleared in Player::UpdateContestedPvP
    if (Creature* creature = ToCreature())
    {
        if (creature->GetCreatureTemplate() && creature->GetCreatureTemplate()->unit_flags & UNIT_FLAG_OOC_NOT_ATTACKABLE)
            SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_OOC_NOT_ATTACKABLE); // set immunity state to the one from db on evade

        clearUnitState(UNIT_STAT_ATTACK_PLAYER);

        if (creature->isPet() || creature->isGuardian())
        {
            if (Unit *owner = GetOwner())
                for (uint8 i = 0; i < MAX_MOVE_TYPE; ++i)
                if (owner->GetSpeedRate(UnitMoveType(i)) > GetSpeedRate(UnitMoveType(i)))
                    SetSpeed(UnitMoveType(i), owner->GetSpeedRate(UnitMoveType(i)), true);
        }
    }
    else if (!isCharmed())
        return;

    RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PET_IN_COMBAT);
}

//TODO: remove this function
bool Unit::isTargetableForAttack() const
{
    return isAttackableByAOE() && !hasUnitState(UNIT_STAT_DIED);
}

bool Unit::canAttack(Unit const* target, bool force) const
{
    ASSERT(target);

    if (force)
    {
        if (IsFriendlyTo(target))
            return false;
    }
    else if (!IsHostileTo(target))
        return false;

    if (!target->isAttackableByAOE())
        return false;

    // feign dead case
    if (target->HasFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FEIGN_DEATH))
    {
        if ((GetTypeId() != TYPEID_PLAYER && !GetOwner()) || (GetOwner() && GetOwner()->GetTypeId() != TYPEID_PLAYER))
            return false;
        // if this == player or owner == player check other conditions
    }
    // real dead case ~UNIT_FLAG2_FEIGN_DEATH && UNIT_STAT_DIED
    else if (target->hasUnitState(UNIT_STAT_DIED))
        return false;

    // if owner can detect target return true of creature
    if (GetOwner() && GetOwner()->GetTypeId() == TYPEID_PLAYER)
        if (Player* owner = GetOwner()->ToPlayer())
        {
            if (owner->canDetectStealthOf(target))
                return true;
        }

    if (!canDetectInvisibilityOf(target))
        return false;

    if (!canDetectStealthOf(target))
        return false;

    return true;
}

bool Unit::isAttackableByAOE(float x, float y, float z, bool LosCheck) const
{
    if (!isAlive())
        return false;

    if (HasFlag(UNIT_FIELD_FLAGS,
        UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_OOC_NOT_ATTACKABLE))
        return false;

    if (GetTypeId() == TYPEID_PLAYER && ToPlayer()->isGameMaster())
        return false;

    if (LosCheck)
        if (!IsWithinLOS(x, y, z))
            return false;

    return !isInFlight();
}

int32 Unit::ModifyHealth(int32 dVal)
{
    int32 gain = 0;

    if (dVal == 0)
        return 0;

    int32 curHealth = (int32)GetHealth();

    int32 val = dVal + curHealth;
    if (val <= 0)
    {
        SetHealth(0);
        return -curHealth;
    }

    int32 maxHealth = (int32)GetMaxHealth();

    if (val < maxHealth)
    {
        SetHealth(val);
        gain = val - curHealth;
    }
    else if (curHealth != maxHealth)
    {
        SetHealth(maxHealth);
        gain = maxHealth - curHealth;
    }

    return gain;
}

// used only to calculate channeling time
void Unit::ModSpellCastTime(SpellEntry const* spellProto, int32 & castTime, Spell * spell)
{
    if (!spellProto || castTime<0)
        return;

    //called from caster
    if (Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_CASTING_TIME, castTime, spell);

    if (!((spellProto->Attributes & (SPELL_ATTR_ABILITY | SPELL_ATTR_TRADESPELL)) || (spellProto->AttributesEx3 & SPELL_ATTR_EX3_NO_SPELL_BONUS)) &&
        ((GetTypeId() == TYPEID_PLAYER && spellProto->SpellFamilyName) || GetTypeId() == TYPEID_UNIT))
        castTime = int32(float(castTime) * GetFloatValue(UNIT_MOD_CAST_SPEED));
    else if (spellProto->Attributes & SPELL_ATTR_RANGED && !(spellProto->AttributesEx2 & SPELL_ATTR_EX2_AUTOREPEAT_FLAG))
        castTime = int32 (float(castTime) * m_modAttackSpeedPct[RANGED_ATTACK]);
}

int32 Unit::ModifyPower(Powers power, int32 dVal)
{
    int32 gain = 0;

    if (dVal == 0)
        return 0;

    int32 curPower = (int32)GetPower(power);

    int32 val = dVal + curPower;
    if (val <= 0)
    {
        SetPower(power, 0);
        return -curPower;
    }

    int32 maxPower = (int32)GetMaxPower(power);

    if (val < maxPower)
    {
        SetPower(power, val);
        gain = val - curPower;
    }
    else if (curPower != maxPower)
    {
        SetPower(power, maxPower);
        gain = maxPower - curPower;
    }

    return gain;
}

bool Unit::isAlwaysVisibleFor(WorldObject const* seer) const
{
    if (WorldObject::isAlwaysVisibleFor(seer))
        return true;

    // Always seen by owner
    if (uint64 guid = GetCharmerOrOwnerGUID())
        if (seer->GetGUID() == guid)
            return true;

    if (IsVisibilityForced())
        return true;

    return false;
}

bool Unit::isAlwaysDetectableFor(WorldObject const* seer) const
{
    if (WorldObject::isAlwaysDetectableFor(seer))
        return true;

    if (isStalkedByGUID(seer->GetGUID()))
        return true;

    return false;
}

void Unit::SetVisibility(UnitVisibility x)
{
    if (x == VISIBILITY_OFF)
        m_serverSideVisibility.SetValue(SERVERSIDE_VISIBILITY_GM, SEC_GAMEMASTER);
    else
        m_serverSideVisibility.SetValue(SERVERSIDE_VISIBILITY_GM, SEC_PLAYER);

    UpdateObjectVisibility();
}

void Unit::UpdateSpeed(UnitMoveType mtype, bool forced, float ratio)
{
    int32 mainSpeedBonus = 0;
    float stackBonus     = 1.0f;
    float nonStackBonus  = 1.0f;

    switch (mtype)
    {
        // Only apply debuffs
        case MOVE_FLIGHT_BACK:
        case MOVE_RUN_BACK:
        case MOVE_SWIM_BACK:
            break;
        case MOVE_WALK:
            return;
        case MOVE_RUN:
        {
            if (IsMounted()) // Use on mount auras
            {
                mainSpeedBonus = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED);
                stackBonus     = GetTotalAuraMultiplier(SPELL_AURA_MOD_MOUNTED_SPEED_ALWAYS);
                nonStackBonus  = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_MOUNTED_SPEED_NOT_STACK))/100.0f;
            }
            else
            {
                mainSpeedBonus = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_SPEED);
                stackBonus     = GetTotalAuraMultiplier(SPELL_AURA_MOD_SPEED_ALWAYS);
                nonStackBonus  = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_SPEED_NOT_STACK))/100.0f;
            }
            break;
        }
        case MOVE_SWIM:
        {
            mainSpeedBonus = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_SWIM_SPEED);
            break;
        }
        case MOVE_FLIGHT:
        {
            if (IsMounted()) // Use on mount auras
            {
                mainSpeedBonus = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED);
                stackBonus     = GetTotalAuraMultiplier(SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED_STACKING);
                nonStackBonus  = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED_NOT_STACKING))/100.0f;
            }
            else             // Use not mount (shapeshift for example) auras (should stack)
            {
                mainSpeedBonus = GetTotalAuraModifier(SPELL_AURA_MOD_FLIGHT_SPEED);
                stackBonus     = GetTotalAuraMultiplier(SPELL_AURA_MOD_FLIGHT_SPEED_STACKING);
                nonStackBonus  = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_FLIGHT_SPEED_NOT_STACKING))/100.0f;
            }
            break;
        }
        default:
            sLog->outError("Unit::UpdateSpeed: Unsupported move type (%d)", mtype);
            return;
    }

    // now we ready for speed calculation
    float speed = std::max(nonStackBonus, stackBonus);
    if (mainSpeedBonus)
        AddPct(speed, mainSpeedBonus);

    switch (mtype)
    {
        case MOVE_RUN:
        case MOVE_SWIM:
        case MOVE_FLIGHT:
        {
            // Set creature speed rate
            /*if (GetTypeId() == TYPEID_UNIT)
            {
                Unit* pOwner = GetCharmerOrOwner();
                if (ToCreature()->isPet() && pOwner && (!isInCombat() && !pOwner->isInCombat())) // Must check for owner or crash on "Tame Beast"
                {
                    // For every yard over 5, increase speed by 0.01
                    //  to help prevent pet from lagging behind and despawning
                    float dist = GetDistance(pOwner);
                    float baseRate = 1.00f; // base speed is 100% of owner speed

                    if (dist < 5)
                        dist = 5;

                    float mult = baseRate + ((dist - 5) * 0.01f);

                    speed *= pOwner->GetSpeedRate(mtype) * mult; // pets derive speed from owner when not in combat
                }
                else
                    speed *= ToCreature()->GetCreatureTemplate()->speed;    // at this point, MOVE_WALK is never reached
            }*/

            // Normalize speed by 191 aura SPELL_AURA_USE_NORMAL_MOVEMENT_SPEED if need
            // TODO: possible affect only on MOVE_RUN
            if (int32 normalization = GetMaxPositiveAuraModifier(SPELL_AURA_USE_NORMAL_MOVEMENT_SPEED))
            {
                if (Creature* creature = ToCreature())
                {
                    uint32 immuneMask = creature->GetCreatureTemplate()->MechanicImmuneMask;
                    if (immuneMask & (1 << MECHANIC_SNARE) || immuneMask & (1 << MECHANIC_DAZE))
                        break;
                }

                // Use speed from aura
                float maxSpeed = normalization / baseMoveSpeed[mtype];
                if (speed > maxSpeed)
                    speed = maxSpeed;
            }
            break;
        }
        default:
            break;
    }

    // for creature case, we check explicit if mob searched for assistance
    if (GetTypeId() == TYPEID_UNIT)
    {
        if (ToCreature()->HasSearchedAssistance())
            speed *= 0.66f;                                 // best guessed value, so this will be 33% reduction. Based off initial speed, mob can then "run", "walk fast" or "walk".
    }

    // Apply strongest slow aura mod to speed
    int32 slow = GetMaxNegativeAuraModifier(SPELL_AURA_MOD_DECREASE_SPEED);
    if (slow)
        AddPct(speed, slow);

    SetSpeed(mtype, speed * ratio, forced);
}

float Unit::GetSpeed(UnitMoveType mtype) const
{
    return m_speed_rate[mtype]*baseMoveSpeed[mtype];
}

void Unit::SetSpeed(UnitMoveType mtype, float rate, bool forced)
{
    if (rate < 0)
        rate = 0.0f;

    // Update speed only on change
    if (m_speed_rate[mtype] == rate)
        return;

    m_speed_rate[mtype] = rate;

    propagateSpeedChange();

    // Send speed change packet only for player
    if (GetTypeId() != TYPEID_PLAYER)
        return;

    if (!isInCombat())
        if (Pet* pet = ToPlayer()->GetPet())
            pet->SetSpeed(mtype, m_speed_rate[mtype], forced);

    WorldPacket data;
    if (!forced)
    {
        switch (mtype)
        {
            case MOVE_WALK:
                data.Initialize(MSG_MOVE_SET_WALK_SPEED, 8+4+1+4+4+4+4+4+4+4);
                break;
            case MOVE_RUN:
                data.Initialize(MSG_MOVE_SET_RUN_SPEED, 8+4+1+4+4+4+4+4+4+4);
                break;
            case MOVE_RUN_BACK:
                data.Initialize(MSG_MOVE_SET_RUN_BACK_SPEED, 8+4+1+4+4+4+4+4+4+4);
                break;
            case MOVE_SWIM:
                data.Initialize(MSG_MOVE_SET_SWIM_SPEED, 8+4+1+4+4+4+4+4+4+4);
                break;
            case MOVE_SWIM_BACK:
                data.Initialize(MSG_MOVE_SET_SWIM_BACK_SPEED, 8+4+1+4+4+4+4+4+4+4);
                break;
            case MOVE_TURN_RATE:
                data.Initialize(MSG_MOVE_SET_TURN_RATE, 8+4+1+4+4+4+4+4+4+4);
                break;
            case MOVE_FLIGHT:
                data.Initialize(MSG_MOVE_SET_FLIGHT_SPEED, 8+4+1+4+4+4+4+4+4+4);
                break;
            case MOVE_FLIGHT_BACK:
                data.Initialize(MSG_MOVE_SET_FLIGHT_BACK_SPEED, 8+4+1+4+4+4+4+4+4+4);
                break;
            default:
                sLog->outError("Unit::SetSpeed: Unsupported move type (%d), data not sent to client.", mtype);
                return;
        }

        data << GetPackGUID();
        data << uint32(0);                                  // movement flags
        data << uint8(0);                                   // unk
        data << uint32(getMSTime());
        data << float(GetPositionX());
        data << float(GetPositionY());
        data << float(GetPositionZ());
        data << float(GetOrientation());
        data << uint32(0);                                  // fall time
        data << float(GetSpeed(mtype));
        SendMessageToSet(&data, true);
    }
    else
    {
        // register forced speed changes for WorldSession::HandleForceSpeedChangeAck
        // and do it only for real sent packets and use run for run/mounted as client expected
        ++ToPlayer()->m_forced_speed_changes[mtype];

        switch (mtype)
        {
            case MOVE_WALK:
                data.Initialize(SMSG_FORCE_WALK_SPEED_CHANGE, 16);
                break;
            case MOVE_RUN:
                data.Initialize(SMSG_FORCE_RUN_SPEED_CHANGE, 17);
                break;
            case MOVE_RUN_BACK:
                data.Initialize(SMSG_FORCE_RUN_BACK_SPEED_CHANGE, 16);
                break;
            case MOVE_SWIM:
                data.Initialize(SMSG_FORCE_SWIM_SPEED_CHANGE, 16);
                break;
            case MOVE_SWIM_BACK:
                data.Initialize(SMSG_FORCE_SWIM_BACK_SPEED_CHANGE, 16);
                break;
            case MOVE_TURN_RATE:
                data.Initialize(SMSG_FORCE_TURN_RATE_CHANGE, 16);
                break;
            case MOVE_FLIGHT:
                data.Initialize(SMSG_FORCE_FLIGHT_SPEED_CHANGE, 16);
                break;
            case MOVE_FLIGHT_BACK:
                data.Initialize(SMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE, 16);
                break;
            default:
                sLog->outError("Unit::SetSpeed: Unsupported move type (%d), data not sent to client.", mtype);
                return;
        }
        data << GetPackGUID();
        data << (uint32)0;                                  // moveEvent, NUM_PMOVE_EVTS = 0x39
        if (mtype == MOVE_RUN)
            data << uint8(0);                               // new 2.1.0
        data << float(GetSpeed(mtype));
        SendMessageToSet(&data, true);
    }
}

void Unit::SetHover(bool on)
{
    if (on)
        CastSpell(this, 11010, true);
    else
        RemoveAurasDueToSpell(11010);
}

void Unit::setDeathState(DeathState s)
{
    if (s != ALIVE && s != JUST_ALIVED)
    {
        CombatStop();
        DeleteThreatList();
        getHostileRefManager().deleteReferences();
        ClearComboPointHolders();                           // any combo points pointed to unit lost at it death

        if (IsNonMeleeSpellCasted(false))
            InterruptNonMeleeSpells(false);

        UnsummonAllTotems();
        RemoveAllControlled();
        RemoveAllAurasOnDeath();
    }

    if (s == JUST_DIED)
    {
        ModifyAuraState(AURA_STATE_HEALTHLESS_20_PERCENT, false);
        ModifyAuraState(AURA_STATE_HEALTHLESS_35_PERCENT, false);
        // remove aurastates allowing special moves
        ClearAllReactives();
        ClearDiminishings();
        GetMotionMaster()->Clear(false);
        GetMotionMaster()->MoveIdle();
        //without this when removing IncreaseMaxHealth aura player may stuck with 1 hp
        //do not why since in IncreaseMaxHealth currenthealth is checked
        if (GetTypeId() != TYPEID_UNIT) // Creature case will be handeled by m_deathDelayTimer
            SetHealth(0);
    }
    else if (s == JUST_ALIVED)
        RemoveFlag (UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE); // clear skinnable for creature and player (at battleground)

    if (m_deathState != ALIVE && s == ALIVE)
    {
        //_ApplyAllAuraMods();
    }
    m_deathState = s;
}

/*########################################
########                          ########
########       AGGRO SYSTEM       ########
########                          ########
########################################*/
bool Unit::CanHaveThreatList() const
{
    // only creatures can have threat list
    if (GetTypeId() != TYPEID_UNIT)
        return false;

    // only alive units can have threat list
    if (!isAlive() || isDying())
        return false;

    // totems can not have threat list
    if (ToCreature()->isTotem())
        return false;

    return true;
}

//======================================================================

float Unit::ApplyTotalThreatModifier(float threat, SpellSchoolMask schoolMask)
{
    if (!HasAuraType(SPELL_AURA_MOD_THREAT))
        return threat;

    SpellSchools school = GetFirstSchoolInMask(schoolMask);

    return threat * m_threatModifier[school];
}

//======================================================================

void Unit::AddThreat(Unit* pVictim, float threat, SpellSchoolMask schoolMask, SpellEntry const *threatSpell)
{
    // Only mobs can manage threat lists
    if (CanHaveThreatList() && !pVictim->hasUnitState(UNIT_STAT_EVADE))
        m_ThreatManager.addThreat(pVictim, threat, schoolMask, threatSpell);
}

//======================================================================

void Unit::DeleteThreatList()
{
    m_ThreatManager.clearReferences();
}

//======================================================================

void Unit::TauntApply(Unit* taunter)
{
    ASSERT(GetTypeId() == TYPEID_UNIT);

    if (!taunter || (taunter->GetTypeId() == TYPEID_PLAYER && taunter->ToPlayer()->isGameMaster()))
        return;

    if (!CanHaveThreatList())
        return;

    Unit *target = getVictim();
    if (target && target == taunter)
        return;

    SetInFront(taunter);
    if (ToCreature()->IsAIEnabled)
        ToCreature()->AI()->AttackStart(taunter);

    //m_ThreatManager.tauntApply(taunter);
}

//======================================================================

void Unit::TauntFadeOut(Unit *taunter)
{
    ASSERT(GetTypeId() == TYPEID_UNIT);

    if (!taunter || (taunter->GetTypeId() == TYPEID_PLAYER && taunter->ToPlayer()->isGameMaster()))
        return;

    if (!CanHaveThreatList())
        return;

    Unit *target = getVictim();
    if (!target || target != taunter)
        return;

    if (m_ThreatManager.isThreatListEmpty())
    {
        if (ToCreature()->IsAIEnabled)
            ToCreature()->AI()->EnterEvadeMode();
        return;
    }

    //m_ThreatManager.tauntFadeOut(taunter);
    target = m_ThreatManager.getHostileTarget();

    if (target && target != taunter)
    {
        SetInFront(target);
        if (ToCreature()->IsAIEnabled)
            ToCreature()->AI()->AttackStart(target);
    }
}

//======================================================================

Unit* Creature::SelectVictim()
{
    //function provides main threat functionality
    //next-victim-selection algorithm and evade mode are called
    //threat list sorting etc.

    //This should not be called by unit who does not have a threatlist
    //or who does not have threat (totem/pet/critter)
    //otherwise enterevademode every update

    Unit* target = NULL;

    if (!m_ThreatManager.isThreatListEmpty())
    {
        if (!HasAuraType(SPELL_AURA_MOD_TAUNT))
        {
            target = m_ThreatManager.getHostileTarget();
        }
        else
            target = getVictim();
    }

    if (target)
    {
        if (!hasUnitState(UNIT_STAT_STUNNED))
        {
            SetInFront(target);

            // check if currently selected target is reachable
            // NOTE: path alrteady generated from AttackStart()
            if (!GetMotionMaster()->isReachable())
            {
                // remove all taunts
                RemoveSpellsCausingAura(SPELL_AURA_MOD_TAUNT);

                if (m_ThreatManager.getThreatList().size() < 2)
                {
                    // only one target in list, we have to evade after timer
                    // TODO: make timer - inside Creature class
                    ((Creature*)this)->AI()->EnterEvadeMode();
                }
                else
                {
                    // remove unreachable target from our threat list
                    // next iteration we will select next possible target
                    getHostileRefManager().deleteReference(target);
                    m_ThreatManager.modifyThreatPercent(target, -101);

                    _removeAttacker(target);
                }
                return NULL;
            }
        }
        return target;
    }

    // last case when creature don't must go to evade mode:
    // it in combat but attacker not make any damage and not enter to aggro radius to have record in threat list
    // for example at owner command to pet attack some far away creature
    // Note: creature not have targeted movement generator but have attacker in this case
    /*if (GetMotionMaster()->GetCurrentMovementGeneratorType() != TARGETED_MOTION_TYPE)
    {
        for (AttackerSet::const_iterator itr = m_attackers.begin(); itr != m_attackers.end(); ++itr)
        {
            if ((*itr)->IsInMap(this) && canAttack(*itr) && (*itr)->isInAccessiblePlaceFor(ToCreature()))
                return NULL;
        }
    }*/

    // search nearby enemy before enter evade mode
    if (HasReactState(REACT_AGGRESSIVE))
    {
        target = SelectNearestTarget();
        if (target && !IsOutOfThreatArea(target))
            return target;
    }

    Unit::AuraList const& iAuras = GetAurasByType(SPELL_AURA_MOD_INVISIBILITY);
    for (Unit::AuraList::const_iterator itr = iAuras.begin(); itr != iAuras.end(); ++itr)
    {
        if ((*itr)->IsPermanent())
        {
            AI()->EnterEvadeMode();
            break;
        }
        
        return NULL;
    }

    // enter in evade mode in other case
    AI()->EnterEvadeMode();

    return NULL;
}

//======================================================================
//======================================================================
//======================================================================

int32 Unit::CalculateSpellDamage(Unit const* target, SpellEntry const* spellProto, uint8 effect_index, int32 const* effBasePoints)
{
    float basePointsPerLevel = spellProto->EffectRealPointsPerLevel[effect_index];
    int32 basePoints = effBasePoints ? *effBasePoints : spellProto->EffectBasePoints[effect_index];
    float randomPointsPerLevel = spellProto->EffectDicePerLevel[effect_index];

    int32 level = int32(getLevel());
    if (level > (int32)spellProto->maxLevel && spellProto->maxLevel > 0)
        level = (int32)spellProto->maxLevel;
    else if (level < (int32)spellProto->baseLevel)
        level = (int32)spellProto->baseLevel;
    level -= (int32)spellProto->spellLevel;

    basePoints += int32(level * basePointsPerLevel);
    int32 randomPoints = int32(spellProto->EffectDieSides[effect_index] + level * randomPointsPerLevel);

    switch (randomPoints)
    {
        case 0:
        case 1: basePoints += 1; break;              // range 1..1
        default:
        {
            // range can have positive (1..rand) and negative (rand..1) values, so order its for irand
            int32 randvalue = (randomPoints >= 1)
                ? irand(1, randomPoints)
                : irand(randomPoints, 1);

            basePoints += randvalue;
            break;
        }
    }

    float value = float(basePoints);

    // random damage
    if (Player* unitPlayer = (GetTypeId() == TYPEID_PLAYER) ? (Player*)this : nullptr)
    {
        if (uint8 comboPoints = unitPlayer->GetComboPoints())
            if (float comboDamage = spellProto->EffectPointsPerComboPoint[effect_index])
                value += comboDamage * comboPoints;
    }

    if (Player* modOwner = GetSpellModOwner())
    {
        modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_ALL_EFFECTS, value);

        switch (effect_index)
        {
            case 0:
                modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_EFFECT1, value);
                break;
            case 1:
                modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_EFFECT2, value);
                break;
            case 2:
                modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_EFFECT3, value);
                break;
        }
    }

    if (spellProto->Attributes & SPELL_ATTR_LEVEL_DAMAGE_CALCULATION && spellProto->spellLevel &&
            spellProto->Effect[effect_index] != SPELL_EFFECT_WEAPON_PERCENT_DAMAGE &&
            spellProto->Effect[effect_index] != SPELL_EFFECT_KNOCK_BACK &&
            (spellProto->Effect[effect_index] != SPELL_EFFECT_APPLY_AURA || spellProto->EffectApplyAuraName[effect_index] != SPELL_AURA_MOD_DECREASE_SPEED))
        value = value * 0.25f * exp(getLevel() * (70 - spellProto->spellLevel) / 1000.0f);

    return int32(value);
}

int32 Unit::CalculateSpellDuration(SpellEntry const* spellProto, uint8 effect_index, Unit const* target)
{
    Player* unitPlayer = (GetTypeId() == TYPEID_PLAYER) ? ToPlayer() : NULL;

    uint8 comboPoints = unitPlayer ? unitPlayer->GetComboPoints() : 0;

    int32 minduration = GetSpellDuration(spellProto);
    int32 maxduration = GetSpellMaxDuration(spellProto);

    // Special Cast PvP duration spells
    if (target->GetTypeId() == TYPEID_PLAYER && maxduration > 10000)   // Special Cast Spells must be handled in the core
    {
        if (GetDiminishingReturnsGroupForSpell(spellProto, true) == DIMINISHING_LIMITONLY && 
        (spellProto->EffectApplyAuraName[0] == SPELL_AURA_MOD_DECREASE_SPEED || spellProto->EffectApplyAuraName[1] == SPELL_AURA_MOD_DECREASE_SPEED))
        {
            minduration = minduration > 10000 ? minduration = 10000 : minduration;
            maxduration = maxduration > 10000 ? maxduration = 10000 : maxduration;
        }
        if (spellProto->SpellFamilyFlags & 0x00080000000LL && spellProto->SpellIconID == 692) // Curse of Tongues is 12s in pvp
        {
            minduration = minduration > 12000 ? minduration = 12000 : minduration;
            maxduration = maxduration > 12000 ? maxduration = 12000 : maxduration;
        }
    }

    int32 duration;

    if (minduration != -1 && minduration != maxduration)
        duration = minduration + int32((maxduration - minduration) * comboPoints / 5);
    else
        duration = minduration;

    if (Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_DURATION, duration);

    if (duration > 0)
    {
        int32 mechanic = GetEffectMechanic(spellProto, effect_index);
        // Find total mod value (negative bonus)
        int32 durationMod_always = target->GetTotalAuraModifierByMiscValue(SPELL_AURA_MECHANIC_DURATION_MOD, mechanic);
        int32 durationMod = 0;

        // Find max mod (negative bonus)
        int32 durationMod_not_stack = target->GetMaxNegativeAuraModifierByMiscValue(SPELL_AURA_MECHANIC_DURATION_MOD_NOT_STACK, mechanic);

        // Select strongest negative mod
        if (durationMod_always > durationMod_not_stack)
            durationMod = durationMod_not_stack;
        else
            durationMod = durationMod_always;

        if (durationMod != 0)
            duration = int32(int64(duration) * (100+durationMod) /100);

        if (duration < 0) duration = 0;
    }

    return duration;
}

DiminishingLevels Unit::GetDiminishing(DiminishingGroup group)
{
    for (Diminishing::iterator i = m_Diminishing.begin(); i != m_Diminishing.end(); ++i)
    {
        if (i->DRGroup != group)
            continue;

        if (!i->hitCount)
            return DIMINISHING_LEVEL_1;

        if (!i->hitTime)
            return DIMINISHING_LEVEL_1;

        // If last spell was casted more than 15 seconds ago - reset the count.
        if (i->stack == 0 && getMSTimeDiff(i->hitTime, getMSTime()) > 15000)
        {
            i->hitCount = DIMINISHING_LEVEL_1;
            return DIMINISHING_LEVEL_1;
        }
        // or else increase the count.
        else
            return DiminishingLevels(i->hitCount);
    }
    return DIMINISHING_LEVEL_1;
}

void Unit::IncrDiminishing(DiminishingGroup group)
{
    // Checking for existing in the table
    bool IsExist = false;
    for (Diminishing::iterator i = m_Diminishing.begin(); i != m_Diminishing.end(); ++i)
    {
        if (i->DRGroup != group)
            continue;

        IsExist = true;
        if (i->hitCount < DIMINISHING_LEVEL_IMMUNE)
            i->hitCount += 1;

        break;
    }

    if (!IsExist)
        m_Diminishing.push_back(DiminishingReturn(group, getMSTime(), DIMINISHING_LEVEL_2));
}

void Unit::ApplyDiminishingToDuration(DiminishingGroup group, int32 &duration, Unit* caster, DiminishingLevels Level, SpellEntry const *spellInfo)
{
    if (duration == -1 || group == DIMINISHING_NONE)/*(caster->IsFriendlyTo(this) && caster != this)*/
        return;

    // test pet/charm masters instead pets/charmedsz
    Unit const* targetOwner = GetCharmerOrOwner();
    Unit const* casterOwner = caster->GetCharmerOrOwner();

    // Duration of crowd control abilities on pvp target is limited by 10 sec. (2.2.0)
    if (duration > 10000 && IsDiminishingReturnsGroupDurationLimited(group))
    {
        Unit const* target = targetOwner ? targetOwner : this;
        Unit const* source = casterOwner ? casterOwner : caster;

        if (target->GetTypeId() == TYPEID_PLAYER && source->GetTypeId() == TYPEID_PLAYER)
        {
            duration = 10000;

            // Curse of Tongues
            if (spellInfo->SpellFamilyName == SPELLFAMILY_WARLOCK && spellInfo->SpellFamilyFlags & 0x80000000LL)
                duration = 12000;

            if (spellInfo)
                ((Player*)source)->ApplySpellMod(spellInfo->Id, SPELLMOD_DURATION, duration);
        }
    }

    float mod = 1.0f;

    // Some diminishings applies to mobs too (for example, Stun)
    if ((GetDiminishingReturnsGroupType(group) == DRTYPE_PLAYER && (targetOwner ? targetOwner->GetTypeId():GetTypeId()) == TYPEID_PLAYER) || GetDiminishingReturnsGroupType(group) == DRTYPE_ALL)
    {
        DiminishingLevels diminish = Level;
        switch (diminish)
        {
            case DIMINISHING_LEVEL_1: break;
            case DIMINISHING_LEVEL_2: mod = 0.5f; break;
            case DIMINISHING_LEVEL_3: mod = 0.25f; break;
            case DIMINISHING_LEVEL_IMMUNE: mod = 0.0f; break;
            default: break;
        }
    }

    duration = int32(duration * mod);
}

void Unit::ApplyDiminishingAura(DiminishingGroup group, bool apply)
{
    // Checking for existing in the table
    for (Diminishing::iterator i = m_Diminishing.begin(); i != m_Diminishing.end(); ++i)
    {
        if (i->DRGroup != group)
            continue;

        i->hitTime = getMSTime();

        if (apply)
            i->stack += 1;
        else if (i->stack)
            i->stack -= 1;

        break;
    }
}

uint32 Unit::GetSpellRadiusForTarget(Unit* target, const SpellRadiusEntry * radiusEntry)
{
    if (!radiusEntry)
        return 0;
    if (radiusEntry->radiusHostile == radiusEntry->radiusFriend)
        return radiusEntry->radiusFriend;
    if (IsHostileTo(target))
        return radiusEntry->radiusHostile;
    return radiusEntry->radiusFriend;
};

Unit* Unit::GetUnit(WorldObject& object, uint64 guid)
{
    return ObjectAccessor::GetUnit(object, guid);
}

Player* Unit::GetPlayer(WorldObject& object, uint64 guid)
{
    return ObjectAccessor::GetPlayer(object, guid);
}

Creature* Unit::GetCreature(WorldObject& object, uint64 guid)
{
    return object.GetMap()->GetCreature(guid);
}

uint32 Unit::GetCreatureType() const
{
    if (GetTypeId() == TYPEID_PLAYER)
    {
        SpellShapeshiftEntry const* ssEntry = sSpellShapeshiftStore.LookupEntry(ToPlayer()->m_form);
        if (ssEntry && ssEntry->creatureType > 0)
            return ssEntry->creatureType;
        else
            return CREATURE_TYPE_HUMANOID;
    }
    else
        return ToCreature()->GetCreatureTemplate()->type;
}

/*#######################################
########                         ########
########       STAT SYSTEM       ########
########                         ########
#######################################*/

bool Unit::HandleStatModifier(UnitMods unitMod, UnitModifierType modifierType, float amount, bool apply)
{
    if (unitMod >= UNIT_MOD_END || modifierType >= MODIFIER_TYPE_END)
    {
        sLog->outError("ERROR in HandleStatModifier(): Invalid UnitMods or invalid UnitModifierType!");
        return false;
    }

    float val = 1.0f;

    switch (modifierType)
    {
        case BASE_VALUE:
        case TOTAL_VALUE:
            m_auraModifiersGroup[unitMod][modifierType] += apply ? amount : -amount;
            break;
        case BASE_PCT:
        case TOTAL_PCT:
            if (amount <= -100.0f)                           //small hack-fix for -100% modifiers
                amount = -200.0f;

            val = (100.0f + amount) / 100.0f;
            m_auraModifiersGroup[unitMod][modifierType] *= apply ? val : (1.0f/val);
            break;

        default:
            break;
    }

    if (!CanModifyStats())
        return false;

    switch (unitMod)
    {
        case UNIT_MOD_STAT_STRENGTH:
        case UNIT_MOD_STAT_AGILITY:
        case UNIT_MOD_STAT_STAMINA:
        case UNIT_MOD_STAT_INTELLECT:
        case UNIT_MOD_STAT_SPIRIT:         UpdateStats(GetStatByAuraGroup(unitMod));  break;

        case UNIT_MOD_ARMOR:               UpdateArmor();           break;
        case UNIT_MOD_HEALTH:              UpdateMaxHealth();       break;

        case UNIT_MOD_MANA:
        case UNIT_MOD_RAGE:
        case UNIT_MOD_FOCUS:
        case UNIT_MOD_ENERGY:
        case UNIT_MOD_HAPPINESS:           UpdateMaxPower(GetPowerTypeByAuraGroup(unitMod));         break;

        case UNIT_MOD_RESISTANCE_HOLY:
        case UNIT_MOD_RESISTANCE_FIRE:
        case UNIT_MOD_RESISTANCE_NATURE:
        case UNIT_MOD_RESISTANCE_FROST:
        case UNIT_MOD_RESISTANCE_SHADOW:
        case UNIT_MOD_RESISTANCE_ARCANE:   UpdateResistances(GetSpellSchoolByAuraGroup(unitMod));      break;

        case UNIT_MOD_ATTACK_POWER:        UpdateAttackPowerAndDamage();         break;
        case UNIT_MOD_ATTACK_POWER_RANGED: UpdateAttackPowerAndDamage(true);     break;

        case UNIT_MOD_DAMAGE_MAINHAND:     UpdateDamagePhysical(BASE_ATTACK);    break;
        case UNIT_MOD_DAMAGE_OFFHAND:      UpdateDamagePhysical(OFF_ATTACK);     break;
        case UNIT_MOD_DAMAGE_RANGED:       UpdateDamagePhysical(RANGED_ATTACK);  break;

        default:
            break;
    }

    return true;
}

float Unit::GetModifierValue(UnitMods unitMod, UnitModifierType modifierType) const
{
    if (unitMod >= UNIT_MOD_END || modifierType >= MODIFIER_TYPE_END)
    {
        sLog->outError("GetModifierValue: Invalid modifier value from UnitMods!");
        return 0.0f;
    }

    if (modifierType == TOTAL_PCT && m_auraModifiersGroup[unitMod][modifierType] <= 0.0f)
        return 0.0f;

    return m_auraModifiersGroup[unitMod][modifierType];
}

float Unit::GetTotalStatValue(Stats stat) const
{
    UnitMods unitMod = UnitMods(UNIT_MOD_STAT_START + stat);

    if (m_auraModifiersGroup[unitMod][TOTAL_PCT] <= 0.0f)
        return 0.0f;

    // value = ((base_value * base_pct) + total_value) * total_pct
    float value  = m_auraModifiersGroup[unitMod][BASE_VALUE] + GetCreateStat(stat);
    value *= m_auraModifiersGroup[unitMod][BASE_PCT];
    value += m_auraModifiersGroup[unitMod][TOTAL_VALUE];
    value *= m_auraModifiersGroup[unitMod][TOTAL_PCT];

    return value;
}

float Unit::GetTotalAuraModValue(UnitMods unitMod) const
{
    if (unitMod >= UNIT_MOD_END)
    {
        sLog->outError("Invalid UnitMods in GetTotalAuraModValue()!");
        return 0.0f;
    }

    if (m_auraModifiersGroup[unitMod][TOTAL_PCT] <= 0.0f)
        return 0.0f;

    float value = m_auraModifiersGroup[unitMod][BASE_VALUE];
    value *= m_auraModifiersGroup[unitMod][BASE_PCT];
    value += m_auraModifiersGroup[unitMod][TOTAL_VALUE];
    value *= m_auraModifiersGroup[unitMod][TOTAL_PCT];

    return value;
}

SpellSchools Unit::GetSpellSchoolByAuraGroup(UnitMods unitMod) const
{
    SpellSchools school = SPELL_SCHOOL_NORMAL;

    switch (unitMod)
    {
        case UNIT_MOD_RESISTANCE_HOLY:     school = SPELL_SCHOOL_HOLY;          break;
        case UNIT_MOD_RESISTANCE_FIRE:     school = SPELL_SCHOOL_FIRE;          break;
        case UNIT_MOD_RESISTANCE_NATURE:   school = SPELL_SCHOOL_NATURE;        break;
        case UNIT_MOD_RESISTANCE_FROST:    school = SPELL_SCHOOL_FROST;         break;
        case UNIT_MOD_RESISTANCE_SHADOW:   school = SPELL_SCHOOL_SHADOW;        break;
        case UNIT_MOD_RESISTANCE_ARCANE:   school = SPELL_SCHOOL_ARCANE;        break;

        default:
            break;
    }

    return school;
}

Stats Unit::GetStatByAuraGroup(UnitMods unitMod) const
{
    Stats stat = STAT_STRENGTH;

    switch (unitMod)
    {
        case UNIT_MOD_STAT_STRENGTH:    stat = STAT_STRENGTH;      break;
        case UNIT_MOD_STAT_AGILITY:     stat = STAT_AGILITY;       break;
        case UNIT_MOD_STAT_STAMINA:     stat = STAT_STAMINA;       break;
        case UNIT_MOD_STAT_INTELLECT:   stat = STAT_INTELLECT;     break;
        case UNIT_MOD_STAT_SPIRIT:      stat = STAT_SPIRIT;        break;

        default:
            break;
    }

    return stat;
}

Powers Unit::GetPowerTypeByAuraGroup(UnitMods unitMod) const
{
    switch (unitMod)
    {
        case UNIT_MOD_RAGE:        return POWER_RAGE;
        case UNIT_MOD_FOCUS:       return POWER_FOCUS;
        case UNIT_MOD_ENERGY:      return POWER_ENERGY;
        case UNIT_MOD_HAPPINESS:   return POWER_HAPPINESS;
        default:
        case UNIT_MOD_MANA:        return POWER_MANA;
    }
}

float Unit::GetTotalAttackPowerValue(WeaponAttackType attType) const
{
    if (attType == RANGED_ATTACK)
    {
        int32 ap = GetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER) + GetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER_MODS);
        if (ap < 0)
            return 0.0f;
        return ap * (1.0f + GetFloatValue(UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER));
    }
    else
    {
        int32 ap = GetInt32Value(UNIT_FIELD_ATTACK_POWER) + GetInt32Value(UNIT_FIELD_ATTACK_POWER_MODS);
        if (ap < 0)
            return 0.0f;
        return ap * (1.0f + GetFloatValue(UNIT_FIELD_ATTACK_POWER_MULTIPLIER));
    }
}

float Unit::GetWeaponDamageRange(WeaponAttackType attType , WeaponDamageRange type) const
{
    if (attType == OFF_ATTACK && !haveOffhandWeapon())
        return 0.0f;

    return m_weaponDamage[attType][type];
}

void Unit::SetLevel(uint32 lvl)
{
    SetUInt32Value(UNIT_FIELD_LEVEL, lvl);

    // group update
    if ((GetTypeId() == TYPEID_PLAYER) && ToPlayer()->GetGroup())
        ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_LEVEL);
}

void Unit::SetHealth(uint32 val)
{
    if (getDeathState() == JUST_DIED)
        val = 0;
    else
    {
        uint32 maxHealth = GetMaxHealth();
        if (maxHealth < val)
            val = maxHealth;
    }

    SetUInt32Value(UNIT_FIELD_HEALTH, val);

    // group update
    if (GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)this)->m_arenaSpectatorFlags |= ARENASPEC_HEALTH;
        if (ToPlayer()->GetGroup())
            ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_CUR_HP);
    }
    else if (ToCreature()->isPet())
    {
        Pet *pet = ((Pet*)this);
        if (pet->isControlled())
        {
            Unit *owner = GetOwner();
            if (owner && (owner->GetTypeId() == TYPEID_PLAYER) && owner->ToPlayer()->GetGroup())
                owner->ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_CUR_HP);
        }
    }
}

void Unit::SetMaxHealth(uint32 val)
{
    uint32 health = GetHealth();
    SetUInt32Value(UNIT_FIELD_MAXHEALTH, val);

    // group update
    if (GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)this)->m_arenaSpectatorFlags |= ARENASPEC_MAXHEALTH;
        if (ToPlayer()->GetGroup())
            ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_MAX_HP);
    }
    else if (ToCreature()->isPet())
    {
        Pet *pet = ((Pet*)this);
        if (pet->isControlled())
        {
            Unit *owner = GetOwner();
            if (owner && (owner->GetTypeId() == TYPEID_PLAYER) && owner->ToPlayer()->GetGroup())
                owner->ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_MAX_HP);
        }
    }

    if (val < health)
        SetHealth(val);
}

void Unit::SetPower(Powers power, uint32 val)
{
    if (GetPower(power) == val)
        return;

    uint32 maxPower = GetMaxPower(power);
    if (maxPower < val)
        val = maxPower;

    SetStatInt32Value(UNIT_FIELD_POWER1 + power, val);

    // group update
    if (GetTypeId() == TYPEID_PLAYER)
    {
        ToPlayer()->m_arenaSpectatorFlags |= ARENASPEC_POWER;

        if (ToPlayer()->GetGroup())
            ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_CUR_POWER);
    }
    else if (ToCreature()->isPet())
    {
        Pet *pet = ((Pet*)this);
        if (pet->isControlled())
        {
            Unit *owner = GetOwner();
            if (owner && (owner->GetTypeId() == TYPEID_PLAYER) && owner->ToPlayer()->GetGroup())
                owner->ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_CUR_POWER);
        }

        // Update the pet's character sheet with happiness damage bonus
        if (pet->getPetType() == HUNTER_PET && power == POWER_HAPPINESS)
            pet->UpdateDamagePhysical(BASE_ATTACK);
    }
}

void Unit::SetMaxPower(Powers power, uint32 val)
{
    uint32 cur_power = GetPower(power);
    SetStatInt32Value(UNIT_FIELD_MAXPOWER1 + power, val);

    // group update
    if (GetTypeId() == TYPEID_PLAYER)
    {
        if (ToPlayer()->GetGroup())
            ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_MAX_POWER);
    }
    else if (ToCreature()->isPet())
    {
        Pet *pet = ((Pet*)this);
        if (pet->isControlled())
        {
            Unit *owner = GetOwner();
            if (owner && (owner->GetTypeId() == TYPEID_PLAYER) && owner->ToPlayer()->GetGroup())
                owner->ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_MAX_POWER);
        }
    }

    if (val < cur_power)
        SetPower(power, val);
}

void Unit::ApplyPowerMod(Powers power, uint32 val, bool apply)
{
    ApplyModUInt32Value(UNIT_FIELD_POWER1+power, val, apply);

    // group update
    if (GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)this)->m_arenaSpectatorFlags |= ARENASPEC_POWER;
        if (ToPlayer()->GetGroup())
            ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_CUR_POWER);
    }
    else if (ToCreature()->isPet())
    {
        Pet *pet = ((Pet*)this);
        if (pet->isControlled())
        {
            Unit *owner = GetOwner();
            if (owner && (owner->GetTypeId() == TYPEID_PLAYER) && owner->ToPlayer()->GetGroup())
                owner->ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_CUR_POWER);
        }
    }
}

void Unit::ApplyMaxPowerMod(Powers power, uint32 val, bool apply)
{
    ApplyModUInt32Value(UNIT_FIELD_MAXPOWER1+power, val, apply);

    // group update
    if (GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)this)->m_arenaSpectatorFlags |= ARENASPEC_MAXPOWER;
        if (ToPlayer()->GetGroup())
            ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_MAX_POWER);
    }
    else if (ToCreature()->isPet())
    {
        Pet *pet = ((Pet*)this);
        if (pet->isControlled())
        {
            Unit *owner = GetOwner();
            if (owner && (owner->GetTypeId() == TYPEID_PLAYER) && owner->ToPlayer()->GetGroup())
                owner->ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_MAX_POWER);
        }
    }
}

void Unit::ApplyAuraProcTriggerDamage(Aura* aura, bool apply)
{
    AuraList& tAuraProcTriggerDamage = m_modAuras[SPELL_AURA_PROC_TRIGGER_DAMAGE];
    if (apply)
        tAuraProcTriggerDamage.push_back(aura);
    else
        tAuraProcTriggerDamage.remove(aura);
}

uint32 Unit::GetCreatePowers(Powers power) const
{
    // POWER_FOCUS and POWER_HAPPINESS only have hunter pet
    switch (power)
    {
        case POWER_MANA:      return GetCreateMana();
        case POWER_RAGE:      return 1000;
        case POWER_FOCUS:     return (GetTypeId() == TYPEID_PLAYER || !ToCreature()->isPet() || ((Pet const*)this)->getPetType() != HUNTER_PET ? 0 : 100);
        case POWER_ENERGY:    return 100;
        case POWER_HAPPINESS: return (GetTypeId() == TYPEID_PLAYER || !ToCreature()->isPet() || ((Pet const*)this)->getPetType() != HUNTER_PET ? 0 : 1050000);
    }

    return 0;
}

void Unit::AddToWorld()
{
    if (!IsInWorld())
    {
        WorldObject::AddToWorld();
    }
}

void Unit::RemoveFromWorld()
{
    // cleanup
    if (IsInWorld())
    {
        UnsummonAllTotems();
        RemoveAllControlled();
        RemoveCharmAuras();
        RemoveBindSightAuras();
        RemoveNotOwnSingleTargetAuras();

        RemoveAllGameObjects();
        RemoveAllDynObjects();

        if (GetCharmerGUID())
            sLog->outCrash("Unit %u has charmer guid when removed from world", GetEntry());

        WorldObject::RemoveFromWorld();
    }
}

void Unit::CleanupsBeforeDelete()
{
    // This needs to be before RemoveFromWorld to make GetCaster() return a valid pointer on aura removal
    InterruptNonMeleeSpells(true);

    if (IsInWorld())
        RemoveFromWorld();

    ASSERT(GetGUID());

    //A unit may be in removelist and not in world, but it is still in grid
    //and may have some references during delete
    RemoveAllAuras();
    m_Events.KillAllEvents(false);                      // non-delatable (currently casted spells) will not deleted now but it will deleted at call in Map::RemoveAllObjectsInRemoveList
    CombatStop();
    ClearComboPointHolders();
    DeleteThreatList();
    getHostileRefManager().setOnlineOfflineState(false);
    GetMotionMaster()->Clear(false);                    // remove different non-standard movement generators.
}

void Unit::UpdateCharmAI()
{
    if (GetTypeId() == TYPEID_PLAYER)
        return;

    if (i_disabledAI) // disabled AI must be primary AI
    {
        if (!isCharmed())
        {
            delete i_AI;
            i_AI = i_disabledAI;
            i_disabledAI = NULL;
        }
    }
    else
    {
        if (isCharmed())
        {
            i_disabledAI = i_AI;
            if (isPossessed())
                i_AI = new PossessedAI(ToCreature());
            else
                i_AI = new PetAI(ToCreature());
        }
    }
}

CharmInfo* Unit::InitCharmInfo()
{
    if (!m_charmInfo)
        m_charmInfo = new CharmInfo(this);

    return m_charmInfo;
}

void Unit::DeleteCharmInfo()
{
    delete m_charmInfo;
    m_charmInfo = NULL;
}

CharmInfo::CharmInfo(Unit* unit)
: m_unit(unit), m_CommandState(COMMAND_FOLLOW), m_petnumber(0), m_barInit(false)
{
    for (int i =0; i < 4; ++i)
    {
        m_charmspells[i].spellId = 0;
        m_charmspells[i].active = ACT_DISABLED;
    }
    if (m_unit->GetTypeId() == TYPEID_UNIT)
    {
        m_oldReactState = m_unit->ToCreature()->GetReactState();
        m_unit->ToCreature()->SetReactState(REACT_PASSIVE);
    }
}

CharmInfo::~CharmInfo()
{
    if (m_unit->GetTypeId() == TYPEID_UNIT)
        if (Creature* creature = m_unit->ToCreature())
            creature->SetReactState(m_oldReactState);
}

void CharmInfo::InitPetActionBar()
{
    if (m_barInit)
        return;

    // the first 3 SpellOrActions are attack, follow and stay
    for (uint32 i = 0; i < 3; i++)
    {
        PetActionBar[i].Type = ACT_COMMAND;
        PetActionBar[i].SpellOrAction = COMMAND_ATTACK - i;

        PetActionBar[i + 7].Type = ACT_REACTION;
        PetActionBar[i + 7].SpellOrAction = COMMAND_ATTACK - i;
    }
    for (uint32 i = 0; i < 4; i++)
    {
        PetActionBar[i + 3].Type = ACT_DISABLED;
        PetActionBar[i + 3].SpellOrAction = 0;
    }
    m_barInit = true;
}

void CharmInfo::InitEmptyActionBar(bool withAttack)
{
    if (m_barInit)
        return;

    for (uint32 x = 0; x < 10; ++x)
    {
        PetActionBar[x].Type = ACT_CAST;
        PetActionBar[x].SpellOrAction = 0;
    }
    if (withAttack)
    {
        PetActionBar[0].Type = ACT_COMMAND;
        PetActionBar[0].SpellOrAction = COMMAND_ATTACK;
    }
    m_barInit = true;
}

void CharmInfo::InitPossessCreateSpells()
{
    InitEmptyActionBar();
    if (m_unit->GetTypeId() == TYPEID_UNIT)
    {
        for (uint32 i = 0; i < CREATURE_MAX_SPELLS; ++i)
        {
            uint32 spellid = m_unit->ToCreature()->m_spells[i];
            if (IsPassiveSpell(spellid))
                m_unit->CastSpell(m_unit, spellid, true);
            else
                AddSpellToActionBar(0, spellid, ACT_CAST);
        }
    }
}

void CharmInfo::InitCharmCreateSpells()
{
    if (m_unit->GetTypeId() == TYPEID_PLAYER)                //charmed players don't have spells
    {
        InitEmptyActionBar();
        return;
    }

    InitPetActionBar();

    for (uint32 x = 0; x < MAX_SPELL_CHARM; ++x)
    {
        uint32 spellId = m_unit->ToCreature()->m_spells[x];
        m_charmspells[x].spellId = spellId;

        if (!spellId)
            continue;

        if (IsPassiveSpell(spellId))
        {
            m_unit->CastSpell(m_unit, spellId, true);
            m_charmspells[x].active = ACT_PASSIVE;
        }
        else
        {
            ActiveStates newstate;
            bool onlyselfcast = true;
            SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId);

            if (!spellInfo) onlyselfcast = false;
            for (uint32 i = 0;i < 3 && onlyselfcast;++i)       //non existent spell will not make any problems as onlyselfcast would be false -> break right away
            {
                if (spellInfo->EffectImplicitTargetA[i] != TARGET_UNIT_CASTER && spellInfo->EffectImplicitTargetA[i] != 0)
                    onlyselfcast = false;
            }

            if (onlyselfcast || !IsPositiveSpell(spellId))   //only self cast and spells versus enemies are autocastable
                newstate = ACT_DISABLED;
            else
                newstate = ACT_CAST;

            AddSpellToActionBar(0, spellId, newstate);
        }
    }
}

bool CharmInfo::AddSpellToActionBar(uint32 oldid, uint32 newid, ActiveStates newstate)
{
    for (uint8 i = 0; i < 10; i++)
    {
        if ((PetActionBar[i].Type == ACT_DISABLED || PetActionBar[i].Type == ACT_ENABLED || PetActionBar[i].Type == ACT_CAST) && PetActionBar[i].SpellOrAction == oldid)
        {
            PetActionBar[i].SpellOrAction = newid;
            if (!oldid)
            {
                if (newstate == ACT_DECIDE)
                    PetActionBar[i].Type = ACT_DISABLED;
                else
                    PetActionBar[i].Type = newstate;
            }

            return true;
        }
    }
    return false;
}

void CharmInfo::ToggleCreatureAutocast(uint32 spellid, bool apply)
{
    if (IsPassiveSpell(spellid))
        return;

    for (uint32 x = 0; x < MAX_SPELL_CHARM; ++x)
    {
        if (spellid == m_charmspells[x].spellId)
        {
            m_charmspells[x].active = apply ? ACT_ENABLED : ACT_DISABLED;
        }
    }
}

void CharmInfo::SetPetNumber(uint32 petnumber, bool statwindow)
{
    m_petnumber = petnumber;
    if (statwindow)
        m_unit->SetUInt32Value(UNIT_FIELD_PETNUMBER, m_petnumber);
    else
        m_unit->SetUInt32Value(UNIT_FIELD_PETNUMBER, 0);
}

bool Unit::isFrozen() const
{
    AuraList const& mRoot = GetAurasByType(SPELL_AURA_MOD_ROOT);
    for (AuraList::const_iterator i = mRoot.begin(); i != mRoot.end(); ++i)
        if (GetSpellSchoolMask((*i)->GetSpellProto()) & SPELL_SCHOOL_MASK_FROST)
            return true;
    return false;
}

/*
struct ProcTriggeredData
{
    ProcTriggeredData(Aura* _triggeredByAura, uint32 _cooldown)
        : triggeredByAura(_triggeredByAura),
        triggeredByAura_SpellPair(Unit::spellEffectPair(triggeredByAura->GetId(), triggeredByAura->GetEffIndex())),
        cooldown(_cooldown)
    {}

    Aura* triggeredByAura;                                  // triggred aura, can be invalidate at triggered aura proccessing
    Unit::spellEffectPair triggeredByAura_SpellPair;        // spell pair, used for re-find aura (by pointer comparison in range)
    uint32 cooldown;                                        // possible hidden cooldown
};

typedef std::list< ProcTriggeredData > ProcTriggeredList;

void Unit::ProcDamageAndSpellFor(bool isVictim, Unit * pTarget, uint32 procFlag, AuraTypeSet const& procAuraTypes, WeaponAttackType attType, SpellEntry const * procSpell, uint32 damage, SpellSchoolMask damageSchoolMask)
{
    for (AuraTypeSet::const_iterator aur = procAuraTypes.begin(); aur != procAuraTypes.end(); ++aur)
    {
        // List of spells (effects) that proceed. Spell prototype and aura-specific value (damage for TRIGGER_DAMAGE)
        ProcTriggeredList procTriggered;

        AuraList const& auras = GetAurasByType(*aur);
        for (AuraList::const_iterator i = auras.begin(), next; i != auras.end(); i = next)
        {
            next = i; ++next;

            Aura* i_aura = *i;

            uint32 cooldown;                                // returned at next line
            if (!IsTriggeredAtSpellProcEvent(i_aura->GetSpellProto(), procSpell, procFlag, attType, isVictim, cooldown))
                continue;

            procTriggered.push_back(ProcTriggeredData(i_aura, cooldown));
        }

        // Handle effects proceed this time
        for (ProcTriggeredList::iterator i = procTriggered.begin(); i != procTriggered.end(); ++i)
        {
            // Some auras can be deleted in function called in this loop (except first, ofc)
            // Until storing auras in std::multimap to hard check deleting by another way
            if (i != procTriggered.begin())
            {
                bool found = false;
                AuraMap::const_iterator lower = GetAuras().lower_bound(i->triggeredByAura_SpellPair);
                AuraMap::const_iterator upper = GetAuras().upper_bound(i->triggeredByAura_SpellPair);
                for (AuraMap::const_iterator itr = lower; itr != upper; ++itr)
                {
                    if (itr->second == i->triggeredByAura)
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    sLog->outError("Spell aura %u (id:%u effect:%u) has been deleted before call spell proc event handler", *aur, i->triggeredByAura_SpellPair.first, i->triggeredByAura_SpellPair.second);
                    sLog->outError("It can be deleted one from early processed auras:");
                    for (ProcTriggeredList::iterator i2 = procTriggered.begin(); i != i2; ++i2)
                        sLog->outError("     Spell aura %u (id:%u effect:%u)", *aur, i2->triggeredByAura_SpellPair.first, i2->triggeredByAura_SpellPair.second);
                    sLog->outError("     <end of list>");
                    continue;
                }
            }

            // this is aura triggering code call
            Aura* triggeredByAura = i->triggeredByAura;

            // save charges existence before processing to prevent crash at access to deleted triggered aura after
            // used in speedup code check before check aura existance.
            bool triggeredByAuraWithCharges =  triggeredByAura->m_procCharges > 0;

            // success in event proccesing
            // used in speedup code check before check aura existance.
            bool casted = false;

            // process triggered code
            switch (*aur)
            {
                case SPELL_AURA_PROC_TRIGGER_SPELL:
                {
                    sLog->outDebug("ProcDamageAndSpell: casting spell (triggered by %s proc aura of spell %u)",
                        (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                    casted = HandleProcTriggerSpell(pTarget, damage, triggeredByAura, procSpell, procFlag, attType, i->cooldown);
                    break;
                }
                case SPELL_AURA_PROC_TRIGGER_DAMAGE:
                {
                    uint32 triggered_damage = triggeredByAura->GetModifier()->m_amount;
                    sLog->outDebug("ProcDamageAndSpell: doing %u damage (triggered by %s aura of spell %u)",
                        triggered_damage, (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                    SpellNonMeleeDamageLog(pTarget, triggeredByAura->GetId(), triggered_damage, true, true);
                    casted = true;
                    break;
                }
                case SPELL_AURA_DUMMY:
                {
                    uint32 effect = triggeredByAura->GetEffIndex();
                    sLog->outDebug("ProcDamageAndSpell: casting spell (triggered by %s dummy aura of spell %u)",
                        (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                    casted = HandleDummyAuraProc(pTarget, damage, triggeredByAura, procSpell, procFlag, i->cooldown);
                    break;
                }
                case SPELL_AURA_PRAYER_OF_MENDING:
                {
                    sLog->outDebug("ProcDamageAndSpell: casting mending (triggered by %s dummy aura of spell %u)",
                        (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());

                    casted = HandleMendingAuraProc(triggeredByAura);
                    break;
                }
                case SPELL_AURA_MOD_HASTE:
                {
                    sLog->outDebug("ProcDamageAndSpell: casting spell (triggered by %s haste aura of spell %u)",
                        (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                    casted = HandleHasteAuraProc(pTarget, damage, triggeredByAura, procSpell, procFlag, i->cooldown);
                    break;
                }
                case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
                {
                    // nothing do, just charges counter
                    // but count only in case appropriate school damage
                    casted = triggeredByAura->GetModifier()->m_miscvalue & damageSchoolMask;
                    break;
                }
                case SPELL_AURA_OVERRIDE_CLASS_SCRIPTS:
                {
                    sLog->outDebug("ProcDamageAndSpell: casting spell (triggered by %s class script aura of spell %u)",
                        (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                    casted = HandleOverrideClassScriptAuraProc(pTarget, triggeredByAura, procSpell, i->cooldown);
                    break;
                }
                default:
                {
                    // nothing do, just charges counter
                    casted = true;
                    break;
                }
            }

            // Update charge (aura can be removed by triggers)
            if (casted && triggeredByAuraWithCharges)
            {
                // need re-found aura (can be dropped by triggers)
                AuraMap::const_iterator lower = GetAuras().lower_bound(i->triggeredByAura_SpellPair);
                AuraMap::const_iterator upper = GetAuras().upper_bound(i->triggeredByAura_SpellPair);
                for (AuraMap::const_iterator itr = lower; itr != upper; ++itr)
                {
                    if (itr->second == triggeredByAura)      // pointer still valid
                    {
                        if (triggeredByAura->m_procCharges > 0)
                            triggeredByAura->m_procCharges -= 1;

                        triggeredByAura->UpdateAuraCharges();
                        break;
                    }
                }
            }
        }

        // Safely remove auras with zero charges
        for (AuraList::const_iterator i = auras.begin(), next; i != auras.end(); i = next)
        {
            next = i; ++next;
            if ((*i)->m_procCharges == 0)
            {
                RemoveAurasDueToSpell((*i)->GetId());
                next = auras.begin();
            }
        }
    }
}
*/
struct ProcTriggeredData
{
    ProcTriggeredData(SpellProcEventEntry const * _spellProcEvent, Aura* _triggeredByAura)
        : spellProcEvent(_spellProcEvent), triggeredByAura(_triggeredByAura),
        triggeredByAura_SpellPair(Unit::spellEffectPair(triggeredByAura->GetId(), triggeredByAura->GetEffIndex()))
        {}
    SpellProcEventEntry const *spellProcEvent;
    Aura* triggeredByAura;
    Unit::spellEffectPair triggeredByAura_SpellPair;
};

typedef std::list< ProcTriggeredData > ProcTriggeredList;
typedef std::list< uint32> RemoveSpellList;

// List of auras that CAN be trigger but may not exist in spell_proc_event
// in most case need for drop charges
// in some types of aura need do additional check
// for example SPELL_AURA_MECHANIC_IMMUNITY - need check for mechanic
static bool isTriggerAura[TOTAL_AURAS];
static bool isNonTriggerAura[TOTAL_AURAS];
void InitTriggerAuraData()
{
    for (uint16 i = 0; i < TOTAL_AURAS; ++i)
    {
        isTriggerAura[i]=false;
        isNonTriggerAura[i] = false;
    }
    isTriggerAura[SPELL_AURA_DUMMY] = true;
    isTriggerAura[SPELL_AURA_MOD_CONFUSE] = true;
    isTriggerAura[SPELL_AURA_MOD_THREAT] = true;
    isTriggerAura[SPELL_AURA_MOD_STUN] = true; // Aura not have charges but need remove him on trigger
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_DONE] = true;
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_TAKEN] = true;
    isTriggerAura[SPELL_AURA_MOD_RESISTANCE] = true;
    isTriggerAura[SPELL_AURA_MOD_ROOT] = true;
    isTriggerAura[SPELL_AURA_REFLECT_SPELLS] = true;
    isTriggerAura[SPELL_AURA_DAMAGE_IMMUNITY] = true;
    isTriggerAura[SPELL_AURA_PROC_TRIGGER_SPELL] = true;
    isTriggerAura[SPELL_AURA_PROC_TRIGGER_DAMAGE] = true;
    isTriggerAura[SPELL_AURA_MOD_CASTING_SPEED] = true;
    isTriggerAura[SPELL_AURA_MOD_POWER_COST_SCHOOL_PCT] = true;
    isTriggerAura[SPELL_AURA_MOD_POWER_COST_SCHOOL] = true;
    isTriggerAura[SPELL_AURA_REFLECT_SPELLS_SCHOOL] = true;
    isTriggerAura[SPELL_AURA_MECHANIC_IMMUNITY] = true;
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN] = true;
    isTriggerAura[SPELL_AURA_SPELL_MAGNET] = true;
    isTriggerAura[SPELL_AURA_MOD_ATTACK_POWER] = true;
    isTriggerAura[SPELL_AURA_ADD_CASTER_HIT_TRIGGER] = true;
    isTriggerAura[SPELL_AURA_OVERRIDE_CLASS_SCRIPTS] = true;
    isTriggerAura[SPELL_AURA_MOD_MECHANIC_RESISTANCE] = true;
    isTriggerAura[SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS] = true;
    isTriggerAura[SPELL_AURA_MOD_HASTE] = true;
    isTriggerAura[SPELL_AURA_MOD_ATTACKER_MELEE_HIT_CHANCE]=true;
    isTriggerAura[SPELL_AURA_PRAYER_OF_MENDING] = true;
    isTriggerAura[SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE] = true;

    isNonTriggerAura[SPELL_AURA_MOD_POWER_REGEN]=true;
    isNonTriggerAura[SPELL_AURA_RESIST_PUSHBACK]=true;
}

uint32 createProcExtendMask(SpellNonMeleeDamage *damageInfo, SpellMissInfo missCondition)
{
    uint32 procEx = PROC_EX_NONE;
    // Check victim state
    if (missCondition != SPELL_MISS_NONE)
        switch (missCondition)
        {
            case SPELL_MISS_MISS:    procEx |= PROC_EX_MISS;   break;
            case SPELL_MISS_RESIST:  procEx |= PROC_EX_RESIST; break;
            case SPELL_MISS_DODGE:   procEx |= PROC_EX_DODGE;  break;
            case SPELL_MISS_PARRY:   procEx |= PROC_EX_PARRY;  break;
            case SPELL_MISS_BLOCK:   procEx |= PROC_EX_BLOCK;  break;
            case SPELL_MISS_EVADE:   procEx |= PROC_EX_EVADE;  break;
            case SPELL_MISS_IMMUNE:  procEx |= PROC_EX_IMMUNE; break;
            case SPELL_MISS_IMMUNE2: procEx |= PROC_EX_IMMUNE; break;
            case SPELL_MISS_DEFLECT: procEx |= PROC_EX_DEFLECT;break;
            case SPELL_MISS_ABSORB:  procEx |= PROC_EX_ABSORB; break;
            case SPELL_MISS_REFLECT: procEx |= PROC_EX_REFLECT;break;
            default:
                break;
        }
    else
    {
        // On block
        if (damageInfo->blocked)
            procEx |= PROC_EX_BLOCK;
        // On absorb
        if (damageInfo->absorb)
            procEx |= PROC_EX_ABSORB;
        // On crit
        if (damageInfo->HitInfo & SPELL_HIT_TYPE_CRIT)
            procEx |= PROC_EX_CRITICAL_HIT;
        else
            procEx |= PROC_EX_NORMAL_HIT;
    }
    return procEx;
}

void Unit::ProcDamageAndSpellFor(bool isVictim, Unit * pTarget, uint32 procFlag, uint32 procExtra, WeaponAttackType attType, SpellEntry const * procSpell, uint32 damage)
{
    ++m_procDeep;
    if (m_procDeep > 5)
    {
        sLog->outError("Prevent possible stack owerflow in Unit::ProcDamageAndSpellFor");
        if (procSpell)
            sLog->outError("  Spell %u", procSpell->Id);
        --m_procDeep;
        return;
    }
    // For melee/ranged based attack need update skills and set some Aura states
    if (procFlag & MELEE_BASED_TRIGGER_MASK)
    {
        // Update skills here for players
        if (GetTypeId() == TYPEID_PLAYER)
        {
            // On melee based hit/miss/resist need update skill (for victim and attacker)
            if (procExtra&(PROC_EX_NORMAL_HIT|PROC_EX_MISS|PROC_EX_RESIST))
            {
                if (pTarget->GetTypeId() != TYPEID_PLAYER && pTarget->GetCreatureType() != CREATURE_TYPE_CRITTER)
                    ToPlayer()->UpdateCombatSkills(pTarget, attType, MELEE_HIT_MISS, isVictim);
            }
            // Update defence if player is victim and parry/dodge/block
            else if (isVictim && procExtra&(PROC_EX_DODGE|PROC_EX_PARRY|PROC_EX_BLOCK))
                ToPlayer()->UpdateDefense();
        }
        // If exist crit/parry/dodge/block need update aura state (for victim and attacker)
        if (procExtra & (PROC_EX_CRITICAL_HIT|PROC_EX_PARRY|PROC_EX_DODGE|PROC_EX_BLOCK))
        {
            // for victim
            if (isVictim)
            {
                // if victim and dodge attack
                if (procExtra&PROC_EX_DODGE)
                {
                    //Update AURA_STATE on dodge
                    if (getClass() != CLASS_ROGUE) // skip Rogue Riposte
                    {
                        ModifyAuraState(AURA_STATE_DEFENSE, true);
                        StartReactiveTimer(REACTIVE_DEFENSE);
                    }
                }
                // if victim and parry attack
                if (procExtra & PROC_EX_PARRY)
                {
                    // For Hunters only Counterattack (skip Mongoose bite)
                    if (getClass() == CLASS_HUNTER)
                    {
                        ModifyAuraState(AURA_STATE_HUNTER_PARRY, true);
                        StartReactiveTimer(REACTIVE_HUNTER_PARRY);
                    }
                    else
                    {
                        ModifyAuraState(AURA_STATE_DEFENSE, true);
                        StartReactiveTimer(REACTIVE_DEFENSE);
                    }
                }
                // if and victim block attack
                if (procExtra & PROC_EX_BLOCK)
                {
                    ModifyAuraState(AURA_STATE_DEFENSE, true);
                    StartReactiveTimer(REACTIVE_DEFENSE);
                }
            }
            else //For attacker
            {
                // Overpower on victim dodge
                if (procExtra&PROC_EX_DODGE && GetTypeId() == TYPEID_PLAYER && getClass() == CLASS_WARRIOR)
                {
                    ToPlayer()->AddComboPoints(pTarget, 1);
                    StartReactiveTimer(REACTIVE_OVERPOWER);
                }
                // Enable AURA_STATE_CRIT on crit
                if (procExtra & PROC_EX_CRITICAL_HIT)
                {
                    ModifyAuraState(AURA_STATE_CRIT, true);
                    StartReactiveTimer(REACTIVE_CRIT);
                    if (getClass() == CLASS_HUNTER)
                    {
                        ModifyAuraState(AURA_STATE_HUNTER_CRIT_STRIKE, true);
                        StartReactiveTimer(REACTIVE_HUNTER_CRIT);
                    }
                }
            }
        }
    }

    RemoveSpellList removedSpells;
    ProcTriggeredList procTriggered;
    // Fill procTriggered list
    for (AuraMap::const_iterator itr = GetAuras().begin(); itr != GetAuras().end(); ++itr)
    {
        SpellProcEventEntry const* spellProcEvent = NULL;
        bool active = (damage > 0) || (procExtra & PROC_EX_ABSORB && isVictim) || procExtra & PROC_EX_INTERNAL_AURA_APPLY;
        if (!IsTriggeredAtSpellProcEvent(pTarget, itr->second, procSpell, procFlag, procExtra, attType, isVictim, active, spellProcEvent))
           continue;

        SpellEntry const *spellProto = itr->second->GetSpellProto();
        if (isVictim && (procExtra & PROC_EX_ABSORB && IsNoAbsorbProcSpell(spellProto)))
            continue;

        procTriggered.push_back(ProcTriggeredData(spellProcEvent, itr->second));
    }
    // Handle effects proceed this time
    for (ProcTriggeredList::iterator i = procTriggered.begin(); i != procTriggered.end(); ++i)
    {
        // Some auras can be deleted in function called in this loop (except first, ofc)
        // Until storing auars in std::multimap to hard check deleting by another way
        if (i != procTriggered.begin())
        {
            bool found = false;
            AuraMap::const_iterator lower = GetAuras().lower_bound(i->triggeredByAura_SpellPair);
            AuraMap::const_iterator upper = GetAuras().upper_bound(i->triggeredByAura_SpellPair);
            for (AuraMap::const_iterator itr = lower; itr != upper; ++itr)
            {
                if (itr->second == i->triggeredByAura)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
//                sLog->outDebug("Spell aura %u (id:%u effect:%u) has been deleted before call spell proc event handler", i->triggeredByAura->GetModifier()->m_auraname, i->triggeredByAura_SpellPair.first, i->triggeredByAura_SpellPair.second);
//                sLog->outDebug("It can be deleted one from early proccesed auras:");
//                for (ProcTriggeredList::iterator i2 = procTriggered.begin(); i != i2; ++i2)
//                    sLog->outDebug("     Spell aura %u (id:%u effect:%u)", i->triggeredByAura->GetModifier()->m_auraname, i2->triggeredByAura_SpellPair.first, i2->triggeredByAura_SpellPair.second);
//                    sLog->outDebug("     <end of list>");
                continue;
            }
        }

        SpellProcEventEntry const *spellProcEvent = i->spellProcEvent;
        Aura *triggeredByAura = i->triggeredByAura;
        Modifier *auraModifier = triggeredByAura->GetModifier();
        SpellEntry const *spellInfo = triggeredByAura->GetSpellProto();
        uint32 effIndex = triggeredByAura->GetEffIndex();
        bool useCharges = triggeredByAura->m_procCharges > 0;
        // For players set spell cooldown if need
        uint32 cooldown = 0;
        if (GetTypeId() == TYPEID_PLAYER && spellProcEvent && spellProcEvent->cooldown)
            cooldown = spellProcEvent->cooldown;

        switch (auraModifier->m_auraname)
        {
            case SPELL_AURA_PROC_TRIGGER_SPELL:
            {
                sLog->outDebug("ProcDamageAndSpell: casting spell %u (triggered by %s aura of spell %u)", spellInfo->Id, (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                // Don`t drop charge or add cooldown for not started trigger
                if (!HandleProcTriggerSpell(pTarget, damage, triggeredByAura, procSpell, procFlag, procExtra, cooldown))
                    continue;
                break;
            }
            case SPELL_AURA_PROC_TRIGGER_DAMAGE:
            {
                sLog->outDebug("ProcDamageAndSpell: doing %u damage from spell id %u (triggered by %s aura of spell %u)", auraModifier->m_amount, spellInfo->Id, (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                SpellNonMeleeDamage damageInfo(this, pTarget, spellInfo->Id, spellInfo->SchoolMask);
                CalculateSpellDamage(&damageInfo, damage, spellInfo);
                SendSpellNonMeleeDamageLog(&damageInfo);
                DealSpellDamage(&damageInfo, true);
                break;
            }
            case SPELL_AURA_MANA_SHIELD:
            case SPELL_AURA_DUMMY:
            {
                sLog->outDebug("ProcDamageAndSpell: casting spell id %u (triggered by %s dummy aura of spell %u)", spellInfo->Id, (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                if (!HandleDummyAuraProc(pTarget, damage, triggeredByAura, procSpell, procFlag, procExtra, cooldown))
                    continue;
                break;
            }
            case SPELL_AURA_MOD_HASTE:
            {
                sLog->outDebug("ProcDamageAndSpell: casting spell id %u (triggered by %s haste aura of spell %u)", spellInfo->Id, (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                if (!HandleHasteAuraProc(pTarget, damage, triggeredByAura, procSpell, procFlag, procExtra, cooldown))
                    continue;
                break;
            }
            case SPELL_AURA_OVERRIDE_CLASS_SCRIPTS:
            {
                sLog->outDebug("ProcDamageAndSpell: casting spell id %u (triggered by %s aura of spell %u)", spellInfo->Id, (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                if (!HandleOverrideClassScriptAuraProc(pTarget, triggeredByAura, procSpell, cooldown))
                    continue;
                break;
            }
            case SPELL_AURA_PRAYER_OF_MENDING:
            {
                sLog->outDebug("ProcDamageAndSpell: casting mending (triggered by %s dummy aura of spell %u)",
                    (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());

                HandleMendingAuraProc(triggeredByAura);
                break;
            }
            case SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE:
            {
                sLog->outDebug("ProcDamageAndSpell: casting spell %u (triggered with value by %s aura of spell %u)", spellInfo->Id, (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());

                if (!HandleProcTriggerSpell(pTarget, damage, triggeredByAura, procSpell, procFlag, procExtra, cooldown))
                    continue;
                break;
            }
            case SPELL_AURA_MOD_STUN:
                // Remove by default, but if charge exist drop it
                if (triggeredByAura->m_procCharges == 0)
                   removedSpells.push_back(triggeredByAura->GetId());
                break;
            case SPELL_AURA_MELEE_ATTACK_POWER_ATTACKER_BONUS:
            case SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS:
                // Hunter's Mark (1-4 Rangs)
                if (spellInfo->SpellFamilyName == SPELLFAMILY_HUNTER && (spellInfo->SpellFamilyFlags&0x0000000000000400LL))
                {
                    int32 basevalue = triggeredByAura->GetBasePoints();
                    triggeredByAura->GetModifier()->m_amount += basevalue / 10;
                    if (triggeredByAura->GetModifier()->m_amount > basevalue * 4)
                        triggeredByAura->GetModifier()->m_amount = basevalue * 4;
                }
                break;
            case SPELL_AURA_MOD_CASTING_SPEED:
                // Skip melee hits or instant cast spells
                if (procSpell == NULL || GetSpellCastTime(procSpell) == 0)
                    continue;
                break;
            case SPELL_AURA_REFLECT_SPELLS_SCHOOL:
                // Skip Melee hits and spells ws wrong school
                if (procSpell == NULL || (auraModifier->m_miscvalue & procSpell->SchoolMask) == 0)
                    continue;
                break;
            case SPELL_AURA_MOD_POWER_COST_SCHOOL_PCT:
            case SPELL_AURA_MOD_POWER_COST_SCHOOL:
                // Skip melee hits and spells ws wrong school or zero cost
                if (procSpell == NULL ||
                    (procSpell->manaCost == 0 && procSpell->ManaCostPercentage == 0) || // Cost check
                    (auraModifier->m_miscvalue & procSpell->SchoolMask) == 0)         // School check
                    continue;
                break;
            case SPELL_AURA_MECHANIC_IMMUNITY:
                // Compare mechanic
                if (procSpell == NULL || procSpell->Mechanic != auraModifier->m_miscvalue || hasUnitState(UNIT_STAT_ISOLATED))
                    continue;
                break;
            case SPELL_AURA_MOD_MECHANIC_RESISTANCE:
                // Compare mechanic
                if (procSpell == NULL || procSpell->Mechanic != auraModifier->m_miscvalue)
                    continue;
                break;
            case SPELL_AURA_ADD_FLAT_MODIFIER:
                // Don't cusume charges for SPELLMOD_COST if zero manacost
                if (spellInfo->EffectMiscValue[effIndex] == SPELLMOD_COST)
                    if (procSpell->manaCost <= 0 || procSpell->ManaCostPercentage <= 0)
                        continue;
                break;
            case SPELL_AURA_ADD_PCT_MODIFIER:
                // Don't cusume charges for SPELLMOD_COST if zero manacost
                if (spellInfo->EffectMiscValue[effIndex] == SPELLMOD_COST)
                    if (procSpell->manaCost <= 0 || procSpell->ManaCostPercentage <= 0)
                        continue;
                break;
            default:
                // nothing do, just charges counter
                break;
        }
        // Remove charge (aura can be removed by triggers)
        if (useCharges)
        {
            // need found aura on drop (can be dropped by triggers)
            AuraMap::const_iterator lower = GetAuras().lower_bound(i->triggeredByAura_SpellPair);
            AuraMap::const_iterator upper = GetAuras().upper_bound(i->triggeredByAura_SpellPair);
            for (AuraMap::const_iterator itr = lower; itr != upper; ++itr)
            {
                if (itr->second == i->triggeredByAura)
                {
                     triggeredByAura->m_procCharges -=1;
                     triggeredByAura->UpdateAuraCharges();
                     if (triggeredByAura->m_procCharges <= 0)
                          removedSpells.push_back(triggeredByAura->GetId());
                    break;
                }
            }
        }
    }
    if (removedSpells.size())
    {
        // Sort spells and remove dublicates
        removedSpells.sort();
        removedSpells.unique();
        // Remove auras from removedAuras
        for (RemoveSpellList::const_iterator i = removedSpells.begin(); i != removedSpells.end();i++)
            RemoveAurasDueToSpell(*i);
    }
    --m_procDeep;
}

SpellSchoolMask Unit::GetMeleeDamageSchoolMask() const
{
    return SPELL_SCHOOL_MASK_NORMAL;
}

Player* Unit::GetSpellModOwner() const
{
    if (GetTypeId() == TYPEID_PLAYER)
        return (Player*)this;
    if (ToCreature()->isPet() || ToCreature()->isTotem())
    {
        Unit* owner = GetOwner();
        if (owner && owner->GetTypeId() == TYPEID_PLAYER)
            return (Player*)owner;
    }
    return NULL;
}

//----------Pet responses methods-----------------
void Unit::SendPetCastFail(uint32 spellid, uint8 msg)
{
    Unit *owner = GetCharmerOrOwner();
    if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_PET_CAST_FAILED, 4 + 1);
    data << uint32(spellid);
    data << uint8(msg);
    owner->ToPlayer()->GetSession()->SendPacket(&data);
}

void Unit::SendPetActionFeedback (uint8 msg)
{
    Unit* owner = GetOwner();
    if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_PET_ACTION_FEEDBACK, 1);
    data << uint8(msg);
    owner->ToPlayer()->GetSession()->SendPacket(&data);
}

void Unit::SendPetTalk (uint32 pettalk)
{
    Unit* owner = GetOwner();
    if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_PET_ACTION_SOUND, 8 + 4);
    data << uint64(GetGUID());
    data << uint32(pettalk);
    owner->ToPlayer()->GetSession()->SendPacket(&data);
}

void Unit::SendPetSpellCooldown (uint32 spellid, time_t cooltime)
{
    Unit* owner = GetOwner();
    if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_SPELL_COOLDOWN, 8+1+4+4);
    data << uint64(GetGUID());
    data << uint8(0x0);                                     // flags (0x1, 0x2)
    data << uint32(spellid);
    data << uint32(cooltime);

    owner->ToPlayer()->GetSession()->SendPacket(&data);
}

void Unit::SendPetClearCooldown (uint32 spellid)
{
    Unit* owner = GetOwner();
    if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_CLEAR_COOLDOWN, (4+8));
    data << uint32(spellid);
    data << uint64(GetGUID());
    owner->ToPlayer()->GetSession()->SendPacket(&data);
}

void Unit::SendPetAIReaction(uint64 guid)
{
    Unit* owner = GetOwner();
    if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_AI_REACTION, 8 + 4);
    data << uint64(guid) << uint32(00000002);
    owner->ToPlayer()->GetSession()->SendPacket(&data);
}

//----------End of Pet responses methods----------

void Unit::StopMoving()
{
    clearUnitState(UNIT_STAT_MOVING);

    // not need send any packets if not in world
    if (!IsInWorld())
        return;

    // send explicit stop packet
    Relocate(GetPositionX(), GetPositionY(), GetPositionZ());

    SendMonsterStop();
}

void Unit::SendMovementFlagUpdate()
{
    WorldPacket data;
    BuildHeartBeatMsg(&data);
    SendMessageToSet(&data, false);
}

bool Unit::IsSitState() const
{
    uint8 s = getStandState();
    return
        s == UNIT_STAND_STATE_SIT_CHAIR        || s == UNIT_STAND_STATE_SIT_LOW_CHAIR  ||
        s == UNIT_STAND_STATE_SIT_MEDIUM_CHAIR || s == UNIT_STAND_STATE_SIT_HIGH_CHAIR ||
        s == UNIT_STAND_STATE_SIT;
}

bool Unit::IsStandState() const
{
    uint8 s = getStandState();
    return !IsSitState() && s != UNIT_STAND_STATE_SLEEP && s != UNIT_STAND_STATE_KNEEL;
}

void Unit::SetStandState(uint8 state)
{
    SetByteValue(UNIT_FIELD_BYTES_1, 0, state);

    if (IsStandState())
        RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_NOT_SEATED);

    if (GetTypeId() == TYPEID_PLAYER)
    {
        WorldPacket data(SMSG_STANDSTATE_UPDATE, 1);
        data << (uint8)state;
        ToPlayer()->GetSession()->SendPacket(&data);
    }
}

bool Unit::IsPolymorphed() const
{
    return GetSpellSpecific(getTransForm()) == SPELL_MAGE_POLYMORPH;
}

bool Unit::hasNegativeAuraWithInterruptFlag(uint32 flag)
{
    for (AuraMap::const_iterator iter = GetAuras().begin(); iter != GetAuras().end(); ++iter)
    {
        if (!iter->second->IsPositive() && iter->second->GetSpellProto()->AuraInterruptFlags & flag && iter->second->GetSpellProto()->Id != 6358)
            return true;
    }
    return false;
}

void Unit::SetDisplayId(uint32 modelId)
{
    SetUInt32Value(UNIT_FIELD_DISPLAYID, modelId);

    if (GetTypeId() == TYPEID_UNIT && ToCreature()->isPet())
    {
        Pet *pet = ((Pet*)this);
        if (!pet->isControlled())
            return;
        Unit *owner = GetOwner();
        if (owner && (owner->GetTypeId() == TYPEID_PLAYER) && owner->ToPlayer()->GetGroup())
            owner->ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_MODEL_ID);
    }
}

void Unit::ClearComboPointHolders()
{
    while (!m_ComboPointHolders.empty())
    {
        uint32 lowguid = *m_ComboPointHolders.begin();

        Player* plr = sObjectMgr->GetPlayer(MAKE_NEW_GUID(lowguid, 0, HIGHGUID_PLAYER));
        if (plr && plr->GetComboTarget() == GetGUID())         // recheck for safe
            plr->ClearComboPoints();                        // remove also guid from m_ComboPointHolders;
        else
            m_ComboPointHolders.erase(lowguid);             // or remove manually
    }
}

void Unit::ClearAllReactives()
{
    for (uint8 i = 0; i < MAX_REACTIVE; ++i)
        m_reactiveTimer[i] = 0;

    if (HasAuraState(AURA_STATE_DEFENSE))
        ModifyAuraState(AURA_STATE_DEFENSE, false);
    if (getClass() == CLASS_HUNTER && HasAuraState(AURA_STATE_HUNTER_PARRY))
        ModifyAuraState(AURA_STATE_HUNTER_PARRY, false);
    if (HasAuraState(AURA_STATE_CRIT))
        ModifyAuraState(AURA_STATE_CRIT, false);
    if (getClass() == CLASS_HUNTER && HasAuraState(AURA_STATE_HUNTER_CRIT_STRIKE))
        ModifyAuraState(AURA_STATE_HUNTER_CRIT_STRIKE, false);

    if (getClass() == CLASS_WARRIOR && GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->ClearComboPoints();
}

void Unit::UpdateReactives(uint32 p_time)
{
    for (uint8 i = 0; i < MAX_REACTIVE; ++i)
    {
        ReactiveType reactive = ReactiveType(i);

        if (!m_reactiveTimer[reactive])
            continue;

        if (m_reactiveTimer[reactive] <= p_time)
        {
            m_reactiveTimer[reactive] = 0;

            switch (reactive)
            {
                case REACTIVE_DEFENSE:
                    if (HasAuraState(AURA_STATE_DEFENSE))
                        ModifyAuraState(AURA_STATE_DEFENSE, false);
                    break;
                case REACTIVE_HUNTER_PARRY:
                    if (getClass() == CLASS_HUNTER && HasAuraState(AURA_STATE_HUNTER_PARRY))
                        ModifyAuraState(AURA_STATE_HUNTER_PARRY, false);
                    break;
                case REACTIVE_CRIT:
                    if (HasAuraState(AURA_STATE_CRIT))
                        ModifyAuraState(AURA_STATE_CRIT, false);
                    break;
                case REACTIVE_HUNTER_CRIT:
                    if (getClass() == CLASS_HUNTER && HasAuraState(AURA_STATE_HUNTER_CRIT_STRIKE))
                        ModifyAuraState(AURA_STATE_HUNTER_CRIT_STRIKE, false);
                    break;
                case REACTIVE_OVERPOWER:
                    if (getClass() == CLASS_WARRIOR && GetTypeId() == TYPEID_PLAYER)
                        ToPlayer()->ClearComboPoints();
                    break;
                default:
                    break;
            }
        }
        else
        {
            m_reactiveTimer[reactive] -= p_time;
        }
    }
}

Unit* Unit::SelectNearbyTarget(float dist) const
{
    std::list<Unit *> targets;
    Trinity::AnyUnfriendlyNoTotemUnitInObjectRangeCheck u_check(this, this, dist);
    Trinity::UnitListSearcher<Trinity::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(this, targets, u_check);
    VisitNearbyObject(dist, searcher);

    // remove current target
    if (getVictim())
        targets.remove(getVictim());

    // remove not LoS targets
    for (std::list<Unit *>::iterator tIter = targets.begin(); tIter != targets.end();)
    {
        if (!IsWithinLOSInMap(*tIter))
        {
            std::list<Unit *>::iterator tIter2 = tIter;
            ++tIter;
            targets.erase(tIter2);
        }
        else
            ++tIter;
    }

    // no appropriate targets
    if (targets.empty())
        return NULL;

    // select random
    uint32 rIdx = urand(0, targets.size()-1);
    std::list<Unit *>::const_iterator tcIter = targets.begin();
    for (uint32 i = 0; i < rIdx; ++i)
        ++tcIter;

    return *tcIter;
}

void Unit::ApplyAttackTimePercentMod(WeaponAttackType att, float val, bool apply)
{
    float remainingTimePct = (float)m_attackTimer[att] / (GetAttackTime(att) * m_modAttackSpeedPct[att]);
    if (val > 0)
    {
        ApplyPercentModFloatVar(m_modAttackSpeedPct[att], val, !apply);
        ApplyPercentModFloatValue(UNIT_FIELD_BASEATTACKTIME+att, val, !apply);
    }
    else
    {
        ApplyPercentModFloatVar(m_modAttackSpeedPct[att], -val, apply);
        ApplyPercentModFloatValue(UNIT_FIELD_BASEATTACKTIME+att, -val, apply);
    }
    m_attackTimer[att] = uint32(GetAttackTime(att) * m_modAttackSpeedPct[att] * remainingTimePct);
}

void Unit::ApplyCastTimePercentMod(float val, bool apply)
{
    if (val > 0)
    {
        ApplyPercentModFloatValue(UNIT_MOD_CAST_SPEED, val, !apply);
        ApplyPercentModFloatVar(m_modCastingSpeedPctPositive, val, !apply);
    }
    else
    {
        ApplyPercentModFloatValue(UNIT_MOD_CAST_SPEED, -val, apply);
        ApplyPercentModFloatVar(m_modCastingSpeedPctNegative, -val, apply);
    }
}

void Unit::UpdateAuraForGroup(uint8 slot)
{
    if (GetTypeId() == TYPEID_PLAYER)
    {
        Player* player = ToPlayer();
        if (player->GetGroup())
        {
            player->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_AURAS);
            player->SetAuraUpdateMask(slot);
        }
    }
    else if (GetTypeId() == TYPEID_UNIT && ToCreature()->isPet())
    {
        Pet *pet = ((Pet*)this);
        if (pet->isControlled())
        {
            Unit *owner = GetOwner();
            if (owner && (owner->GetTypeId() == TYPEID_PLAYER) && owner->ToPlayer()->GetGroup())
            {
                owner->ToPlayer()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_AURAS);
                pet->SetAuraUpdateMask(slot);
            }
        }
    }
}

float Unit::GetAPMultiplier(WeaponAttackType attType, bool normalized)
{
    if (!normalized || GetTypeId() != TYPEID_PLAYER)
        return float(GetAttackTime(attType))/1000.0f;

    Item *Weapon = ToPlayer()->GetWeaponForAttack(attType);
    if (!Weapon)
        return 2.4;                                         // fist attack

    switch (Weapon->GetProto()->InventoryType)
    {
        case INVTYPE_2HWEAPON:
            return 3.3;
        case INVTYPE_RANGED:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_THROWN:
            return 2.8;
        case INVTYPE_WEAPON:
        case INVTYPE_WEAPONMAINHAND:
        case INVTYPE_WEAPONOFFHAND:
        default:
            return Weapon->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_DAGGER ? 1.7 : 2.4;
    }
}

Aura* Unit::GetDummyAura(uint32 spell_id) const
{
    Unit::AuraList const& mDummy = GetAurasByType(SPELL_AURA_DUMMY);
    for (Unit::AuraList::const_iterator itr = mDummy.begin(); itr != mDummy.end(); ++itr)
        if ((*itr)->GetId() == spell_id)
            return *itr;

    return NULL;
}

bool Unit::IsUnderLastManaUseEffect() const
{
    return  getMSTimeDiff(m_lastManaUse, getMSTime()) < 5000;
}

void Unit::SetContestedPvP(Player *attackedPlayer)
{
    Player* player = GetCharmerOrOwnerPlayerOrPlayerItself();

    if (!player || attackedPlayer && (attackedPlayer == player || player->duel && player->duel->opponent == attackedPlayer))
        return;

    player->SetContestedPvPTimer(30000);
    if (!player->hasUnitState(UNIT_STAT_ATTACK_PLAYER))
    {
        player->addUnitState(UNIT_STAT_ATTACK_PLAYER);
        player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_CONTESTED_PVP);
        // call MoveInLineOfSight for nearby contested guards
        UpdateObjectVisibility();
    }
    if (!hasUnitState(UNIT_STAT_ATTACK_PLAYER))
    {
        addUnitState(UNIT_STAT_ATTACK_PLAYER);
        // call MoveInLineOfSight for nearby contested guards
        UpdateObjectVisibility();
    }
}

void Unit::AddPetAura(PetAura const* petSpell)
{
    if (GetTypeId() != TYPEID_PLAYER)
        return;

    m_petAuras.insert(petSpell);
    if (Pet* pet = ToPlayer()->GetPet())
        pet->CastPetAura(petSpell);
}

void Unit::RemovePetAura(PetAura const* petSpell)
{
    if (GetTypeId() != TYPEID_PLAYER)
        return;

    m_petAuras.erase(petSpell);
    if (Pet* pet = ToPlayer()->GetPet())
        pet->RemoveAurasDueToSpell(petSpell->GetAura(pet->GetEntry()));
}

Pet* Unit::CreateTamedPetFrom(Creature* creatureTarget, uint32 spell_id)
{
    if (GetTypeId()!=TYPEID_PLAYER)
        return NULL;

    Pet* pet = new Pet(ToPlayer(), HUNTER_PET);

    if (!pet->CreateBaseAtCreature(creatureTarget))
    {
        delete pet;
        return NULL;
    }

    pet->SetCreatorGUID(GetGUID());
    pet->SetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE, getFaction());
    pet->SetUInt32Value(UNIT_CREATED_BY_SPELL, spell_id);

    if (!pet->InitStatsForLevel(creatureTarget->getLevel()))
    {
        sLog->outError("Pet::InitStatsForLevel() failed for creature (Entry: %u)!", creatureTarget->GetEntry());
        delete pet;
        return NULL;
    }

    pet->GetCharmInfo()->SetPetNumber(sObjectMgr->GeneratePetNumber(), true);
    // this enables pet details window (Shift+P)
    pet->InitPetCreateSpells(false);
    pet->SetHealth(pet->GetMaxHealth());

    return pet;
}

bool Unit::IsTriggeredAtSpellProcEvent(Unit *pVictim, Aura* aura, SpellEntry const* procSpell, uint32 procFlag, uint32 procExtra, WeaponAttackType attType, bool isVictim, bool active, SpellProcEventEntry const*& spellProcEvent )
{
    SpellEntry const *spellProto = aura->GetSpellProto();

    // Get proc Event Entry
    spellProcEvent = sSpellMgr->GetSpellProcEvent(spellProto->Id);

    // Aura info stored here
    Modifier *mod = aura->GetModifier();
    // Skip this auras
    if (isNonTriggerAura[mod->m_auraname])
        return false;
    // If not trigger by default and spellProcEvent == NULL - skip
    if (!isTriggerAura[mod->m_auraname] && spellProcEvent == NULL)
        return false;

    // Get EventProcFlag
    uint32 EventProcFlag = PROC_EX_NONE;
    if (spellProcEvent && spellProcEvent->procFlags) // if exist get custom spellProcEvent->procFlags
        EventProcFlag = spellProcEvent->procFlags;
    else
        EventProcFlag = spellProto->procFlags;       // else get from spell proto
    // Continue if no trigger exist
    if (!EventProcFlag)
        return false;

    // Check spellProcEvent data requirements
    if (!SpellMgr::IsSpellProcEventCanTriggeredBy(spellProcEvent, EventProcFlag, procSpell, procFlag, procExtra, active))
        return false;

    // In most cases req get honor or XP from kill
    if (EventProcFlag & PROC_FLAG_KILL && GetTypeId() == TYPEID_PLAYER)
    {
        bool allow = ToPlayer()->isHonorOrXPTarget(pVictim);
        // Shadow Word: Death - can trigger from every kill
        if (aura->GetId() == 32409)
            allow = true;
        if (!allow)
            return false;
    }
    // Aura added by spell can`t trogger from self (prevent drop charges/do triggers)
    // But except periodic triggers (can triggered from self)
    if (procSpell && procSpell->Id == spellProto->Id && !(spellProto->procFlags&PROC_FLAG_ON_TAKE_PERIODIC))
        return false;

    // Check if current equipment allows aura to proc
    if (!isVictim && GetTypeId() == TYPEID_PLAYER)
    {
        if (spellProto->EquippedItemClass == ITEM_CLASS_WEAPON)
        {
            Item *item = NULL;
            if (attType == BASE_ATTACK)
                item = ToPlayer()->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
            else if (attType == OFF_ATTACK)
                item = ToPlayer()->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            else
                item = ToPlayer()->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);

            if (!ToPlayer()->IsUseEquippedWeapon(attType == BASE_ATTACK))
                return false;

            if (!item || item->IsBroken() || item->GetProto()->Class != ITEM_CLASS_WEAPON || !((1<<item->GetProto()->SubClass) & spellProto->EquippedItemSubClassMask))
                return false;
        }
        else if (spellProto->EquippedItemClass == ITEM_CLASS_ARMOR)
        {
            // Check if player is wearing shield
            Item *item = ToPlayer()->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            if (!item || item->IsBroken() || item->GetProto()->Class != ITEM_CLASS_ARMOR || !((1<<item->GetProto()->SubClass) & spellProto->EquippedItemSubClassMask))
                return false;
        }
    }
    // Get chance from spell
    float chance = float(spellProto->procChance);
    // If in spellProcEvent exist custom chance, chance = spellProcEvent->customChance;
    if (spellProcEvent && spellProcEvent->customChance)
        chance = spellProcEvent->customChance;
    // If PPM exist calculate chance from PPM
    if (!isVictim && spellProcEvent && spellProcEvent->ppmRate != 0)
    {
        uint32 WeaponSpeed = GetAttackTime(attType);
        chance = GetPPMProcChance(WeaponSpeed, spellProcEvent->ppmRate);
    }
    // Apply chance modifer aura
    if (Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_CHANCE_OF_SUCCESS, chance);

    return roll_chance_f(chance);
}

bool Unit::HandleMendingAuraProc(Aura* triggeredByAura)
{
    // aura can be deleted at casts
    SpellEntry const *spellProto = triggeredByAura->GetSpellProto();
    uint32 effIdx = triggeredByAura->GetEffIndex();
    int32 heal = triggeredByAura->GetModifier()->m_amount;
    uint64 caster_guid = triggeredByAura->GetCasterGUID();

    // jumps
    int32 jumps = triggeredByAura->m_procCharges-1;

    // current aura expire
    triggeredByAura->m_procCharges = 1;             // will removed at next charges decrease

    // next target selection
    if (jumps > 0 && GetTypeId() == TYPEID_PLAYER && IS_PLAYER_GUID(caster_guid))
    {
        float radius;
        if (spellProto->EffectRadiusIndex[effIdx])
            radius = GetSpellRadius(spellProto, effIdx, false);
        else
            radius = GetSpellMaxRange(sSpellRangeStore.LookupEntry(spellProto->rangeIndex));

        if (Player* caster = ((Player*)triggeredByAura->GetCaster()))
        {
            caster->ApplySpellMod(spellProto->Id, SPELLMOD_RADIUS, radius, NULL);

            if (Player* target = ToPlayer()->GetNextRandomRaidMember(radius))
            {
                // aura will applied from caster, but spell casted from current aura holder
                SpellModifier *mod = new SpellModifier;
                mod->op = SPELLMOD_CHARGES;
                mod->value = jumps-5;               // negative
                mod->type = SPELLMOD_FLAT;
                mod->spellId = spellProto->Id;
                mod->effectId = effIdx;
                mod->lastAffected = NULL;
                mod->mask = spellProto->SpellFamilyFlags;
                mod->charges = 0;

                caster->AddSpellMod(mod, true);
                CastSpell(target, 41637, true);
                CastCustomSpell(target, spellProto->Id, &heal, NULL, NULL, true, NULL, triggeredByAura, caster->GetGUID());
                caster->AddSpellMod(mod, false);

                heal = caster->SpellHealingBonusDone(target, spellProto, heal, HEAL);
                heal = target->SpellHealingBonusTaken(caster, spellProto, heal, HEAL);
            }
        }
    }

    // heal
    CastCustomSpell(this, 33110, &heal, NULL, NULL, true, NULL, NULL, caster_guid);
    return true;
}

void Unit::RemoveAurasAtChanneledTarget(SpellEntry const* spellInfo, Unit * caster)
{
/*    uint64 target_guid = GetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT);
    if (target_guid == GetGUID())
        return;

    if (!IS_UNIT_GUID(target_guid))
        return;

    Unit* target = ObjectAccessor::GetUnit(*this, target_guid);*/
    if (!caster)
        return;

    for (AuraMap::iterator iter = GetAuras().begin(); iter != GetAuras().end();)
    {
        if (iter->second->GetId() == spellInfo->Id && iter->second->GetCasterGUID() == caster->GetGUID())
            RemoveAura(iter);
        else
            ++iter;
    }
}

void Unit::MonsterMoveByPath(float x, float y, float z, uint32 speed, bool forceDest)
{
    PathInfo path(this, x, y, z, forceDest);
    PointPath pointPath = path.getFullPath();

    uint32 traveltime = uint32(pointPath.GetTotalLength() / float(speed));
    MonsterMoveByPath(pointPath, 1, pointPath.size(), traveltime);
}

template<typename PathElem, typename PathNode>
void Unit::MonsterMoveByPath(Path<PathElem, PathNode> const& path, uint32 start, uint32 end, uint32 transitTime)
{
    SplineFlags flags = GetTypeId() == TYPEID_PLAYER ? SPLINEFLAG_WALKMODE : SplineFlags(((Creature*)this)->GetUnitMovementFlags());
    SetUnitMovementFlags(flags);
    SendMonsterMoveByPath(path, start, end, transitTime);
}

template void Unit::MonsterMoveByPath<PathNode>(const Path<PathNode> &, uint32, uint32, uint32);
template void Unit::SendMonsterMoveByPath<PathNode>(const Path<PathNode> &, uint32, uint32, uint32);
template void Unit::SendMonsterMoveByPath<TaxiPathNodePtr, const TaxiPathNodeEntry>(const Path<TaxiPathNodePtr, const TaxiPathNodeEntry> &, uint32, uint32, uint32);

bool Unit::UpdatePosition(float x, float y, float z, float orientation, bool teleport)
{
    // prevent crash when a bad coord is sent by the client
    if (!Trinity::IsValidMapCoord(x, y, z, orientation))
    {
        sLog->outDebug("Unit::UpdatePosition(%f, %f, %f) .. bad coordinates!", x, y, z);
        return false;
    }

    bool turn = (GetOrientation() != orientation);
    bool relocated = (teleport || GetPositionX() != x || GetPositionY() != y || GetPositionZ() != z);

    // TODO: Check if orientation transport offset changed instead of only global orientation
    if (turn)
        RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TURNING);

    if (relocated)
    {
        RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_MOVE);

        // move and update visible state if need
        if (GetTypeId() == TYPEID_PLAYER)
            GetMap()->PlayerRelocation(ToPlayer(), x, y, z, orientation);
        else
            GetMap()->CreatureRelocation(ToCreature(), x, y, z, orientation);
    }
    else if (turn)
        SetOrientation(orientation);

    return (relocated || turn);
}

void Unit::NearTeleportTo(float x, float y, float z, float orientation, bool casting /*= false*/ )
{
    if (GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->TeleportTo(GetMapId(), x, y, z, orientation, TELE_TO_NOT_LEAVE_TRANSPORT | TELE_TO_NOT_LEAVE_COMBAT | TELE_TO_NOT_UNSUMMON_PET | (casting ? TELE_TO_SPELL : 0));
    else
    {
        GetMap()->CreatureRelocation(ToCreature(), x, y, z, orientation);

        WorldPacket data;
        BuildHeartBeatMsg(&data);
        SendMessageToSet(&data, false);
    }
}

/*-----------------------Trinity-----------------------------*/

void Unit::Kill(Unit *pVictim, bool durabilityLoss)
{
    // Prevent killing unit twice (and giving reward from kill twice)
    if (!pVictim->GetHealth())
        return;

    if (pVictim->GetTypeId() == TYPEID_UNIT)
        pVictim->ToCreature()->HandleDelayedDeath(CREATURE_DEATH_DELAY);
    else
        pVictim->SetHealth(0);

    // find player: owner of controlled `this` or `this` itself maybe
    Player* player = GetCharmerOrOwnerPlayerOrPlayerItself();
    Creature* creature = pVictim->ToCreature();

    bool bRewardIsAllowed = true;
    if (creature)
    {
        bRewardIsAllowed = creature->IsDamageEnoughForLootingAndReward();
        if (!bRewardIsAllowed)
            creature->SetLootRecipient(NULL);
    }

    if (bRewardIsAllowed && creature && creature->GetLootRecipient())
        player = creature->GetLootRecipient();

    // Reward player, his pets, and group/raid members
    // call kill spell proc event (before real die and combat stop to triggering auras removed at death/combat stop)
    if (bRewardIsAllowed && player && player != pVictim)
        player->RewardPlayerAndGroupAtKill(pVictim);

    // Do KILL and KILLED procs. KILL proc is called only for the unit who landed the killing blow regardless of who tapped the victim
    ProcDamageAndSpell(pVictim, PROC_FLAG_KILL, PROC_FLAG_KILLED, PROC_EX_NONE, 0);

    // if talent known but not triggered (check priest class for speedup check)
    bool SpiritOfRedemption = false;
    if (pVictim->GetTypeId() == TYPEID_PLAYER && pVictim->getClass() == CLASS_PRIEST)
    {
        AuraList const& vDummyAuras = pVictim->GetAurasByType(SPELL_AURA_DUMMY);
        for (AuraList::const_iterator itr = vDummyAuras.begin(); itr != vDummyAuras.end(); ++itr)
        {
            if ((*itr)->GetSpellProto()->SpellIconID == 1654)
            {
                // save value before aura remove
                uint32 ressSpellId = pVictim->GetUInt32Value(PLAYER_SELF_RES_SPELL);
                if (!ressSpellId)
                    ressSpellId = pVictim->ToPlayer()->GetResurrectionSpellId();
                //Remove all expected to remove at death auras (most important negative case like DoT or periodic triggers)
                pVictim->RemoveAllAurasOnDeath();
                // restore for use at real death
                pVictim->SetUInt32Value(PLAYER_SELF_RES_SPELL, ressSpellId);

                // FORM_SPIRITOFREDEMPTION and related auras
                pVictim->CastSpell(pVictim, 27827, true, NULL, *itr);
                pVictim->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE); // should not be attackable
                SpiritOfRedemption = true;
                break;
            }
        }
    }

    if (!SpiritOfRedemption)
    {
        sLog->outDebug("SET JUST_DIED");
        pVictim->setDeathState(JUST_DIED);
        pVictim->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE); // reactive attackable flag
    }

    // 10% durability loss on death
    // clean InHateListOf
    if (pVictim->GetTypeId() == TYPEID_PLAYER)
    {
        // remember victim PvP death for corpse type and corpse reclaim delay
        // at original death (not at SpiritOfRedemtionTalent timeout)
        pVictim->ToPlayer()->SetPvPDeath(player != NULL);

        // only if not player and not controlled by player pet. And not at BG
        if (durabilityLoss && !player && !pVictim->ToPlayer()->InBattleGround())
        {
            sLog->outDebug("We are dead, loosing 10 percents durability");
            pVictim->ToPlayer()->DurabilityLossAll(0.10f, false);
            // durability lost message
            WorldPacket data(SMSG_DURABILITY_DAMAGE_DEATH, 0);
            pVictim->ToPlayer()->GetSession()->SendPacket(&data);
        }
        // Call KilledUnit for creatures
        if (GetTypeId() == TYPEID_UNIT && ToCreature()->IsAIEnabled)
            ToCreature()->AI()->KilledUnit(pVictim);
        else if (Guardian *pPet = GetGuardianPet())
            pPet->AI()->KilledUnit(pVictim);

        // last damage from non duel opponent or opponent controlled creature
        if (pVictim->ToPlayer()->duel)
        {
            pVictim->ToPlayer()->duel->opponent->CombatStopWithPets(true);
            pVictim->ToPlayer()->CombatStopWithPets(true);
            pVictim->ToPlayer()->DuelComplete(DUEL_INTERUPTED);
        }
    }
    else                                                // creature died
    {
        sLog->outDebug("DealDamageNotPlayer");

        if (!creature->isPet())
        {
            creature->DeleteThreatList();
            creature->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
        }

        // Call KilledUnit for creatures, this needs to be called after the lootable flag is set
        if (GetTypeId() == TYPEID_UNIT && ToCreature()->IsAIEnabled)
            ToCreature()->AI()->KilledUnit(pVictim);
        else if (Guardian *pPet = GetGuardianPet())
            pPet->AI()->KilledUnit(pVictim);

        // Call creature just died function
        if (creature->IsAIEnabled)
            creature->AI()->JustDied(this);

        if (TempSummon* summon = creature->ToTempSummon())
            if (Unit* summoner = summon->GetSummoner())
                if (summoner->ToCreature() && summoner->IsAIEnabled)
                    summoner->ToCreature()->AI()->SummonedCreatureDies(creature, this);

        // Dungeon specific stuff, only applies to players killing creatures
        if (creature->GetInstanceId())
        {
            Map *m = creature->GetMap();
            Player *creditedPlayer = GetCharmerOrOwnerPlayerOrPlayerItself();
            // TODO: do instance binding anyway if the charmer/owner is offline

            if (m->IsDungeon() && creditedPlayer)
            {
                if (m->IsRaid() || m->IsHeroic())
                {
                    if (creature->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_INSTANCE_BIND)
                        ((InstanceMap *)m)->PermBindAllPlayers(creditedPlayer);
                }
                else
                {
                    // the reset time is set but not added to the scheduler
                    // until the players leave the instance
                    time_t resettime = creature->GetRespawnTimeEx() + 2 * HOUR;
                    if (InstanceSave *save = sInstanceSaveMgr->GetInstanceSave(creature->GetInstanceId()))
                        if (save->GetResetTime() < resettime) save->SetResetTime(resettime);
                }
            }
        }
    }

    // outdoor pvp things, do these after setting the death state, else the player activity notify won't work... doh...
    // handle player kill only if not suicide (spirit of redemption for example)
    if (player && this != pVictim)
        if (OutdoorPvP * pvp = player->GetOutdoorPvP())
            pvp->HandleKill(player, pVictim);

    //if (pVictim->GetTypeId() == TYPEID_PLAYER)
    //    if (OutdoorPvP * pvp = pVictim->ToPlayer()->GetOutdoorPvP())
    //        pvp->HandlePlayerActivityChanged(pVictim->ToPlayer());

    // battleground things (do this at the end, so the death state flag will be properly set to handle in the bg->handlekill)
    if (player && player->InBattleGround())
    {
        if (BattleGround *bg = player->GetBattleGround())
        {
            if (pVictim->GetTypeId() == TYPEID_PLAYER)
                bg->HandleKillPlayer(pVictim->ToPlayer(), player);
            else
                bg->HandleKillUnit(pVictim->ToCreature(), player);
        }
    }
}

void Unit::SetControlled(bool apply, UnitState state, bool waitForFinalize)
{
    if (apply)
    {
        if (hasUnitState(state))
            return;

        addUnitState(state);
        switch (state)
        {
            case UNIT_STAT_STUNNED:
                SetStunned(true);
                CastStop();
                break;
            case UNIT_STAT_ROOT:
                if (!hasUnitState(UNIT_STAT_STUNNED))
                    SetRooted(true);
                break;
            case UNIT_STAT_CONFUSED:
                if (!hasUnitState(UNIT_STAT_STUNNED))
                {
                    SetConfused(true, waitForFinalize);
                    CastStop();
                }
                break;
            case UNIT_STAT_FLEEING:
                if (!hasUnitState(UNIT_STAT_STUNNED | UNIT_STAT_CONFUSED))
                {
                    SetFeared(true);
                    CastStop();
                }
                break;
            default:
                break;
        }
    }
    else
    {
        switch (state)
        {
            case UNIT_STAT_STUNNED: if (HasAuraType(SPELL_AURA_MOD_STUN))    return;
                                    else    SetStunned(false);    break;
            case UNIT_STAT_ROOT:    if (HasAuraType(SPELL_AURA_MOD_ROOT))    return;
                                    else    SetRooted(false);     break;
            case UNIT_STAT_CONFUSED:if (HasAuraType(SPELL_AURA_MOD_CONFUSE)) return;
                                    else    SetConfused(false);   break;
            case UNIT_STAT_FLEEING: if (HasAuraType(SPELL_AURA_MOD_FEAR))    return;
                                    else    SetFeared(false);     break;
            default: return;
        }

        clearUnitState(state);

        if (hasUnitState(UNIT_STAT_STUNNED))
            SetStunned(true);
        else
        {
            if (hasUnitState(UNIT_STAT_ROOT))
                SetRooted(true);

            if (hasUnitState(UNIT_STAT_CONFUSED))
                SetConfused(true, waitForFinalize);
            else if (hasUnitState(UNIT_STAT_FLEEING))
                SetFeared(true);
        }
    }
}

void Unit::SetStunned(bool apply)
{
    if (apply)
    {
        SetUInt64Value(UNIT_FIELD_TARGET, 0);
        SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_STUNNED);
        CastStop();

        // Creature specific
        if (GetTypeId() != TYPEID_PLAYER)
            ToCreature()->StopMoving();
        else
            SetStandState(UNIT_STAND_STATE_STAND);

        WorldPacket data(SMSG_FORCE_MOVE_ROOT, 8);
        data << GetPackGUID();
        data << uint32(0);
        SendMessageToSet(&data, true);

        CastStop();
    }
    else
    {
        if (isAlive() && getVictim())
            SetUInt64Value(UNIT_FIELD_TARGET, getVictim()->GetGUID());

        // don't remove UNIT_FLAG_STUNNED for pet when owner is mounted (disabled pet's interface)
        Unit *pOwner = GetOwner();
        if (!pOwner || (pOwner->GetTypeId() == TYPEID_PLAYER && !pOwner->ToPlayer()->IsMounted()))
            RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_STUNNED);

        if (!hasUnitState(UNIT_STAT_ROOT))         // prevent allow move if have also root effect
        {
            WorldPacket data(SMSG_FORCE_MOVE_UNROOT, 8+4);
            data << GetPackGUID();
            data << uint32(0);
            SendMessageToSet(&data, true);
        }
    }
}

void Unit::SetRooted(bool apply)
{
    uint32 apply_stat = UNIT_STAT_ROOT;
    if (apply)
    {
        if (m_rootTimes > 0) // blizzard internal check?
            m_rootTimes++;

        SetFlag(UNIT_FIELD_FLAGS, (apply_stat<<16)); // probably wrong

        if (GetTypeId() == TYPEID_PLAYER)
        {
            WorldPacket data(SMSG_FORCE_MOVE_ROOT, 10);
            data << GetPackGUID();
            data << m_rootTimes;
            SendMessageToSet(&data, true);
        }
        else
            ToCreature()->StopMoving();
    }
    else
    {
        RemoveFlag(UNIT_FIELD_FLAGS, (apply_stat<<16)); // probably wrong

        if (!hasUnitState(UNIT_STAT_STUNNED))      // prevent allow move if have also stun effect
        {
            if (GetTypeId() == TYPEID_PLAYER)
            {
                WorldPacket data(SMSG_FORCE_MOVE_UNROOT, 10);
                data << GetPackGUID();
                data << ++m_rootTimes;
                SendMessageToSet(&data, true);
            }
        }
    }
}

void Unit::SetFeared(bool apply)
{
    if (apply)
    {
        SetUInt64Value(UNIT_FIELD_TARGET, 0);

        Unit *caster = NULL;
        Unit::AuraList const& fearAuras = GetAurasByType(SPELL_AURA_MOD_FEAR);
        if (!fearAuras.empty())
            caster = ObjectAccessor::GetUnit(*this, fearAuras.front()->GetCasterGUID());
        if (!caster)
            caster = getAttackerForHelper();
        GetMotionMaster()->MoveFleeing(caster, fearAuras.empty() ? sWorld->getConfig(CONFIG_CREATURE_FAMILY_FLEE_DELAY) : 0);             // caster == NULL processed in MoveFleeing
    }
    else
    {
        if (isAlive())
        {
            if (GetMotionMaster()->GetCurrentMovementGeneratorType() == FLEEING_MOTION_TYPE)
                GetMotionMaster()->MovementExpired();
            if (getVictim())
                SetUInt64Value(UNIT_FIELD_TARGET, getVictim()->GetGUID());
        }
    }

    if (GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->SetClientControl(this, !apply);
}

void Unit::SetConfused(bool apply, bool waitForFinalize)
{
    if (apply)
        GetMotionMaster()->MoveConfused(waitForFinalize);
    else
    {
        if (isAlive() && GetMotionMaster()->GetCurrentMovementGeneratorType() == CONFUSED_MOTION_TYPE)
            GetMotionMaster()->MovementExpired();
    }

    if (GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->SetClientControl(this, !apply);
}

void Unit::SetCharmedBy(Unit* charmer, CharmType type)
{
    if (!charmer)
        return;

    ASSERT(type != CHARM_TYPE_POSSESS || charmer->GetTypeId() == TYPEID_PLAYER);

    sLog->outDebug("SetCharmedBy: charmer %u, charmed %u, type %u.", charmer->GetEntry(), GetEntry(), (uint32)type);

    if (this == charmer)
    {
        sLog->outCrash("Unit::SetCharmedBy: Unit %u is trying to charm itself!", GetEntry());
        return;
    }

    if (isInFlight())
        return;

    if (GetTypeId() == TYPEID_PLAYER && ToPlayer()->GetTransport())
    {
        sLog->outCrash("Unit::SetCharmedBy: Player on transport is trying to charm %u", GetEntry());
        return;
    }
    CastStop();
    CombatStop(); //TODO: CombatStop(true) may cause crash (interrupt spells)
    DeleteThreatList();

    // Charmer stop charming
    if (charmer->GetTypeId() == TYPEID_PLAYER)
    {
        charmer->ToPlayer()->StopCastingCharm();
        charmer->ToPlayer()->StopCastingBindSight();
    }

    // Charmed stop charming
    if (GetTypeId() == TYPEID_PLAYER)
    {
        ToPlayer()->StopCastingCharm();
        ToPlayer()->StopCastingBindSight();
    }

    // StopCastingCharm may remove a possessed pet?
    if (!IsInWorld())
    {
        sLog->outCrash("Unit::SetCharmedBy: %u is not in world but %u is trying to charm it!", GetEntry(), charmer->GetEntry());
        return;
    }

    // Set charmed
    setFaction(charmer->getFaction());
    charmer->SetCharm(this, true);

    if (GetTypeId() == TYPEID_UNIT)
    {
        ToCreature()->AI()->OnCharmed(true);
        GetMotionMaster()->Clear(false);
        GetMotionMaster()->MoveIdle();
    }
    else
    {
        if (ToPlayer()->isAFK())
            ToPlayer()->ToggleAFK();
        ToPlayer()->SetClientControl(this, 0);
    }

    // Pets already have a properly initialized CharmInfo, don't overwrite it.
    if (!GetCharmInfo())
    {
        CharmInfo *charmInfo = InitCharmInfo();
        if (type == CHARM_TYPE_POSSESS)
            charmInfo->InitPossessCreateSpells();
        else
            charmInfo->InitCharmCreateSpells();
    }

    charmer->ForceHealthAndPowerUpdate();       
    if (charmer->GetTypeId() == TYPEID_PLAYER)
    {
        ForceHealthAndPowerUpdate(charmer->ToPlayer());

        switch (type)
        {
            case CHARM_TYPE_POSSESS:
                addUnitState(UNIT_STAT_POSSESSED);
                SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);
                charmer->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISABLE_MOVE);
                charmer->ToPlayer()->SetClientControl(this, 1);
                charmer->ToPlayer()->SetViewpoint(this, true);
                charmer->ToPlayer()->PossessSpellInitialize();
                break;
            case CHARM_TYPE_CHARM:
                if (GetTypeId() == TYPEID_UNIT && charmer->getClass() == CLASS_WARLOCK)
                {
                    CreatureTemplate const* cinfo = ToCreature()->GetCreatureTemplate();
                    if (cinfo && cinfo->type == CREATURE_TYPE_DEMON)
                    {
                        //to prevent client crash
                        SetFlag(UNIT_FIELD_BYTES_0, 2048);

                        //just to enable stat window
                        if (GetCharmInfo())
                            GetCharmInfo()->SetPetNumber(sObjectMgr->GeneratePetNumber(), true);

                        //if charmed two demons the same session, the 2nd gets the 1st one's name
                        SetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP, time(NULL));
                    }
                }
                charmer->ToPlayer()->CharmSpellInitialize();
                break;
            default:
                break;
        }
    }
}

void Unit::RemoveCharmedBy(Unit *charmer)
{
    if (!isCharmed())
        return;

    if (!charmer)
        charmer = GetCharmer();
    if (charmer != GetCharmer()) // one aura overrides another?
        return;

    CharmType type;
    if (hasUnitState(UNIT_STAT_POSSESSED))
        type = CHARM_TYPE_POSSESS;
    else
        type = CHARM_TYPE_CHARM;

    CastStop();
    CombatStop(); //TODO: CombatStop(true) may cause crash (interrupt spells)
    getHostileRefManager().deleteReferences();
    DeleteThreatList();
    RestoreFaction();
    GetMotionMaster()->InitDefault();

    if (type == CHARM_TYPE_POSSESS)
    {
        clearUnitState(UNIT_STAT_POSSESSED);
        RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);
    }

    if (GetTypeId() == TYPEID_UNIT)
    {
        if (!ToCreature()->isPet())
            RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);

        ToCreature()->AI()->OnCharmed(false);
        ToCreature()->AIM_Initialize();

        if (ToCreature()->AI() && charmer && charmer->isAlive())
            ToCreature()->AI()->AttackStart(charmer);
    }
    else
        ToPlayer()->SetClientControl(this, 1);

    // If charmer still exists
    if (!charmer)
        return;

    ASSERT(type != CHARM_TYPE_POSSESS || charmer->GetTypeId() == TYPEID_PLAYER);

    charmer->SetCharm(this, false);

    if (charmer->GetTypeId() == TYPEID_PLAYER)
    {
        switch (type)
        {
            case CHARM_TYPE_POSSESS:
                charmer->ToPlayer()->SetClientControl(charmer, 1);
                charmer->ToPlayer()->SetViewpoint(this, false);
                charmer->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISABLE_MOVE);
                break;
            case CHARM_TYPE_CHARM:
                if (GetTypeId() == TYPEID_UNIT && charmer->getClass() == CLASS_WARLOCK)
                {
                    CreatureTemplate const* cinfo = ToCreature()->GetCreatureTemplate();
                    if (cinfo && cinfo->type == CREATURE_TYPE_DEMON)
                    {
                        CreatureDataAddon const *cainfo = ToCreature()->GetCreatureAddon();
                        if (cainfo && cainfo->bytes0 != 0)
                            SetUInt32Value(UNIT_FIELD_BYTES_0, cainfo->bytes0);
                        else
                            RemoveFlag(UNIT_FIELD_BYTES_0, 2048);

                        if (GetCharmInfo())
                            GetCharmInfo()->SetPetNumber(0, true);
                        else
                            sLog->outError("Aura::HandleModCharm: target="UI64FMTD" with typeid=%d has a charm aura but no charm info!", GetGUID(), GetTypeId());
                    }
                }
                break;
            default:
                break;
        }
    }

    charmer->ForceHealthAndPowerUpdate();
    if (charmer->GetTypeId() == TYPEID_PLAYER)
        ForceHealthAndPowerUpdate(charmer->ToPlayer());

    //a guardian should always have charminfo
    if (charmer->GetTypeId() == TYPEID_PLAYER && this != charmer->GetFirstControlled())
        charmer->ToPlayer()->SendRemoveControlBar();
    else if (GetTypeId() == TYPEID_PLAYER || GetTypeId() == TYPEID_UNIT && !ToCreature()->isGuardian())
        DeleteCharmInfo();
}

void Unit::RestoreFaction()
{
    if (GetTypeId() == TYPEID_PLAYER)
        ToPlayer()->setFactionForRace(getRace());
    else
    {
        CreatureTemplate const* cinfo = ToCreature()->GetCreatureTemplate();

        if (ToCreature()->isPet())
        {
            if (Unit* owner = GetOwner())
                setFaction(owner->getFaction());
            else if (cinfo)
                setFaction(cinfo->faction_A);
        }
        else if (cinfo)  // normal creature
            setFaction(cinfo->faction_A);
    }
}

bool Unit::IsInPartyWith(Unit const *unit) const
{
    if (this == unit)
        return true;

    const Unit *u1 = GetCharmerOrOwnerOrSelf();
    const Unit *u2 = unit->GetCharmerOrOwnerOrSelf();
    if (u1 == u2)
        return true;

    if (u1->GetTypeId() == TYPEID_PLAYER && u2->GetTypeId() == TYPEID_PLAYER)
        return u1->ToPlayer()->IsInSameGroupWith(u2->ToPlayer());
    else
        return false;
}

bool Unit::IsInRaidWith(Unit const *unit) const
{
    if (this == unit)
        return true;

    const Unit *u1 = GetCharmerOrOwnerOrSelf();
    const Unit *u2 = unit->GetCharmerOrOwnerOrSelf();
    if (u1 == u2)
        return true;

    if (u1->GetTypeId() == TYPEID_PLAYER && u2->GetTypeId() == TYPEID_PLAYER)
        return u1->ToPlayer()->IsInSameRaidWith(u2->ToPlayer());
    else
        return false;
}

void Unit::GetRaidMember(std::list<Unit*> &nearMembers, float radius)
{
    Player *owner = GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!owner)
        return;

    Group *pGroup = owner->GetGroup();
    if (!pGroup)
        return;

    for (GroupReference *itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* Target = itr->getSource();

        // IsHostileTo check duel and controlled by enemy
        if (Target && Target != this && Target->isAlive()
            && IsWithinDistInMap(Target, radius) && !IsHostileTo(Target))
            nearMembers.push_back(Target);
    }
}

void Unit::GetPartyMember(std::list<Unit*> &TagUnitMap, float radius)
{
    Unit *owner = GetCharmerOrOwnerOrSelf();
    Group *pGroup = NULL;
    if (owner->GetTypeId() == TYPEID_PLAYER)
        pGroup = owner->ToPlayer()->GetGroup();

    if (pGroup)
    {
        uint8 subgroup = owner->ToPlayer()->GetSubGroup();

        for (GroupReference *itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player* Target = itr->getSource();

            // IsHostileTo check duel and controlled by enemy
            if (Target && Target->GetSubGroup() == subgroup && !IsHostileTo(Target))
            {
                if (Target->GetTypeId() == TYPEID_PLAYER && Target->ToPlayer()->duel && Target->ToPlayer()->duel->startTime != 0)
                    continue;

                if (Target->isAlive() && IsWithinDistInMap(Target, radius))
                    TagUnitMap.push_back(Target);

                if (Guardian* pet = Target->GetGuardianPet())
                    if (pet->isAlive() &&  IsWithinDistInMap(pet, radius))
                        TagUnitMap.push_back(pet);
            }
        }
    }
    else
    {
        if (owner->isAlive() && (owner == this || IsWithinDistInMap(owner, radius)))
            TagUnitMap.push_back(owner);
        if (Guardian* pet = owner->GetGuardianPet())
            if (pet->isAlive() && (pet == this && IsWithinDistInMap(pet, radius)))
                TagUnitMap.push_back(pet);
    }
}

void Unit::AddAura(uint32 spellId, Unit* target)
{
    if (!target || !target->isAlive())
        return;

    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId);
    if (!spellInfo)
        return;

    if (target->IsImmunedToSpell(spellInfo))
        return;

    for (uint32 i = 0; i < 3; ++i)
    {
        if (spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA)
        {
            Aura *Aur = CreateAura(spellInfo, i, NULL, target, this);
            target->AddAura(Aur);
        }
    }
}

void Unit::SetFlying(bool apply)
{
    if (apply)
    {
        SetByteFlag(UNIT_FIELD_BYTES_1, 3, 0x02);
        AddUnitMovementFlag(MOVEFLAG_FLYING | MOVEFLAG_FLYING2);
    }
    else
    {
        RemoveByteFlag(UNIT_FIELD_BYTES_1, 3, 0x02);
        RemoveUnitMovementFlag(MOVEFLAG_FLYING | MOVEFLAG_FLYING2);
    }
}

void Unit::UpdateObjectVisibility(bool forced)
{
    if (!forced)
        AddToNotify(NOTIFY_VISIBILITY_CHANGED);
    else
    {
        WorldObject::UpdateObjectVisibility(true);
        // call MoveInLineOfSight for nearby creatures
        Trinity::AIRelocationNotifier notifier(*this);
        VisitNearbyObject(GetMap()->GetVisibilityRange(), notifier);
    }
}

void Unit::ApplyPreCastSpell(Unit* caster, const SpellEntry* spellInfo)
{
    // Negative buff should only be applied on players and pets
    if (GetTypeId() == TYPEID_PLAYER || (GetTypeId() == TYPEID_UNIT && ToCreature()->isPet()))
    {
        if (!spellInfo)
            return;

        uint32 spellId = 0;
        if (spellInfo->CasterAuraStateNot == AURA_STATE_WEAKENED_SOUL || spellInfo->TargetAuraStateNot == AURA_STATE_WEAKENED_SOUL)
            spellId = 6788; // Weakened Soul
        else if (spellInfo->CasterAuraStateNot == AURA_STATE_FORBEARANCE || spellInfo->TargetAuraStateNot == AURA_STATE_FORBEARANCE)
            spellId = 25771; // Forbearance
        else if (spellInfo->CasterAuraStateNot == AURA_STATE_HYPOTHERMIA)
            spellId = 41425; // Hypothermia
        else if (spellInfo->Mechanic == MECHANIC_BANDAGE)
            spellId = 11196; // Recently Bandaged
        else if ((spellInfo->AttributesEx & 0x20) && (spellInfo->AttributesEx2 & 0x20000))
            spellId = 23230; // Blood Fury - Healing Reduction

        SpellEntry const *AdditionalSpellInfo = sSpellStore.LookupEntry(spellId);
        if (AdditionalSpellInfo)
        {
            // applied at target by target
            Aura* AdditionalAura = CreateAura(AdditionalSpellInfo, 0, NULL, this, caster, 0);
            AddAura(AdditionalAura);
        }
    }
}

bool Unit::ShouldRevealHealthTo(Player* player) const
{
    if (player == this || player->isGameMaster())
        return true;

    if (player->IsInRaidWith(this))
        return true;

    return false;
}

void Unit::ForceHealthAndPowerUpdate(Player* charmer)
{
    uint32 powerType = getPowerType();
    ForceValuesUpdateAtIndex(UNIT_FIELD_HEALTH);
    ForceValuesUpdateAtIndex(UNIT_FIELD_MAXHEALTH);
    ForceValuesUpdateAtIndex(UNIT_FIELD_POWER1 + powerType);
    ForceValuesUpdateAtIndex(UNIT_FIELD_MAXPOWER1 + powerType);

    if (charmer)
    {
        if (Group* group = charmer->GetGroup())
        {
            charmer->SetGroupUpdateFlag(GROUP_UPDATE_PET);
            group->UpdatePlayerOutOfRange(charmer);
        }
    }
}

Unit* Unit::GetMagicHitRedirectTarget(Unit* victim, SpellEntry const* spellInfo)
{
    // Patch 1.2 notes: Spell Reflection no longer reflects abilities
    if (spellInfo->AttributesEx & SPELL_ATTR_EX_CANT_BE_REDIRECTED || spellInfo->Attributes & (SPELL_ATTR_ABILITY | SPELL_ATTR_UNAFFECTED_BY_INVULNERABILITY))
        return victim;

    Unit::AuraList const& magnetAuras = victim->GetAurasByType(SPELL_AURA_SPELL_MAGNET);
    for (Unit::AuraList::const_iterator itr = magnetAuras.begin(); itr != magnetAuras.end(); ++itr)
    {
        if (Unit* magnet = (*itr)->GetCaster())
        {
            if ((*itr)->m_procCharges > 0)
            {
                // only drop charge if spell is not delayed
                if (!spellInfo->speed)
                    (*itr)->SetAuraProcCharges((*itr)->m_procCharges - 1);
                    
                return magnet;
            }
        }
    }
    
    return victim;
}

Unit* Unit::GetMeleeHitRedirectTarget(Unit* victim, SpellEntry const* spellInfo)
{
    if (spellInfo && spellInfo->AttributesEx & (SPELL_ATTR_EX_CANT_BE_REDIRECTED | SPELL_ATTR_EX_CANT_BE_REFLECTED))
        return victim;

    float maxRange = NOMINAL_MELEE_RANGE;
    if (spellInfo && spellInfo->rangeIndex)
        maxRange = GetSpellMaxRange(sSpellRangeStore.LookupEntry(spellInfo->rangeIndex));

    Unit::AuraList const& hitTriggerAuras = victim->GetAurasByType(SPELL_AURA_ADD_CASTER_HIT_TRIGGER);
    for (Unit::AuraList::const_iterator i = hitTriggerAuras.begin(); i != hitTriggerAuras.end(); ++i)
    {
        if (Unit* magnet = (*i)->GetCaster())
        {
            if (magnet->IsWithinLOSInMap(this) && magnet->IsWithinDistInMap(this, maxRange) && (*i)->m_procCharges > 0)
            {
                victim->RemoveAurasDueToSpell((*i)->GetId());
                return magnet;
            }
        }
    }

    return victim;
}

void CharmInfo::SetIsCommandAttack(bool val)
{
    m_isCommandAttack = val;
}

bool CharmInfo::IsCommandAttack()
{
    return m_isCommandAttack;
}

void CharmInfo::SaveStayPosition()
{
    m_unit->GetPosition(m_stayX, m_stayY, m_stayZ);
}

void CharmInfo::GetStayPosition(float &x, float &y, float &z)
{
    x = m_stayX;
    y = m_stayY;
    z = m_stayZ;
}

void CharmInfo::SetIsAtStay(bool val)
{
    m_isAtStay = val;
}

bool CharmInfo::IsAtStay()
{
    return m_isAtStay;
}

void CharmInfo::SetIsFollowing(bool val)
{
    m_isFollowing = val;
}

bool CharmInfo::IsFollowing()
{
    return m_isFollowing;
}

void CharmInfo::SetIsReturning(bool val)
{
    m_isReturning = val;
}

bool CharmInfo::IsReturning()
{
    return m_isReturning;
}

// This function get high ranked spelld from array
// found in spells learned/talented by player
// info: spells must be sorted from high ranks to low
// ex: array[] = {rank3, rank2, rank1 };
uint32 Unit::GetMaxRankSpellFromArray(uint32 array[], uint8 count)
{
    // if array is empty return no rank
    if (!count) return 0;

    for (uint8 i = 0; i < count; i++)
        if (ToPlayer()->HasSpell(array[i]))
        {
            uint32 spellId = array[i];
            return spellId;
        }

    // no spell found, return no rank
    return 0;
}

void Unit::SetPhaseMask(uint32 newPhaseMask, bool update)
{
    if (newPhaseMask == GetPhaseMask())
        return;

    if (IsInWorld())
    {
        // modify hostile references for new phasemask, some special cases deal with hostile references themselves
        if (GetTypeId() == TYPEID_UNIT || (!ToPlayer()->isGameMaster() && !ToPlayer()->GetSession()->PlayerLogout()))
        {
            HostileRefManager& refManager = getHostileRefManager();
            HostileReference* ref = refManager.getFirst();

            while (ref)
            {
                if (Unit* unit = ref->getSource()->getOwner())
                    if (Creature *creature = unit->ToCreature())
                        refManager.setOnlineOfflineState(creature, creature->InSamePhase(newPhaseMask));

                ref = ref->next();
            }

            // modify threat lists for new phasemask
            if (GetTypeId() != TYPEID_PLAYER)
            {
                std::list<HostileReference*> threatList = getThreatManager().getThreatList();
                std::list<HostileReference*> offlineThreatList = getThreatManager().getOfflieThreatList();

                // merge expects sorted lists
                threatList.sort();
                offlineThreatList.sort();
                threatList.merge(offlineThreatList);

                for (std::list<HostileReference*>::const_iterator itr = threatList.begin(); itr != threatList.end(); ++itr)
                    if (Unit* unit = (*itr)->getTarget())
                        unit->getHostileRefManager().setOnlineOfflineState(ToCreature(), unit->InSamePhase(newPhaseMask));
            }
        }
    }

    // Phase player, dont update
    WorldObject::SetPhaseMask(newPhaseMask, false);

    // Phase pets and summons
    if (IsInWorld())
    {
        if (GetTypeId() == TYPEID_PLAYER)
        {
            if (Guardian* pet = ToPlayer()->GetGuardianPet())
                pet->SetPhaseMask(newPhaseMask, true);
        }

        RemoveNotOwnSingleTargetAuras(); // we can lost access to caster or target
    }

    // Update visibility after phasing pets and summons so they wont despawn
    if (update)
        UpdateObjectVisibility();
}

void Unit::InterruptNearbyCasters(float distance)
{
    std::list<Unit*> targets;
    Trinity::AnyUnfriendlyUnitInObjectRangeCheck u_check(this, this, distance);
    Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(this, targets, u_check);
    VisitNearbyObject(distance, searcher);
    for (std::list<Unit*>::iterator iter = targets.begin(); iter != targets.end(); ++iter)
    {
        if (!(*iter)->hasUnitState(UNIT_STAT_CASTING))
            continue;

        for (uint32 i = CURRENT_FIRST_NON_MELEE_SPELL; i < CURRENT_MAX_SPELL; i++)
        {
            if ((*iter)->GetCurrentSpell(i) && (*iter)->GetCurrentSpell(i)->m_targets.getUnitTargetGUID() == GetGUID())
                (*iter)->InterruptSpell(CurrentSpellTypes(i), false);
        }
    }
}
