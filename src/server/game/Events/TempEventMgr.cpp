#include "TempEventMgr.h"
#include "Player.h"
#include "Chat.h"
#include "World.h"
#include "Language.h"
#include "MapManager.h"

TempEventMgr::TempEventMgr()
{
    m_EventStatus = EVENT_STATUS_INACTIVE;
    m_PlayerLimit = PLAYER_LIMIT_NOT_SET;
    m_EventParticipants.clear();

    eventLoc.mapId       = 0;
    eventLoc.zoneId      = 0;
    eventLoc.x           = 0;
    eventLoc.y           = 0;
    eventLoc.z           = 0;
    eventLoc.orientation = 0;
}

void TempEventMgr::SetEventStatus(Player* pInvoker, uint32 eventStatus)
{
    switch (eventStatus)
    {
        case EVENT_STATUS_INACTIVE:
        {
            break;
        }
        case EVENT_STATUS_ACTIVE:
        {
            if (GetEventStatus() == EVENT_STATUS_ACTIVE)
            {
                ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_ALREADY_ACTIVE);
                return;
            }

            if (!HasEventLocation())
            {
                ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_NOT_SET_LOCATION);
                return;
            }

            if (GetPlayerLimit() == PLAYER_LIMIT_NOT_SET)
            {
                ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_NOT_SET_PLIMIT);
                SetPlayerLimit(PLAYER_LIMIT_DEFAULT);
            }

            break;
        }
        case EVENT_STATUS_IN_PROGRESS:
        {
            break;
        }
        case EVENT_STATUS_FINISHED:
        {
            TeleportPlayersToEntryPoint(pInvoker);
            break;
        }
    }

    m_EventStatus = eventStatus;
}

void TempEventMgr::ResetEvent()
{
    // Destructor?
    m_EventStatus = EVENT_STATUS_INACTIVE;
    m_PlayerLimit = PLAYER_LIMIT_NOT_SET;
    m_EventParticipants.clear();

    eventLoc.mapId       = 0;
    eventLoc.zoneId      = 0;
    eventLoc.x           = 0;
    eventLoc.y           = 0;
    eventLoc.z           = 0;
    eventLoc.orientation = 0;
}

bool TempEventMgr::AddEventLocation(uint32 mapId, uint32 zoneId, float x, float y, float z, float orientation)
{
    if (!sMapMgr->IsValidMapCoord(mapId, x, y, z))
        return false;

    eventLoc.mapId          = mapId;
    eventLoc.zoneId         = zoneId;
    eventLoc.x              = x;
    eventLoc.y              = y;
    eventLoc.z              = z;
    eventLoc.orientation    = orientation;

    return true;
}

void TempEventMgr::AddPlayerToEvent(Player* player)
{
    uint32 playerLimit = GetPlayerLimit();

    if (GetEventStatus() == EVENT_STATUS_INACTIVE)
    {
        ChatHandler(player).PSendSysMessage(LANG_TEMPEVENT_NO_ACTIVE_EVENTS);
        return;
    }

    if (HasPlayerLimit() && (GetEventParticipantCount() >= playerLimit))
    {
        ChatHandler(player).PSendSysMessage(LANG_TEMPEVENT_ENOUGH_PARTICIPANTS);
        return;
    }

    if (m_EventParticipants.find(player) == m_EventParticipants.end())
        m_EventParticipants.insert(player);

    ChatHandler(player).PSendSysMessage(LANG_TEMPEVENT_PLAYER_JOINED);

    if (HasPlayerLimit())
        ChatHandler(player).PSendSysMessage(LANG_TEMPEVENT_QUEUE_STATUS, GetEventParticipantCount(), playerLimit);
}

void TempEventMgr::RemovePlayerFromEvent(Player* player, bool sendNotification)
{
    EventParticipants::iterator iter = m_EventParticipants.find(player);
    if (iter == m_EventParticipants.end())
        return;

    m_EventParticipants.erase(iter);

    if (sendNotification)
        ChatHandler(player).PSendSysMessage(LANG_TEMPEVENT_PLAYER_LEFT);
}

bool TempEventMgr::TeleportPlayersToEvent(Player* pInvoker)
{
    if (GetEventStatus() == EVENT_STATUS_INACTIVE)
    {
        ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_NO_ACTIVE_EVENTS);
        return false;
    }

    for (EventParticipants::const_iterator itr = m_EventParticipants.begin(); itr != m_EventParticipants.end(); ++itr)
    {
        if ((*itr)->GetZoneId() == eventLoc.zoneId)
            continue;

        (*itr)->SendSummonRequestFrom(pInvoker);
    }

    return true;
}

bool TempEventMgr::TeleportPlayersToEntryPoint(Player* pInvoker)
{
    for (EventParticipants::const_iterator itr = m_EventParticipants.begin(); itr != m_EventParticipants.end(); ++itr)
    {
        // only teleport player to homebind if they are still in the event location
        if ((*itr)->GetZoneId() != eventLoc.zoneId)
            continue;

        // ITSLOVELOL: Need to be changed to match function name
        (*itr)->TeleportToHomebind();
    }

    return true;
}

char *GetStatusString(uint32 eventStatus)
{
    switch (eventStatus)
    {
        case EVENT_STATUS_INACTIVE:
            return "Inactive";
        case EVENT_STATUS_ACTIVE:
            return "Active";
        case EVENT_STATUS_IN_PROGRESS:
            return "Running";
        case EVENT_STATUS_FINISHED:
            return "Finished";
    }

    return "";
}

char *classString(uint8 playerClass)
{
    switch (playerClass)
    {
        case CLASS_WARRIOR: return "Warrior"; break;
        case CLASS_PALADIN: return "Paladin"; break;
        case CLASS_HUNTER:  return "Hunter";  break;
        case CLASS_ROGUE:   return "Rogue";   break;
        case CLASS_PRIEST:  return "Priest";  break;
        case CLASS_SHAMAN:  return "Shaman";  break;
        case CLASS_MAGE:    return "Mage";    break;
        case CLASS_WARLOCK: return "Warlock"; break;
        case CLASS_DRUID:   return "Druid";   break;
        default: return ""; break;
    }
}

char *raceString(uint8 playerRace)
{
    switch (playerRace)
    {
        case RACE_HUMAN:         return "Human";    break;
        case RACE_ORC:           return "Orc";      break;
        case RACE_DWARF:         return "Dwarf";    break;
        case RACE_NIGHTELF:      return "Nightelf"; break;
        case RACE_UNDEAD_PLAYER: return "Undead";   break;
        case RACE_TAUREN:        return "Tauren";   break;
        case RACE_GNOME:         return "Gnome";    break;
        case RACE_TROLL:         return "Troll";    break;
        case RACE_BLOODELF:      return "Bloodelf"; break;
        case RACE_DRAENEI:       return "Draenei";  break;
        default: return ""; break;
    }
}

void TempEventMgr::SendEventInfoString(Player* pInvoker)
{
    if (GetEventStatus() == EVENT_STATUS_INACTIVE)
    {
        ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_NO_ACTIVE_EVENTS);
        return;
    }

    MapEntry const* mapEntry;
    AreaTableEntry const* zoneEntry;
    
    ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_INFO);
    ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_INFO_STATUS, GetStatusString(GetEventStatus()));
    
    int loc = pInvoker->GetSession() ? pInvoker->GetSession()->GetSessionDbcLocale() : sWorld->GetDefaultDbcLocale();
    if (HasEventLocation())
    {
        mapEntry = sMapStore.LookupEntry(eventLoc.mapId);
        zoneEntry = GetAreaEntryByAreaID(eventLoc.zoneId);

        ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_INFO_LOCATION, 
            mapEntry->name[loc], 
            zoneEntry->area_name[loc]);
    }

    if (HasPlayerLimit())
        ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_INFO_PLIMIT, GetPlayerLimit());

    ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_INFO_PCOUNT, GetEventParticipantCount());

    for (EventParticipants::const_iterator itr = m_EventParticipants.begin(); itr != m_EventParticipants.end(); ++itr)
    {
        mapEntry = sMapStore.LookupEntry((*itr)->GetMapId());
        zoneEntry = GetAreaEntryByAreaID((*itr)->GetZoneId());

        ChatHandler(pInvoker).PSendSysMessage(LANG_TEMPEVENT_INFO_PARTICIPANTS, 
            (*itr)->GetName(),
            raceString((*itr)->getRace()),
            classString((*itr)->getClass()),
            mapEntry->name[loc],
            zoneEntry->area_name[loc]);
    }

}
