-- Bonechewer Scavenger
SET @ENTRY := 18952;
UPDATE `creature_template` SET `AIName`='EventAI' WHERE `entry`=@ENTRY;
DELETE FROM `creature_ai_scripts` WHERE `creature_id`=@ENTRY;
INSERT INTO `creature_ai_scripts` (`id`, `creature_id`, `event_type`, `event_inverse_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action1_type`, `action1_param1`, `action1_param2`, `action1_param3`, `action2_type`, `action2_param1`, `action2_param2`, `action2_param3`, `action3_type`, `action3_param1`, `action3_param2`, `action3_param3`, `comment`) VALUES 
(@ENTRY*100+1, @ENTRY, 0, 0, 100, 0, 2000, 4000, 5000, 8000, 11, 34113, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bonechewer Scavenger - Cast Bonechewer Bite'),
(@ENTRY*100+2, @ENTRY, 4, 0, 100, 1, 0, 0, 0, 0, 11, 13398, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bonechewer Scavenger - Cast Throw Wrench on Aggro'),
(@ENTRY*100+3, @ENTRY, 2, 0, 100, 0, 15, 0, 0, 0, 25, 0, 0, 0, 1, -47, 0, 0, 0, 0, 0, 0, 'Bonechewer Scavenger - Flee at 15% HP');

-- Bonechewer Mutant
SET @ENTRY := 16876;
UPDATE `creature_template` SET `AIName`='EventAI' WHERE `entry`=@ENTRY;
DELETE FROM `creature_ai_scripts` WHERE `creature_id`=@ENTRY;
INSERT INTO `creature_ai_scripts` (`id`, `creature_id`, `event_type`, `event_inverse_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action1_type`, `action1_param1`, `action1_param2`, `action1_param3`, `action2_type`, `action2_param1`, `action2_param2`, `action2_param3`, `action3_type`, `action3_param1`, `action3_param2`, `action3_param3`, `comment`) VALUES 
(@ENTRY*100+1, @ENTRY, 0, 0, 100, 0, 2000, 4000, 5000, 8000, 11, 34113, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bonechewer Mutant - Cast Bonechewer Bite'),
(@ENTRY*100+2, @ENTRY, 2, 0, 100, 1, 30, 0, 0, 0, 11, 8599, 0, 1, 1, -46, 0, 0, 0, 0, 0, 0, 'Bonechewer Mutant - Cast Enrage and Say Text at 30% HP'),
(@ENTRY*100+3, @ENTRY, 2, 0, 100, 1, 20, 0, 0, 0, 11, 34114, 0, 1, 1, -149, 0, 0, 0, 0, 0, 0, 'Bonechewer Mutant - Cast Instability and Say Text at 20% HP');

-- Bonechewer Raider
SET @ENTRY := 16925;
UPDATE `creature_template` SET `AIName`='EventAI' WHERE `entry`=@ENTRY;
DELETE FROM `creature_ai_scripts` WHERE `creature_id`=@ENTRY;
INSERT INTO `creature_ai_scripts` (`id`, `creature_id`, `event_type`, `event_inverse_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action1_type`, `action1_param1`, `action1_param2`, `action1_param3`, `action2_type`, `action2_param1`, `action2_param2`, `action2_param3`, `action3_type`, `action3_param1`, `action3_param2`, `action3_param3`, `comment`) VALUES 
(@ENTRY*100+1, @ENTRY, 4, 0, 100, 1, 0, 0, 0, 0, 11, 32723, 0, 1, 43, 0, 0, 0, 0, 0, 0, 0, 'Bonechewer Raider - Cast Summon Bonechewer Riding Wolf and Dismount on Aggro');
