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
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "Log.h"
#include "Corpse.h"
#include "Player.h"
#include "MapManager.h"
#include "Transport.h"
#include "Battleground.h"
#include "WaypointMovementGenerator.h"
#include "InstanceSaveMgr.h"
#include "AnticheatMgr.h"

void WorldSession::HandleMoveWorldportAckOpcode(WorldPacket & /*recv_data*/)
{
    sLog->outDebug("WORLD: got MSG_MOVE_WORLDPORT_ACK.");
    HandleMoveWorldportAckOpcode();
}

void WorldSession::HandleMoveWorldportAckOpcode()
{
    if (!GetPlayer())
        return;

    // ignore unexpected far teleports
    if (!GetPlayer()->IsBeingTeleportedFar())
        return;

    // get the teleport destination
    WorldLocation oldLoc;
    GetPlayer()->GetPosition(&oldLoc);
    WorldLocation &newLoc = GetPlayer()->GetTeleportDest();

    // possible errors in the coordinate validity check
    if (!sMapMgr->IsValidMapCoord(newLoc))
    {
        GetPlayer()->SetSemaphoreTeleportFar(false);
        LogoutPlayer(false);
        return;
    }

    // get the destination map entry, not the current one, this will fix homebind and reset greeting
    MapEntry const* mEntry = sMapStore.LookupEntry(newLoc.GetMapId());

    Map * oldMap = GetPlayer()->GetMap();
    Map * newMap = NULL;

    // prevent crash at attempt landing to not existed battleground instance
    if (mEntry->IsBattleGroundOrArena())
    {
        if (GetPlayer()->GetBattleGroundId())
            newMap = sMapMgr->FindMap(newLoc.GetMapId(), GetPlayer()->GetBattleGroundId());
    }

    InstanceTemplate const* mInstance = sObjectMgr->GetInstanceTemplate(newLoc.GetMapId());

    // reset instance validity, except if going to an instance inside an instance
    if (GetPlayer()->m_InstanceValid == false && !mInstance)
        GetPlayer()->m_InstanceValid = true;

    GetPlayer()->SetSemaphoreTeleportFar(false);

    if (GetPlayer()->IsInWorld())
    {
        sLog->outCrash("Player is still in world when teleported from map %u! to new map %u", oldMap->GetId(), newLoc.GetMapId());
        oldMap->Remove(GetPlayer(), false);
    }

    if (!newMap)
        newMap = sMapMgr->CreateMap(newLoc.GetMapId(), GetPlayer(), 0);

    // relocate the player to the teleport destination
    // the CanEnter checks are done in TeleporTo but conditions may change
    // while the player is in transit, for example the map may get full
    if (!newMap || !newMap->CanEnter(GetPlayer()))
    {
        sLog->outError("Map %d could not be created for player %d, porting player to homebind", newLoc.GetMapId(), GetPlayer()->GetGUIDLow());
        if (!GetPlayer()->TeleportTo(oldLoc))
            GetPlayer()->TeleportToHomebind();

        return;
    }

    GetPlayer()->Relocate(&newLoc);
    GetPlayer()->ResetMap();
    GetPlayer()->SetMap(newMap);

    // check this before Map::Add(player), because that will create the instance save!
    bool reset_notify = (GetPlayer()->GetBoundInstance(GetPlayer()->GetMapId(), GetPlayer()->GetDifficulty()) == NULL);

    GetPlayer()->SendInitialPacketsBeforeAddToMap();
    if (!GetPlayer()->GetMap()->Add(GetPlayer()))
    {
        sLog->outError("WORLD: failed to teleport player %s (%d) to map %d because of unknown reason!", GetPlayer()->GetName(), GetPlayer()->GetGUIDLow(), newLoc.GetMapId());
        if (!GetPlayer()->TeleportTo(oldLoc))
            GetPlayer()->TeleportToHomebind();

        return;
    }

    // battleground state prepare (in case join to BG), at relogin/tele player not invited
    // only add to bg group and object, if the player was invited (else he entered through command)
    if (GetPlayer()->InBattleGround())
    {
        // cleanup setting if outdated
        if (!mEntry->IsBattleGroundOrArena())
        {
            // We're not in BG
            GetPlayer()->SetBattleGroundId(0);                          // We're not in BG.
            // reset destination bg team
            GetPlayer()->SetBGTeam(0);
        }
        // join to bg case
        else if (BattleGround *bg = GetPlayer()->GetBattleGround())
        {
            if (GetPlayer()->IsInvitedForBattleGroundInstance(GetPlayer()->GetBattleGroundId()))
                bg->AddPlayer(GetPlayer());
        }
    }

    GetPlayer()->SendInitialPacketsAfterAddToMap();

    // flight fast teleport case
    if (GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE)
    {
        if (!GetPlayer()->InBattleGround())
        {
            // short preparations to continue flight
            FlightPathMovementGenerator* flight = (FlightPathMovementGenerator*)(GetPlayer()->GetMotionMaster()->top());
            flight->Initialize(*GetPlayer());
            return;
        }

        // battleground state prepare, stop flight
        GetPlayer()->GetMotionMaster()->MovementExpired();
        GetPlayer()->CleanupAfterTaxiFlight();
    }

    // resurrect character at enter into instance where his corpse exist after add to map
    Corpse *corpse = GetPlayer()->GetCorpse();
    if (corpse && corpse->GetType() != CORPSE_BONES && corpse->GetMapId() == GetPlayer()->GetMapId())
    {
        if (mEntry->IsDungeon())
        {
            GetPlayer()->ResurrectPlayer(0.5f, false);
            GetPlayer()->SpawnCorpseBones();
        }
    }

    if (mEntry->IsRaid() && mInstance)
    {
        if (reset_notify)
        {
            uint32 timeleft = sInstanceSaveMgr->GetResetTimeFor(GetPlayer()->GetMapId()) - time(NULL);
            GetPlayer()->SendInstanceResetWarning(GetPlayer()->GetMapId(), timeleft); // greeting at the entrance of the resort raid instance
        }
    }

    // mount allow check
    if (!mEntry->IsMountAllowed())
        GetPlayer()->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

    // honorless target
    if (GetPlayer()->pvpInfo.inHostileArea)
        GetPlayer()->CastSpell(GetPlayer(), 2479, true);
    // in friendly area
    else if (GetPlayer()->IsPvP() && !GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP))
        GetPlayer()->UpdatePvP(false, false);

    // resummon pet
    if (GetPlayer()->m_temporaryUnsummonedPetNumber)
    {
        Pet* NewPet = new Pet(GetPlayer());
        if (!NewPet->LoadPetFromDB(GetPlayer(), 0, GetPlayer()->m_temporaryUnsummonedPetNumber, true))
            delete NewPet;

        GetPlayer()->m_temporaryUnsummonedPetNumber = 0;
    }

    //lets process all delayed operations on successful teleport
    GetPlayer()->ProcessDelayedOperations();
}

void WorldSession::HandleMoveTeleportAck(WorldPacket& recv_data)
{
    uint64 guid;
    uint32 flags, time;

    recv_data >> guid;
    recv_data >> flags >> time;
    sLog->outDebug("Guid " UI64FMTD, guid);
    sLog->outDebug("Flags %u, time %u", flags, time/IN_MILLISECONDS);

    Unit *mover = _player->m_mover;
    Player *plMover = mover->GetTypeId() == TYPEID_PLAYER ? mover->ToPlayer() : NULL;

    if (!plMover || !plMover->IsBeingTeleportedNear())
        return;

    if (guid != plMover->GetGUID())
        return;

    plMover->SetSemaphoreTeleportNear(false);

    uint32 old_zone = plMover->GetZoneId();

    WorldLocation const& dest = plMover->GetTeleportDest();

    plMover->UpdatePosition(dest, true);

    uint32 newzone = plMover->GetZoneId();

    plMover->UpdateZone(newzone);

    // new zone
    if (old_zone != newzone)
    {
        // honorless target
        if (plMover->pvpInfo.inHostileArea)
            plMover->CastSpell(plMover, 2479, true);
         // in friendly area
        else if (plMover->IsPvP() && !plMover->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP))
            plMover->UpdatePvP(false, false);
    }

    // resummon pet
    if (plMover->m_temporaryUnsummonedPetNumber)
    {
        Pet* NewPet = new Pet(plMover);
        if (!NewPet->LoadPetFromDB(plMover, 0, plMover->m_temporaryUnsummonedPetNumber, true))
            delete NewPet;

        plMover->m_temporaryUnsummonedPetNumber = 0;
    }

    //lets process all delayed operations on successful teleport
    plMover->ProcessDelayedOperations();
}

void WorldSession::HandleMovementOpcodes(WorldPacket & recv_data)
{
    uint16 opcode = recv_data.GetOpcode();
    Unit *mover = _player->m_mover;

    ASSERT(mover != NULL);                                  // there must always be a mover

    Player *plrMover = mover->ToPlayer();

    // ignore, waiting processing in WorldSession::HandleMoveWorldportAckOpcode and WorldSession::HandleMoveTeleportAck
    if (plrMover && plrMover->IsBeingTeleported())
    {
        recv_data.rpos(recv_data.wpos());                   // prevent warnings spam
        return;
    }

    /* extract packet */
    MovementInfo movementInfo;
    recv_data >> movementInfo;
    /*----------------*/

    if (!mover || !mover->IsInWorld())
        return;

    if (!Trinity::IsValidMapCoord(movementInfo.GetPos()->GetPositionX(), movementInfo.GetPos()->GetPositionY(), movementInfo.GetPos()->GetPositionZ(), movementInfo.GetPos()->GetOrientation()))
    {
        recv_data.rpos(recv_data.wpos());                   // prevent warnings spam
        return;
    }

    //Save movement flags
    mover->SetUnitMovementFlags(movementInfo.GetMovementFlags());

    /* handle special cases */
    if (movementInfo.HasMovementFlag(MOVEFLAG_ONTRANSPORT))
    {
        // transports size limited
        // (also received at zeppelin/lift leave by some reason with t_* as absolute in continent coordinates, can be safely skipped)
        if (movementInfo.GetTransportPos()->GetPositionX() > 50 || movementInfo.GetTransportPos()->GetPositionY() > 50 || movementInfo.GetTransportPos()->GetPositionZ() > 50)
        {
            recv_data.rpos(recv_data.wpos());                   // prevent warnings spam
            return;
        }

        if (!Trinity::IsValidMapCoord(movementInfo.GetPos()->GetPositionX() + movementInfo.GetTransportPos()->GetPositionX(), movementInfo.GetPos()->GetPositionY() + movementInfo.GetTransportPos()->GetPositionY(),
            movementInfo.GetPos()->GetPositionZ() + movementInfo.GetTransportPos()->GetPositionZ(), movementInfo.GetPos()->GetOrientation() + movementInfo.GetTransportPos()->GetOrientation()))
        {
            recv_data.rpos(recv_data.wpos());                   // prevent warnings spam
            return;
        }

        // if we boarded a transport, add us to it
        if (plrMover && !plrMover->m_transport)
        {
            // elevators also cause the client to send MOVEFLAG_ONTRANSPORT - just unmount if the guid can be found in the transport list
            for (MapManager::TransportSet::iterator iter = sMapMgr->m_Transports.begin(); iter != sMapMgr->m_Transports.end(); ++iter)
            {
                if ((*iter)->GetGUID() == movementInfo.t_guid)
                {
                    // unmount before boarding
                    plrMover->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

                    plrMover->m_transport = (*iter);
                    (*iter)->AddPassenger(plrMover);
                    break;
                }
            }
        }
    }
    else if (plrMover && plrMover->m_transport)               // if we were on a transport, leave
    {
        plrMover->m_transport->RemovePassenger(plrMover);
        plrMover->m_transport = nullptr;
        movementInfo.ClearTransportData();
    }

    if (plrMover && (movementInfo.HasMovementFlag(MOVEFLAG_SWIMMING) != plrMover->IsInWater()))
    {
        // now client not include swimming flag in case jumping under water
        plrMover->SetInWater(!plrMover->IsInWater() || plrMover->GetBaseMap()->IsUnderWater(movementInfo.GetPos()->GetPositionX(), movementInfo.GetPos()->GetPositionY(), movementInfo.GetPos()->GetPositionZ()));
    }

    if (plrMover)
        sAnticheatMgr->StartHackDetection(plrMover, movementInfo, opcode);

    uint32 mstime = getMSTime();
    /*----------------------*/
    if (m_clientTimeDelay == 0)
        m_clientTimeDelay = mstime > movementInfo.time ? std::min(mstime - movementInfo.time, (uint32)100) : 0;

    /* process position-change */
    WorldPacket data(opcode, mover->GetPackGUID().size() + recv_data.size());
    movementInfo.time = movementInfo.time + m_clientTimeDelay;
    data << mover->GetPackGUID();
    data.append(recv_data.contents(), recv_data.size());
    if (mover->isCharmed() && mover->GetCharmer())
        mover->GetCharmer()->SendMessageToSet(&data, false);
    else
        mover->SendMessageToSet(&data, false);

    mover->m_movementInfo = movementInfo;
    
    mover->UpdatePosition(movementInfo.pos);

     // fall damage generation (ignore in flight case that can be triggered also at lags in moment teleportation to another map).
    if (opcode == MSG_MOVE_FALL_LAND && plrMover && !plrMover->isInFlight())
        plrMover->HandleFallDamage(movementInfo);

    if (plrMover)                                            // nothing is charmed, or player charmed
    {
        if (mover->IsSitState() && movementInfo.GetMovementFlags() & (MOVEFLAG_MOVING | MOVEFLAG_TURNING))
            mover->SetStandState(UNIT_STAND_STATE_STAND);

        plrMover->UpdateFallInformationIfNeed(movementInfo, opcode);

        if (plrMover->InBattleGround())
        {
            switch (plrMover->GetMapId())
            {
                case 562: // Blade's Edge Arena
                    if (movementInfo.GetPos()->GetPositionZ() < -5.0f)
                        plrMover->HandleFallUnderMap();
                        break;
                case 559: // Nagrand Arena
                    if (movementInfo.GetPos()->GetPositionZ() < 6.0f)
                        plrMover->HandleFallUnderMap();
                    break;
                case 572: // Ruins of Lordaeron
                    if (movementInfo.GetPos()->GetPositionZ() < 20.0f)
                        plrMover->HandleFallUnderMap();
                    break;
                default: // battlegrounds
                    if (movementInfo.GetPos()->GetPositionZ() < -250.0f)
                        plrMover->HandleFallUnderMap();
                    break;
            }
        }
        else if (movementInfo.GetPos()->GetPositionZ() < -500.0f)
            plrMover->HandleFallUnderMap();
    }
}

void WorldSession::HandleForceSpeedChangeAck(WorldPacket &recv_data)
{
    sLog->outDebug("WORLD: Recvd CMSG_SPEED_CHANGE_ACK");
    /* extract packet */
    ObjectGuid guid;
    uint32 unk1;
    MovementInfo movementInfo;
    float newspeed;

    recv_data >> guid;
    recv_data >> unk1;                            // counter or moveEvent
    recv_data >> movementInfo;
    recv_data >> newspeed;

    // now can skip not our packet
    if (GetPlayer()->GetGUID() != guid.GetRawValue())
        return;
    /*----------------*/

    // Save movement flags
    GetPlayer()->SetUnitMovementFlags(movementInfo.GetMovementFlags());

    // client ACK send one packet for mounted/run case and need skip all except last from its
    // in other cases anti-cheat check can be fail in false case
    UnitMoveType move_type;
    UnitMoveType force_move_type;

    static char const* move_type_name[MAX_MOVE_TYPE] = {"Walk", "Run", "RunBack", "Swim", "SwimBack", "TurnRate", "Flight", "FlightBack"};

    uint16 opcode = recv_data.GetOpcode();
    switch (opcode)
    {
        case CMSG_FORCE_WALK_SPEED_CHANGE_ACK:          move_type = MOVE_WALK;          force_move_type = MOVE_WALK;        break;
        case CMSG_FORCE_RUN_SPEED_CHANGE_ACK:           move_type = MOVE_RUN;           force_move_type = MOVE_RUN;         break;
        case CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK:      move_type = MOVE_RUN_BACK;      force_move_type = MOVE_RUN_BACK;    break;
        case CMSG_FORCE_SWIM_SPEED_CHANGE_ACK:          move_type = MOVE_SWIM;          force_move_type = MOVE_SWIM;        break;
        case CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK:     move_type = MOVE_SWIM_BACK;     force_move_type = MOVE_SWIM_BACK;   break;
        case CMSG_FORCE_TURN_RATE_CHANGE_ACK:           move_type = MOVE_TURN_RATE;     force_move_type = MOVE_TURN_RATE;   break;
        case CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK:        move_type = MOVE_FLIGHT;        force_move_type = MOVE_FLIGHT;      break;
        case CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK:   move_type = MOVE_FLIGHT_BACK;   force_move_type = MOVE_FLIGHT_BACK; break;
        default:
            sLog->outError("WorldSession::HandleForceSpeedChangeAck: Unknown move type opcode: %u", opcode);
            return;
    }

    // skip all forced speed changes except last and unexpected
    // in run/mounted case used one ACK and it must be skipped.m_forced_speed_changes[MOVE_RUN] store both.
    if (GetPlayer()->m_forced_speed_changes[force_move_type] > 0)
    {
        --GetPlayer()->m_forced_speed_changes[force_move_type];
        if (GetPlayer()->m_forced_speed_changes[force_move_type] > 0)
            return;
    }
}

void WorldSession::HandleSetActiveMoverOpcode(WorldPacket &recv_data)
{
    sLog->outDebug("WORLD: Recvd CMSG_SET_ACTIVE_MOVER");

    uint64 guid;
    recv_data >> guid;

    if (GetPlayer()->IsInWorld())
        if (Unit *mover = ObjectAccessor::GetUnit(*GetPlayer(), guid))
            GetPlayer()->SetMover(mover);
    else
    {
        sLog->outError("HandleSetActiveMoverOpcode: incorrect mover guid: mover is " UI64FMTD " and should be " UI64FMTD, guid, _player->m_mover->GetGUID());
        GetPlayer()->SetMover(GetPlayer());
    }
}

void WorldSession::HandleMoveNotActiveMoverOpcode(WorldPacket &recv_data)
{
    sLog->outDebug("WORLD: Recvd CMSG_MOVE_NOT_ACTIVE_MOVER");
    recv_data.hexlike();

    uint64 old_mover_guid;
    MovementInfo mi;

    recv_data.readPackGUID(old_mover_guid);
    recv_data >> mi;

    if (!old_mover_guid)
        return;

    GetPlayer()->m_movementInfo = mi;
}

void WorldSession::HandleMountSpecialAnimOpcode(WorldPacket& /*recvdata*/)
{
    sLog->outDebug("WORLD: Recvd CMSG_MOUNTSPECIAL_ANIM");

    WorldPacket data(SMSG_MOUNTSPECIAL_ANIM, 8);
    data << uint64(GetPlayer()->GetGUID());

    GetPlayer()->SendMessageToSet(&data, false);
}

void WorldSession::HandleMoveKnockBackAck(WorldPacket & recv_data)
{
    // Currently not used but maybe use later for recheck final player position
    // (must be at call same as into "recv_data >> x >> y >> z >> orientation;"
    sLog->outDebug("CMSG_MOVE_KNOCK_BACK_ACK");

    MovementInfo movementInfo;
    uint64 guid;
    uint32 unk1;

    recv_data >> guid;                                      // guid
    recv_data >> unk1;                                      // unk
    recv_data >> movementInfo;

    if (GetPlayer()->GetGUID() != guid)
        return;

    // Save movement flags
    GetPlayer()->SetUnitMovementFlags(movementInfo.GetMovementFlags());
}

void WorldSession::HandleMoveFlyModeChangeAckOpcode(WorldPacket & recv_data)
{
    // fly mode on/off
    sLog->outDebug("WORLD: CMSG_MOVE_SET_CAN_FLY_ACK");
    //recv_data.hexlike();

    MovementInfo movementInfo;

    recv_data.read_skip<uint64>();                          // guid
    recv_data.read_skip<uint32>();                          // unk
    recv_data >> movementInfo;
    recv_data.read_skip<uint32>();                          // unk2

    GetPlayer()->SetUnitMovementFlags(movementInfo.GetMovementFlags());
}

void WorldSession::HandleMoveHoverAck(WorldPacket& recv_data)
{
    sLog->outDebug("CMSG_MOVE_HOVER_ACK");

    uint64 guid;
    recv_data.readPackGUID(guid);

    recv_data.read_skip<uint32>();

    MovementInfo movementInfo;
    recv_data >> movementInfo;

    recv_data.read_skip<uint32>();
}

void WorldSession::HandleMoveWaterWalkAck(WorldPacket& recv_data)
{
    sLog->outDebug("CMSG_MOVE_WATER_WALK_ACK");

    uint64 guid;
    recv_data.readPackGUID(guid);

    recv_data.read_skip<uint32>();

    MovementInfo movementInfo;
    recv_data >> movementInfo;

    recv_data.read_skip<uint32>();
}

void WorldSession::HandleSummonResponseOpcode(WorldPacket& recv_data)
{
    if (!GetPlayer()->isAlive() || GetPlayer()->isInCombat())
        return;

    uint64 summoner_guid;
    bool agree;
    recv_data >> summoner_guid;
    recv_data >> agree;

    GetPlayer()->SummonIfPossible(agree);
}

