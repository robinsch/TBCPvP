-- Gossip Menu for Far Seer Regulkut
UPDATE `npc_text` SET `text0_1`='Well met, $c. Your arrival in Thrallmar could not have occurred at a better time. The warchief is depending on us, since he cannot be here himself.' WHERE (`ID`='8718');
INSERT INTO `npc_gossip` (`npc_guid`, `textid`) VALUES ('57479', '8718');