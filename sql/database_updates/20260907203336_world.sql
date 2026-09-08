-- ==============================================
-- FILE: Blazing_Forge_Fix.sql
-- GENERATED: 20260907203336
-- ==============================================
-- Blazing Forge Kit toy (item 31825 / spell 36600) summons nothing: gameobject_template
-- was missing the rows for the forge and anvil it summons.
INSERT INTO `gameobject_template`
    (`entry`, `type`, `displayId`, `name`, `size`, `data0`, `data1`)
VALUES
    (3000684, 8, 209, 'Blazing Forge Kit: Forge', 0.5, 3, 10),
    (3000685, 8, 273, 'Blazing Forge Kit: Anvil', 0.5, 1, 10)
ON DUPLICATE KEY UPDATE
    `type`      = VALUES(`type`),
    `displayId` = VALUES(`displayId`),
    `name`      = VALUES(`name`),
    `size`      = VALUES(`size`),
    `data0`     = VALUES(`data0`),
    `data1`     = VALUES(`data1`);
