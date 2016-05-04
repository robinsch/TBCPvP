-- Gossip Menu for Nazgrel
INSERT INTO `npc_gossip` (`npc_guid`, `textid`) VALUES ('22491', '9995');
UPDATE `creature_template` SET `ScriptName`='npc_nazgrel' WHERE (`entry`='3230');
