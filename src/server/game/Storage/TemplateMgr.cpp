#include "TemplateMgr.h"
#include "DBCStores.h"

void TemplateMgr::LoadItems()
{
	QueryResult_AutoPtr result = WorldDatabase.PQuery(
        "SELECT templateEntry, itemEntry, itemAmount, enchantEntry, gemEntry1, gemEntry2, gemEntry3"
        " FROM template_item ORDER BY addPriority");

    if (result)
    {
        do
        {
            Field* pFields = result->Fetch();

            TemplateItem pTemp;
            uint32 templateEntry = pFields[0].GetUInt32();
            pTemp.itemEntry = pFields[1].GetUInt32();
            pTemp.itemAmount = pFields[2].GetUInt32();
            pTemp.enchantEntry = pFields[3].GetUInt32();
            for (uint8 i = 0; i < MAX_GEM_SOCKETS; ++i)
                pTemp.gemEnchantEntry[i] = pFields[4 + i].GetUInt32();

            templateItems[templateEntry].push_back(pTemp);
        } while (result->NextRow());
    }
}

void TemplateMgr::LoadSpells()
{
    QueryResult_AutoPtr result = WorldDatabase.PQuery(
        "SELECT templateEntry, spellEntry"
        " FROM template_spell");

    if (result)
    {
        do
        {
            Field* pFields = result->Fetch();

            uint32 templateEntry = pFields[0].GetUInt32();
            uint32 spellEntry = pFields[1].GetUInt32();

            templateSpells[templateEntry].push_back(spellEntry);
        } while (result->NextRow());
    }
}

void TemplateMgr::LoadReputations()
{
    QueryResult_AutoPtr result = WorldDatabase.PQuery(
        "SELECT templateEntry, factionEntry, factionStanding"
        " FROM template_reputation");

    if (result)
    {
        do
        {
            Field* pFields = result->Fetch();

            TemplateReputation pTemp;
            uint32 templateEntry = pFields[0].GetUInt32();
            pTemp.factionEntry = pFields[1].GetUInt32();
            pTemp.factionStanding = pFields[2].GetUInt32();

            templateReputations[templateEntry].push_back(pTemp);
        } while (result->NextRow());
    }
}

void TemplateMgr::AddItems(Player* player, TemplateItemEntry te)
{
    auto it = templateItems.find(uint32(te));
    if (it != templateItems.end())
    {
        for (auto it2 : it->second)
        {
            uint32 itemEntry = it2.itemEntry;
            uint32 itemAmount = it2.itemAmount;
            uint32 enchantEntry = itemAmount > 1 ? 0 : it2.enchantEntry;
            uint32 gemEnchantEntry[MAX_GEM_SOCKETS];
            for (uint8 i = 0; i < MAX_GEM_SOCKETS; ++i)
                gemEnchantEntry[i] = it2.gemEnchantEntry[i];

            // equip item
            player->StoreNewItemInBestSlots(itemEntry, itemAmount);

            if (Item* item = player->GetItemByEntry(itemEntry))
            {
                uint32 slot = item->GetSlot();

                // enchant item
                if (enchantEntry)
                {
                    // remove old enchanting before applying new
                    player->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
                    item->SetEnchantment(PERM_ENCHANTMENT_SLOT, enchantEntry, 0, 0);
                    player->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, true);
                }
                // socket item
                if (gemEnchantEntry)
                {
                    bool SocketBonusActivated = item->GemsFitSockets();    // save state of socketbonus
                    player->ToggleMetaGemsActive(slot, false);             // turn off all metagems (except for the target item)
                                                                           // if a meta gem is being equipped, all information has to be written to the item before testing if the conditions for the gem are met
                                                                           // remove ALL enchants
                    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + MAX_GEM_SOCKETS; ++enchant_slot)
                        player->ApplyEnchantment(item, EnchantmentSlot(enchant_slot), false);

                    for (uint8 i = 0; i < MAX_GEM_SOCKETS; ++i)
                    {
                        if (gemEnchantEntry[i])
                            item->SetEnchantment(EnchantmentSlot(SOCK_ENCHANTMENT_SLOT + i), gemEnchantEntry[i], 0, 0);
                    }

                    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT + MAX_GEM_SOCKETS; ++enchant_slot)
                        player->ApplyEnchantment(item, EnchantmentSlot(enchant_slot), true);

                    bool SocketBonusToBeActivated = item->GemsFitSockets(); // current socketbonus state
                    if (SocketBonusActivated != SocketBonusToBeActivated)   // if there was a change...
                    {
                        player->ApplyEnchantment(item, BONUS_ENCHANTMENT_SLOT, false);
                        item->SetEnchantment(BONUS_ENCHANTMENT_SLOT, (SocketBonusToBeActivated ? item->GetProto()->socketBonus : 0), 0, 0);
                        player->ApplyEnchantment(item, BONUS_ENCHANTMENT_SLOT, true);
                        // it is not displayed, client has an inbuilt system to determine if the bonus is activated
                    }

                    player->ToggleMetaGemsActive(slot, true);               // turn on all metagems (except for target item)
                }
            }
        }
    }
}

void TemplateMgr::AddSpells(Player* player, TemplateSpellEntry te)
{
    auto it = templateSpells.find(uint32(te));
    if (it != templateSpells.end())
    {
        for (auto spellEntry : it->second)
            player->learnSpell(spellEntry);
    }
}

void TemplateMgr::AddReputations(Player* player, TemplateReputationEntry te)
{
    auto it = templateReputations.find(uint32(te));
    if (it != templateReputations.end())
    {
        for (auto it2 : it->second)
        {
            if (FactionEntry const* fe = sFactionStore.LookupEntry(it2.factionEntry))
				player->ModifyFactionReputation(fe, it2.factionStanding);
        }
    }
}

TemplateSpellEntry const TemplateMgr::GetTemplateSpellEntryForClass(uint32 playerClass)
{
    switch (playerClass)
    {
        case CLASS_WARRIOR: return DEFAULT_WARRIOR; break;
        case CLASS_PALADIN: return DEFAULT_PALADIN; break;
        case CLASS_HUNTER:  return DEFAULT_HUNTER;  break;
        case CLASS_ROGUE:   return DEFAULT_ROGUE;   break;
        case CLASS_PRIEST:  return DEFAULT_PRIEST;  break;
        case CLASS_SHAMAN:  return DEFAULT_SHAMAN;  break;
        case CLASS_MAGE:    return DEFAULT_MAGE;    break;
        case CLASS_WARLOCK: return DEFAULT_WARLOCK; break;
        case CLASS_DRUID:   return DEFAULT_DRUID;   break;
        default: return TemplateSpellEntry(0);
    }
}

TemplateItemEntry const TemplateMgr::GetDefaultTemplateItemEntryForClass(uint32 playerClass)
{
    switch (playerClass)
    {
        case CLASS_WARRIOR: return DEFAULT_ITEM_WARRIOR; break;
        case CLASS_PALADIN: return DEFAULT_ITEM_PALADIN; break;
        case CLASS_HUNTER:  return DEFAULT_ITEM_HUNTER;  break;
        case CLASS_ROGUE:   return DEFAULT_ITEM_ROGUE;   break;
        case CLASS_PRIEST:  return DEFAULT_ITEM_PRIEST;  break;
        case CLASS_SHAMAN:  return DEFAULT_ITEM_SHAMAN;  break;
        case CLASS_MAGE:    return DEFAULT_ITEM_MAGE;    break;
        case CLASS_WARLOCK: return DEFAULT_ITEM_WARLOCK; break;
        case CLASS_DRUID:   return DEFAULT_ITEM_DRUID;   break;
        default: return TemplateItemEntry(0);
    }
}
