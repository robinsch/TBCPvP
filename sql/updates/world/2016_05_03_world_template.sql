/*Table structure for table `template_item` */

DROP TABLE IF EXISTS `template_item`;

CREATE TABLE `template_item` (
  `templateEntry` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `itemEntry` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `addPriority` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `itemAmount` tinyint(3) unsigned NOT NULL DEFAULT '1',
  `enchantEntry` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `gemEntry1` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `gemEntry2` mediumint(8) unsigned NOT NULL DEFAULT '0',
  `gemEntry3` mediumint(8) unsigned NOT NULL DEFAULT '0'
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

/*Table structure for table `template_spell` */

DROP TABLE IF EXISTS `template_spell`;

CREATE TABLE `template_spell` (
  `templateEntry` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `spellEntry` int(10) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`templateEntry`,`spellEntry`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;
