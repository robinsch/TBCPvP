-- Bonechewer Scavenger
SET @ENTRY := 18952;
UPDATE `creature_template` SET `AIName`='EventAI' WHERE `entry`=@ENTRY;
DELETE FROM `creature_ai_scripts` WHERE `creature_id`=@ENTRY;
INSERT INTO `creature_ai_scripts` (`id`, `creature_id`, `event_type`, `event_inverse_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action1_type`, `action1_param1`, `action1_param2`, `action1_param3`, `action2_type`, `action2_param1`, `action2_param2`, `action2_param3`, `action3_type`, `action3_param1`, `action3_param2`, `action3_param3`, `comment`) VALUES 
(@ENTRY*100+1, @ENTRY, 0, 0, 100, 1, 2000, 4000, 5000, 8000, 11, 34113, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bonechewer Scavenger - Cast Bonechewer Bite'),
(@ENTRY*100+2, @ENTRY, 4, 0, 100, 0, 0, 0, 0, 0, 11, 13398, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bonechewer Scavenger - Cast Throw Wrench on Aggro'),
(@ENTRY*100+3, @ENTRY, 2, 0, 100, 0, 15, 0, 0, 0, 25, 0, 0, 0, 1, -47, 0, 0, 0, 0, 0, 0, 'Bonechewer Scavenger - Flee at 15% HP');

-- Bonechewer Mutant
SET @ENTRY := 16876;
UPDATE `creature_template` SET `AIName`='EventAI' WHERE `entry`=@ENTRY;
DELETE FROM `creature_ai_scripts` WHERE `creature_id`=@ENTRY;
INSERT INTO `creature_ai_scripts` (`id`, `creature_id`, `event_type`, `event_inverse_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action1_type`, `action1_param1`, `action1_param2`, `action1_param3`, `action2_type`, `action2_param1`, `action2_param2`, `action2_param3`, `action3_type`, `action3_param1`, `action3_param2`, `action3_param3`, `comment`) VALUES 
(@ENTRY*100+1, @ENTRY, 0, 0, 100, 1, 2000, 4000, 5000, 8000, 11, 34113, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bonechewer Mutant - Cast Bonechewer Bite'),
(@ENTRY*100+2, @ENTRY, 2, 0, 100, 0, 30, 0, 0, 0, 11, 8599, 0, 1, 1, -46, 0, 0, 0, 0, 0, 0, 'Bonechewer Mutant - Cast Enrage and Say Text at 30% HP'),
(@ENTRY*100+3, @ENTRY, 2, 0, 100, 0, 20, 0, 0, 0, 11, 34114, 0, 1, 1, -149, 0, 0, 0, 0, 0, 0, 'Bonechewer Mutant - Cast Instability and Say Text at 20% HP');

-- Bonechewer Raider
SET @ENTRY := 16925;
UPDATE `creature_template` SET `AIName`='EventAI' WHERE `entry`=@ENTRY;
DELETE FROM `creature_ai_scripts` WHERE `creature_id`=@ENTRY;
INSERT INTO `creature_ai_scripts` (`id`, `creature_id`, `event_type`, `event_inverse_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action1_type`, `action1_param1`, `action1_param2`, `action1_param3`, `action2_type`, `action2_param1`, `action2_param2`, `action2_param3`, `action3_type`, `action3_param1`, `action3_param2`, `action3_param3`, `comment`) VALUES 
(@ENTRY*100+1, @ENTRY, 4, 0, 100, 1, 0, 0, 0, 0, 11, 32723, 0, 1, 43, 0, 0, 0, 0, 0, 0, 0, 'Bonechewer Raider - Cast Summon Bonechewer Riding Wolf and Dismount on Aggro');

-- Shattered Hand Neophyte
SET @ENTRY := 19410;
UPDATE `creature_template` SET `AIName`='EventAI' WHERE `entry`=@ENTRY;
DELETE FROM `creature_ai_scripts` WHERE `creature_id`=@ENTRY;
INSERT INTO `creature_ai_scripts` (`id`, `creature_id`, `event_type`, `event_inverse_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action1_type`, `action1_param1`, `action1_param2`, `action1_param3`, `action2_type`, `action2_param1`, `action2_param2`, `action2_param3`, `action3_type`, `action3_param1`, `action3_param2`, `action3_param3`, `comment`) VALUES 
(@ENTRY*100+1, @ENTRY, 0, 0, 100, 1, 4000, 4500, 16000, 17000, 11, 11639, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Shattered Hand Neophyte - Cast Shadow Word: Pain'),
(@ENTRY*100+2, @ENTRY, 2, 0, 100, 0, 30, 0, 0, 0, 11, 6742, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Shattered Hand Neophyte - Cast Bloodlust at 30% HP'),
(@ENTRY*100+3, @ENTRY, 16, 0, 100, 1, 6742, 30, 22000, 25000, 11, 6742, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Shattered Hand Neophyte - Cast Bloodlust on Friendlies Missing Buff'),
(@ENTRY*100+4, @ENTRY, 2, 0, 100, 1, 50, 0, 0, 0, 11, 11640, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Shattered Hand Neophyte - Cast Renew at 50% HP'),
(@ENTRY*100+5, @ENTRY, 2, 0, 100, 0, 15, 0, 0, 0, 25, 0, 0, 0, 1, -47, 0, 0, 0, 0, 0, 0, 'Shattered Hand Neophyte - Flee at 15% HP'),
(@ENTRY*100+6, @ENTRY, 6, 0, 100, 0, 0, 0, 0, 0, 11, 38879, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0, 'Shattered Hand Neophyte - Cast Soul Burden on Death');

-- Shattered Hand Grunt
SET @ENTRY := 16867;
UPDATE `creature_template` SET `AIName`='EventAI' WHERE `entry`=@ENTRY;
DELETE FROM `creature_ai_scripts` WHERE `creature_id`=@ENTRY;
INSERT INTO `creature_ai_scripts` (`id`, `creature_id`, `event_type`, `event_inverse_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action1_type`, `action1_param1`, `action1_param2`, `action1_param3`, `action2_type`, `action2_param1`, `action2_param2`, `action2_param3`, `action3_type`, `action3_param1`, `action3_param2`, `action3_param3`, `comment`) VALUES 
(@ENTRY*100+1, @ENTRY, 9, 0, 100, 1, 0, 5, 8000, 9000, 11, 11976, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Shattered Hand Grunt - Cast Strike'),
(@ENTRY*100+2, @ENTRY, 0, 0, 100, 1, 6000, 8000, 12000, 14000, 11, 11978, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Shattered Hand Grunt - Cast Kick'),
(@ENTRY*100+3, @ENTRY, 2, 0, 100, 0, 15, 0, 0, 0, 25, 0, 0, 0, 1, -47, 0, 0, 0, 0, 0, 0, 'Shattered Hand Grunt - Flee at 15% HP'),
(@ENTRY*100+4, @ENTRY, 6, 0, 100, 0, 0, 0, 0, 0, 11, 38879, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0, 'Shattered Hand Grunt - Cast Soul Burden on Death');

-- Bloodmage
SET @ENTRY := 19258;
UPDATE `creature_template` SET `AIName`='EventAI' WHERE `entry`=@ENTRY;
DELETE FROM `creature_ai_scripts` WHERE `creature_id`=@ENTRY;
INSERT INTO `creature_ai_scripts` (`id`, `creature_id`, `event_type`, `event_inverse_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action1_type`, `action1_param1`, `action1_param2`, `action1_param3`, `action2_type`, `action2_param1`, `action2_param2`, `action2_param3`, `action3_type`, `action3_param1`, `action3_param2`, `action3_param3`, `comment`) VALUES 
(@ENTRY*100+1, @ENTRY, 0, 0, 100, 1, 0, 0, 3400, 4700, 11, 15530, 1, 64, 0, 0, 0, 0, 0, 0, 0, 0, 'Bloodmage - Cast Frostbolt'),
(@ENTRY*100+2, @ENTRY, 9, 0, 100, 1, 0, 10, 13600, 14500, 11, 12674, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bloodmage - Cast Frost Nova on Close'),
(@ENTRY*100+3, @ENTRY, 0, 0, 100, 1, 8000, 9000, 19000, 24000, 11, 33634, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bloodmage - Cast Blizzard'),
(@ENTRY*100+4, @ENTRY, 0, 0, 100, 1, 10000, 12000, 15000, 22000, 11, 22273, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bloodmage - Cast Cast Arcane Missiles');


-- Arcanist Torseldori
SET @ENTRY := 19257;
UPDATE `creature_template` SET `AIName`='EventAI' WHERE `entry`=@ENTRY;
DELETE FROM `creature_ai_scripts` WHERE `creature_id`=@ENTRY;
INSERT INTO `creature_ai_scripts` (`id`, `creature_id`, `event_type`, `event_inverse_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `action1_type`, `action1_param1`, `action1_param2`, `action1_param3`, `action2_type`, `action2_param1`, `action2_param2`, `action2_param3`, `action3_type`, `action3_param1`, `action3_param2`, `action3_param3`, `comment`) VALUES 
(@ENTRY*100+1, @ENTRY, 0, 0, 100, 1, 0, 0, 3400, 4700, 11, 15530, 1, 64, 0, 0, 0, 0, 0, 0, 0, 0, 'Bloodmage - Cast Frostbolt'),
(@ENTRY*100+2, @ENTRY, 9, 0, 100, 1, 0, 10, 13600, 14500, 11, 12674, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bloodmage - Cast Frost Nova on Close'),
(@ENTRY*100+3, @ENTRY, 0, 0, 100, 1, 8000, 9000, 19000, 24000, 11, 33634, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bloodmage - Cast Blizzard'),
(@ENTRY*100+4, @ENTRY, 0, 0, 100, 1, 10000, 12000, 15000, 22000, 11, 22273, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Bloodmage - Cast Cast Arcane Missiles');
