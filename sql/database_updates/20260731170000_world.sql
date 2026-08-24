-- ==============================================
-- FILE: flurry_proc_events_all_ranks.sql
-- GENERATED: 20260731170000
-- ==============================================
-- Flurry (warrior): the buff never spent its three charges.
--
-- spell_proc_event carried rows for rank 1 only - talent 12319 and its effect
-- 12966 - and nothing for ranks 2 to 5.
--
-- The talent row is the one that matters. It sets procEx = 2,
-- PROC_EX_CRITICAL_HIT, so the talent applies Flurry on a critical strike and
-- nothing else. Without a row the core falls back to PROC_EX_NONE, which the
-- enum documents as "If none can trigger on Hit/Crit only" - that is, on every
-- landed swing. So at ranks 2 to 5 the buff was reapplied by each white hit and
-- its charges went back to three every time. The counter never moved and the
-- haste lasted the full duration, which is exactly what was reported. Rank 1
-- behaved correctly, and nobody takes Flurry at rank 1.
--
-- The effect rows carry procEx = 65536, PROC_EX_EX_TRIGGER_ALWAYS, commented in
-- the core as "used for drop charges": the charge is spent on any swing rather
-- than only on one that lands. Ranks 2 to 5 get it too, so a missed swing costs
-- a charge at every rank the same way.
--
-- procFlags stays 0 throughout, as on the existing rows: the core then uses the
-- flags on the spells themselves, which are already correct
-- (PROC_FLAG_DEAL_MELEE_SWING on the effects, plus the ability flags on the
-- talent).

-- Talent ranks 2-5: apply on a critical strike only, as rank 1 does.
INSERT IGNORE INTO `spell_proc_event`
    (`entry`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`,
     `SpellFamilyMask2`, `procFlags`, `procEx`, `ppmRate`, `CustomChance`, `Cooldown`)
VALUES
    (12971, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0),
    (12972, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0),
    (12973, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0),
    (12974, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0)
ON DUPLICATE KEY UPDATE `procEx` = VALUES(`procEx`);

-- Effect ranks 2-5: spend a charge on any swing, as rank 1 does.
INSERT IGNORE INTO `spell_proc_event`
    (`entry`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`,
     `SpellFamilyMask2`, `procFlags`, `procEx`, `ppmRate`, `CustomChance`, `Cooldown`)
VALUES
    (12967, 0, 0, 0, 0, 0, 0, 65536, 0, 0, 0),
    (12968, 0, 0, 0, 0, 0, 0, 65536, 0, 0, 0),
    (12969, 0, 0, 0, 0, 0, 0, 65536, 0, 0, 0),
    (12970, 0, 0, 0, 0, 0, 0, 65536, 0, 0, 0)
ON DUPLICATE KEY UPDATE `procEx` = VALUES(`procEx`);
