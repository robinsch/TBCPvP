-- Paladin [Stoicism]
UPDATE `spell_affect` SET `SpellFamilyMask`='120122641025943' WHERE (`entry`='31844') AND effectId = 1;
UPDATE `spell_affect` SET `SpellFamilyMask`='120122641025943' WHERE (`entry`='31845') AND effectId = 1;

-- Druid [Subtlety]
UPDATE `spell_affect` SET `SpellFamilyMask`='12965527033218834' WHERE (`entry`='17118') AND effectId = 1;
UPDATE `spell_affect` SET `SpellFamilyMask`='12965527033218834' WHERE (`entry`='17119') AND effectId = 1;
UPDATE `spell_affect` SET `SpellFamilyMask`='12965527033218834' WHERE (`entry`='17120') AND effectId = 1;
UPDATE `spell_affect` SET `SpellFamilyMask`='12965527033218834' WHERE (`entry`='17121') AND effectId = 1;
UPDATE `spell_affect` SET `SpellFamilyMask`='12965527033218834' WHERE (`entry`='17122') AND effectId = 1;

-- Priest [Silent Resolve]
DELETE FROM `spell_affect` WHERE entry in (14523, 14784, 14785, 14786, 14787) AND effectId = 1;
INSERT INTO `spell_affect` VALUES(14523, 1, 24368649060719);
INSERT INTO `spell_affect` VALUES(14784, 1, 24368649060719);
INSERT INTO `spell_affect` VALUES(14785, 1, 24368649060719);
INSERT INTO `spell_affect` VALUES(14786, 1, 24368649060719);
INSERT INTO `spell_affect` VALUES(14787, 1, 24368649060719);

-- Shaman [Healing Grace]
UPDATE `spell_affect` SET `SpellFamilyMask`='14231441312768' WHERE (`entry`='29187') AND effectId = 1;
UPDATE `spell_affect` SET `SpellFamilyMask`='14231441312768' WHERE (`entry`='29189') AND effectId = 1;
UPDATE `spell_affect` SET `SpellFamilyMask`='14231441312768' WHERE (`entry`='29191') AND effectId = 1;
