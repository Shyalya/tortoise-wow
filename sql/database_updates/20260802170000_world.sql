-- ==============================================
-- FILE: survival_artisan_rank.sql
-- GENERATED: 20260802170000
-- ==============================================
-- Survival could not pass 225: nobody taught the artisan rank.
--
-- Spell 46057 "Artisan Survivalist" exists, and so does the rank it grants,
-- 46056. Neither npc_trainer nor npc_trainer_template mentioned 46057 anywhere,
-- so the skill capped at 225 for everyone.
--
-- That mattered: 33 items require skill 142, and nine of them ask for more than
-- 225 - Stabilizing Healing Salve at 250, Stonescale Fishing Trap at 275,
-- Outline: Fisherman's Backpack at 285, Outline: Oil-Powered Cooker at 295, and
-- four at 300 including Major Healing Salve. All present in the database and all
-- unreachable.
--
-- Rufus Hardwick (50070) is the trainer meant to hand it out: he is documented
-- as the artisan survival trainer, sits at Nesingwary's Expedition in
-- Stranglethorn Vale, and carries faction 35, friendly to both sides - the only
-- survival trainer that is. The expert rank stays where it is, with the two
-- Expert Survivalists in Stonard and Feathermoon, one per faction.
--
-- The numbers follow the house convention rather than invention. Artisan costs
-- 50000 at skill 200 for every profession here - herbalism, jewelcrafting, the
-- lot. Survival's own level gates run 1, 10, 20 for the first three ranks, and 35
-- is the usual artisan threshold.
--
-- Noted but deliberately not touched: Survival's expert rank costs 250, where the
-- same convention would say 5000. Odd, but it is existing data and cheaper than
-- it should be, which harms nobody.

INSERT IGNORE INTO `npc_trainer` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`)
VALUES (50070, 46057, 50000, 142, 200, 35)
ON DUPLICATE KEY UPDATE
    `spellcost`     = VALUES(`spellcost`),
    `reqskill`      = VALUES(`reqskill`),
    `reqskillvalue` = VALUES(`reqskillvalue`),
    `reqlevel`      = VALUES(`reqlevel`);
