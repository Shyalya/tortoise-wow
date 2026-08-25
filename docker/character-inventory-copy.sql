-- Required by BackupCharacterInventory and `.character diffitems`.
-- The core copies rows with INSERT ... SELECT *, so schema parity is required.
CREATE TABLE IF NOT EXISTS `character_inventory_copy` LIKE `character_inventory`;
