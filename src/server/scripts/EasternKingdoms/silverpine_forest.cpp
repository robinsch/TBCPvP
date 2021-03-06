/*
 * Copyright (C) 2010-2012 Project SkyFire <http://www.projectskyfire.org/>
 * Copyright (C) 2010-2012 Oregon <http://www.oregoncore.com/>
 * Copyright (C) 2006-2008 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
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

/* ScriptData
SDName: Silverpine_Forest
SD%Complete: 100
SDComment: Quest support: 1886, 435, 452
SDCategory: Silverpine Forest
EndScriptData */

/* ContentData
npc_astor_hadren
npc_deathstalker_erland
pyrewood_ambush
EndContentData */

#include "ScriptPCH.h"
#include "ScriptedEscortAI.h"

/*######
## npc_astor_hadren
######*/

#define GOSSIP_HAH "You're Astor Hadren, right?"
#define GOSSIP_SAH "You've got something I need, Astor. And I'll be taking it now."

struct npc_astor_hadrenAI : public ScriptedAI
{
    npc_astor_hadrenAI(Creature *c) : ScriptedAI(c) {}

    void Reset()
    {
        me->setFaction(68);
    }

    void EnterCombat(Unit* /*who*/)
    {
    }

    void JustDied(Unit * /*who*/)
    {
        me->setFaction(68);
    }
};

CreatureAI* GetAI_npc_astor_hadren(Creature* creature)
{
    return new npc_astor_hadrenAI(creature);
}

bool GossipHello_npc_astor_hadren(Player* player, Creature* creature)
{
    if (player->GetQuestStatus(1886) == QUEST_STATUS_INCOMPLETE)
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_HAH, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

    player->SEND_GOSSIP_MENU(623, creature->GetGUID());

    return true;
}

bool GossipSelect_npc_astor_hadren(Player* player, Creature* creature, uint32 /*uiSender*/, uint32 uiAction)
{
    switch (uiAction)
    {
        case GOSSIP_ACTION_INFO_DEF + 1:
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_SAH, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
            player->SEND_GOSSIP_MENU(624, creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF + 2:
            player->CLOSE_GOSSIP_MENU();
            creature->setFaction(21);
            if (player)
                CAST_AI(npc_astor_hadrenAI, creature->AI())->AttackStart(player);
            break;
    }
    return true;
}

/*######
## npc_deathstalker_erland
######*/

enum eErland
{
    SAY_QUESTACCEPT     = -1000306,
    SAY_START           = -1000307,
    SAY_AGGRO_1         = -1000308,
    SAY_AGGRO_2         = -1000309,
    SAY_AGGRO_3         = -1000310,
    SAY_LAST            = -1000311,

    SAY_THANKS          = -1000312,
    SAY_RANE            = -1000313,
    SAY_ANSWER          = -1000314,
    SAY_MOVE_QUINN      = -1000315,

    SAY_GREETINGS       = -1000316,
    SAY_QUINN           = -1000317,
    SAY_ON_BYE          = -1000318,

    QUEST_ESCORTING     = 435,
    NPC_RANE            = 1950,
    NPC_QUINN           = 1951
};

struct npc_deathstalker_erlandAI : public npc_escortAI
{
    npc_deathstalker_erlandAI(Creature *c) : npc_escortAI(c) {}

    void WaypointReached(uint32 i)
    {
        Player* player = GetPlayerForEscort();

        if (!player)
            return;

        switch (i)
        {
        case 1: DoScriptText(SAY_START, me, player);break;
        case 13:
            DoScriptText(SAY_LAST, me, player);
            player->GroupEventHappens(QUEST_ESCORTING, me); break;
        case 14: DoScriptText(SAY_THANKS, me, player); break;
        case 15: {
                Unit* Rane = me->FindNearestCreature(NPC_RANE, 20);
                if (Rane)
                    DoScriptText(SAY_RANE, Rane);
                break;}
        case 16: DoScriptText(SAY_ANSWER, me); break;
        case 17: DoScriptText(SAY_MOVE_QUINN, me); break;
        case 24: DoScriptText(SAY_GREETINGS, me); break;
        case 25: {
                Unit* Quinn = me->FindNearestCreature(NPC_QUINN, 20);
                if (Quinn)
                    DoScriptText(SAY_QUINN, Quinn);
                break;}
        case 26: DoScriptText(SAY_ON_BYE, me, NULL); break;
        }
    }

    void Reset() {}

    void EnterCombat(Unit* who)
    {
        DoScriptText(RAND(SAY_AGGRO_1, SAY_AGGRO_2, SAY_AGGRO_3), me, who);
    }
};

bool QuestAccept_npc_deathstalker_erland(Player* player, Creature* creature, Quest const* quest)
{
    if (quest->GetQuestId() == QUEST_ESCORTING)
    {
        DoScriptText(SAY_QUESTACCEPT, creature, player);

        if (npc_escortAI* pEscortAI = CAST_AI(npc_deathstalker_erlandAI, creature->AI()))
            pEscortAI->Start(true, false, player->GetGUID());
    }

    return true;
}

CreatureAI* GetAI_npc_deathstalker_erlandAI(Creature* creature)
{
    return new npc_deathstalker_erlandAI(creature);
}

/*######
## pyrewood_ambush
#######*/

#define QUEST_PYREWOOD_AMBUSH 452

#define NPCSAY_INIT "Get ready, they'll be arriving any minute..." //not blizzlike
#define NPCSAY_END "Thanks for your help!" //not blizzlike

static float PyrewoodSpawnPoints[3][4] =
{
    //pos_x   pos_y     pos_z    orien
    //door
    {-396.17f, 1505.86f, 19.77f, 0},
    {-396.91f, 1505.77f, 19.77f, 0},
    {-397.94f, 1504.74f, 19.77f, 0},
};

#define WAIT_SECS 6000

struct pyrewood_ambushAI : public ScriptedAI
{
    pyrewood_ambushAI(Creature *c) : ScriptedAI(c), Summons(me)
    {
       QuestInProgress = false;
    }

    uint32 Phase;
    int8 KillCount;
    uint32 WaitTimer;
    uint64 PlayerGUID;
    SummonList Summons;

    bool QuestInProgress;

    void Reset()
    {
        WaitTimer = WAIT_SECS;

        if (!QuestInProgress) //fix reset values (see UpdateVictim)
        {
            Phase = 0;
            KillCount = 0;
            PlayerGUID = 0;
            Summons.DespawnAll();
        }
    }

    void EnterCombat(Unit * /*who*/){}

    void JustSummoned(Creature *pSummoned)
    {
        Summons.Summon(pSummoned);
        ++KillCount;
    }

    void SummonedCreatureDespawn(Creature *pSummoned)
    {
        Summons.Despawn(pSummoned);
        --KillCount;
    }

    void SummonCreatureWithRandomTarget(uint32 creatureId, int position)
    {
        if (Creature *pSummoned = me->SummonCreature(creatureId, PyrewoodSpawnPoints[position][0], PyrewoodSpawnPoints[position][1], PyrewoodSpawnPoints[position][2], PyrewoodSpawnPoints[position][3], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15000))
        {
            Player* player = NULL;
            Unit *pTarget = NULL;
            if (PlayerGUID)
            {
                player = Unit::GetPlayer(*me, PlayerGUID);
                if (player)
                    pTarget = RAND((Unit*)me, (Unit*)player);
            } else
                pTarget = me;

            if (pTarget)
            {
                pSummoned->setFaction(168);
                pSummoned->AddThreat(pTarget, 32.0f);
                pSummoned->AI()->AttackStart(pTarget);
            }
        }
    }

    void JustDied(Unit * /*pKiller*/)
    {
        if (PlayerGUID)
            if (Player* player = Unit::GetPlayer(*me, PlayerGUID))
                if (player->GetQuestStatus(QUEST_PYREWOOD_AMBUSH) == QUEST_STATUS_INCOMPLETE)
                    player->FailQuest(QUEST_PYREWOOD_AMBUSH);
    }

    void UpdateAI(const uint32 diff)
    {
        //sLog->outString("DEBUG: p(%i) k(%i) d(%u) W(%i)", Phase, KillCount, diff, WaitTimer);

        if (!QuestInProgress)
            return;

        if (KillCount && Phase < 6)
        {
            if (!UpdateVictim()) //reset() on target Despawn...
                return;

            DoMeleeAttackIfReady();
            return;
        }

        switch (Phase)
        {
            case 0:
                if (WaitTimer == WAIT_SECS)
                    me->MonsterSay(NPCSAY_INIT, LANG_UNIVERSAL, 0); //no blizzlike

                if (WaitTimer <= diff)
                {
                    WaitTimer -= diff;
                    return;
                }
                break;
            case 1:
                SummonCreatureWithRandomTarget(2060, 1);
                break;
            case 2:
                SummonCreatureWithRandomTarget(2061, 2);
                SummonCreatureWithRandomTarget(2062, 0);
                break;
            case 3:
                SummonCreatureWithRandomTarget(2063, 1);
                SummonCreatureWithRandomTarget(2064, 2);
                SummonCreatureWithRandomTarget(2065, 0);
                break;
            case 4:
                SummonCreatureWithRandomTarget(2066, 1);
                SummonCreatureWithRandomTarget(2067, 0);
                SummonCreatureWithRandomTarget(2068, 2);
                break;
            case 5: //end
                if (PlayerGUID)
                {
                    if (Player* player = Unit::GetPlayer(*me, PlayerGUID))
                    {
                        me->MonsterSay(NPCSAY_END, LANG_UNIVERSAL, 0); //not blizzlike
                        player->GroupEventHappens(QUEST_PYREWOOD_AMBUSH, me);
                    }
                }
                QuestInProgress = false;
                Reset();
                break;
        }
        ++Phase; //prepare next phase
    }
};

CreatureAI* GetAI_pyrewood_ambush(Creature* creature)
{
    return new pyrewood_ambushAI (creature);
}

bool QuestAccept_pyrewood_ambush(Player* player, Creature* creature, const Quest *pQuest)
{
    if (pQuest->GetQuestId() == QUEST_PYREWOOD_AMBUSH && !CAST_AI(pyrewood_ambushAI, creature->AI())->QuestInProgress)
    {
        CAST_AI(pyrewood_ambushAI, creature->AI())->QuestInProgress = true;
        CAST_AI(pyrewood_ambushAI, creature->AI())->Phase = 0;
        CAST_AI(pyrewood_ambushAI, creature->AI())->KillCount = 0;
        CAST_AI(pyrewood_ambushAI, creature->AI())->PlayerGUID = player->GetGUID();
    }

    return true;
}

/*######
## AddSC
######*/

void AddSC_silverpine_forest()
{
    Script *newscript;

    newscript = new Script;
    newscript->Name = "npc_astor_hadren";
    newscript->pGossipHello =  &GossipHello_npc_astor_hadren;
    newscript->pGossipSelect = &GossipSelect_npc_astor_hadren;
    newscript->GetAI = &GetAI_npc_astor_hadren;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "npc_deathstalker_erland";
    newscript->GetAI = &GetAI_npc_deathstalker_erlandAI;
    newscript->pQuestAccept = &QuestAccept_npc_deathstalker_erland;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "pyrewood_ambush";
    newscript->GetAI = &GetAI_pyrewood_ambush;
    newscript->pQuestAccept = &QuestAccept_pyrewood_ambush;
    newscript->RegisterSelf();
}
