ALTER TABLE `template_item`
ADD COLUMN `addPriority`  mediumint(8) UNSIGNED NOT NULL DEFAULT 0 AFTER `itemEntry`;

ALTER TABLE `npc_vendor`
ADD COLUMN `slot`  tinyint(3) UNSIGNED NOT NULL DEFAULT 0 AFTER `entry`;
