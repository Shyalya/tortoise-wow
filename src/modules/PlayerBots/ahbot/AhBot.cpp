
#include "Category.h"
#include <memory>
#include "ItemBag.h"
#include "ahbot/AhBot.h"
#include "World.h"
#include "Config/Config.h"
#include "Chat/Chat.h"
#include "AhBotConfig.h"
#include "AuctionHouse/AuctionHouseMgr.h"
#include "WorldSession.h"
#include "Objects/Player.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "AccountMgr.h"
#include "playerbot/playerbot.h"
#include "Mail/Mail.h"
#include "Util.h"

#ifdef CMANGOS
#include <boost/thread/thread.hpp>
#endif

using namespace ahbot;

bool AhBot::HandleAhBotCommand(ChatHandler* handler, char const* args)
{
    auctionbot.HandleCommand(args);
    return true;
}

uint32 AhBot::auctionIds[MAX_AUCTIONS] = {1,6,7};
uint32 AhBot::auctioneers[MAX_AUCTIONS] = {79707,4656,23442};
std::map<uint32, uint32> AhBot::factions;

void AhBot::Init()
{
    sLog.outString("[AhBot] Initializing AhBot by ike3");

    if (!sAhBotConfig.Initialize())
    {
        sLog.outString("[AhBot] Disabled or failed to load ahbot.conf — AhBot will not run");
        return;
    }

    sLog.outString("[AhBot] Config: GUID=%llu, updateInterval=%ds, maxItemLevel=%d, maxRequiredLevel=%d, priceMultiplier=%.2f",
        (unsigned long long)sAhBotConfig.guid,
        sAhBotConfig.updateInterval,
        sAhBotConfig.maxItemLevel,
        sAhBotConfig.maxRequiredLevel,
        sAhBotConfig.priceMultiplier);

    factions[1] = 1;
    factions[2] = 1;
    factions[3] = 1;
    factions[4] = 2;
    factions[5] = 2;
    factions[6] = 2;
    factions[7] = 3;

    availableItems.Init();

    sLog.outString("[AhBot] Initialization complete. First auction check in %d seconds.", sAhBotConfig.updateInterval);
}

AhBot::~AhBot()
{
}

ObjectGuid AhBot::GetAHBplayerGUID()
{
    return ObjectGuid(sAhBotConfig.guid);
}

#ifdef MANGOS
class AhbotThread: public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { auctionbot.ForceUpdate(); return 0; }
};
#endif
#ifdef CMANGOS
void AhbotThread()
{
    auctionbot.ForceUpdate();
}
#endif

void activateAhbotThread()
{
#ifdef MANGOS
    AhbotThread *thread = new AhbotThread();
    thread->activate();
#endif
#ifdef CMANGOS
    boost::thread t(AhbotThread);
    t.detach();
#endif
}

void AhBot::Update()
{
    if (sWorld.IsShutdowning())
        return;

    if (sWorld.IsStopped())
        return;

    // Carry out whatever the bot thread decided on last pass. This has to come
    // before the nextAICheckTime early-out below, or queued work would only run
    // once every check interval instead of on the next tick.
    RunQueuedWork();

    time_t now = time(0);

    if (now < nextAICheckTime)
        return;

    if (updating)
    {
        sLog.outString("[AhBot] Update skipped — previous check still running");
        return;
    }

    sLog.outString("[AhBot] Scheduling auction check (next after this: in %d seconds)", sAhBotConfig.updateInterval);
    nextAICheckTime = time(0) + sAhBotConfig.updateInterval;
    activateAhbotThread();
    CleanupPropositions();
}

void AhBot::ForceUpdate()
{
	if (!sAhBotConfig.enabled)
	{
		sLog.outString("[AhBot] ForceUpdate called but AhBot is disabled in ahbot.conf");
		return;
	}

	bool expected = false;
	if (!updating.compare_exchange_strong(expected, true))
	{
		sLog.outString("[AhBot] ForceUpdate called but previous check is still running — skipping");
		return;
	}

	sLog.outString("[AhBot] === Auction check starting ===");

	if (!allBidders.size())
	{
		sLog.outString("[AhBot] No bidders loaded yet — calling LoadRandomBots");
		LoadRandomBots();
	}

	if (!allBidders.size())
	{
		sLog.outError("[AhBot] No bidders available — cannot post or answer auctions. Check that AhBot.GUID is set to a valid character GUID in ahbot.conf.");
		updating = false;
		return;
	}

	sLog.outString("[AhBot] Bidders loaded: %zu total (A=%zu H=%zu N=%zu)",
		allBidders.size(), bidders[1].size(), bidders[2].size(), bidders[3].size());

	CheckCategoryMultipliers();

	int answered = 0, added = 0;
	for (int i = 0; i < MAX_AUCTIONS; i++)
	{
		sLog.outString("[AhBot] --- Checking auction house id=%u ---", auctionIds[i]);
		InAuctionItemsBag inAuctionItems(auctionIds[i]);
		inAuctionItems.Init(true);

		int ahAnswered = 0, ahAdded = 0;
		for (int j = 0; j < CategoryList::instance.size(); j++)
		{
			Category* category = CategoryList::instance[j];
			ahAnswered += Answer(i, category, &inAuctionItems);
			ahAdded += AddAuctions(i, category, &inAuctionItems);
		}

		sLog.outString("[AhBot] Auction house id=%u: answered=%d added=%d", auctionIds[i], ahAnswered, ahAdded);
		answered += ahAnswered;
		added += ahAdded;
	}

	CleanupHistory();

	sLog.outString("[AhBot] === Check complete: %d answered, %d added. Next check in %d seconds ===",
		answered, added, sAhBotConfig.updateInterval);
    updating = false;
}

struct SortByPricePredicate
{
    bool operator()(AuctionSnapshot const & a, AuctionSnapshot const & b) const
    {
        if (a.startbid == b.startbid)
            return a.buyout < b.buyout;

        return a.startbid < b.startbid;
    }
};

std::vector<AuctionSnapshot> AhBot::LoadAuctions(const std::vector<AuctionSnapshot>& auctionEntryMap,
        Category*& category, int& auction)
{
    std::vector<AuctionSnapshot> entries;
    for (std::vector<AuctionSnapshot>::const_iterator itr = auctionEntryMap.begin();
            itr != auctionEntryMap.end(); ++itr)
    {
        const AuctionSnapshot& entry = *itr;
        if (IsBotAuction(entry.owner) || IsBotAuction(entry.bidder))
            continue;

        Item *item = sAuctionMgr.GetAItem(entry.itemGuidLow);
        if (!item)
            continue;

        if (!category->Contains(item->GetProto()))
            continue;

        uint32 price = category->GetPricingStrategy()->GetBuyPrice(item->GetProto(), auctionIds[auction]);
        if (!price || !item->GetCount())
        {
            sLog.outDetail("%s (x%d) in auction %d: price cannot be determined",
                    item->GetProto()->Name1.c_str(), item->GetCount(), auctionIds[auction]);
            continue;
        }

        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), SortByPricePredicate());
    return entries;
}

void AhBot::FindMinPrice(const std::vector<AuctionSnapshot>& auctionEntryMap, const AuctionSnapshot& entry, Item*& item, uint32* minBid,
        uint32* minBuyout)
{
    *minBid = 0;
    *minBuyout = 0;
    for (std::vector<AuctionSnapshot>::const_iterator itr = auctionEntryMap.begin();
            itr != auctionEntryMap.end(); ++itr)
    {
        const AuctionSnapshot& other = *itr;
        if (other.owner == entry.owner)
            continue;

        Item *otherItem = sAuctionMgr.GetAItem(other.itemGuidLow);
        if (!otherItem || !otherItem->GetCount() || !otherItem->GetProto() || otherItem->GetProto()->ItemId != item->GetProto()->ItemId)
            continue;

        uint32 startbid = other.startbid / otherItem->GetCount() * item->GetCount();
        uint32 bid = other.bid / otherItem->GetCount() * item->GetCount();
        uint32 buyout = other.buyout / otherItem->GetCount() * item->GetCount();

        if (!bid && startbid && (!*minBid || *minBid > startbid))
            *minBid = startbid;

        if (bid && (*minBid || *minBid > bid))
            *minBid = bid;

        if (buyout && (!*minBuyout || *minBuyout > buyout))
            *minBuyout = buyout;
    }
}

static int8 InventoryTypeToEquipSlot(uint32 invType)
{
    switch (invType)
    {
        case INVTYPE_HEAD:           return EQUIPMENT_SLOT_HEAD;
        case INVTYPE_NECK:           return EQUIPMENT_SLOT_NECK;
        case INVTYPE_SHOULDERS:      return EQUIPMENT_SLOT_SHOULDERS;
        case INVTYPE_CHEST:
        case INVTYPE_ROBE:           return EQUIPMENT_SLOT_CHEST;
        case INVTYPE_WAIST:          return EQUIPMENT_SLOT_WAIST;
        case INVTYPE_LEGS:           return EQUIPMENT_SLOT_LEGS;
        case INVTYPE_FEET:           return EQUIPMENT_SLOT_FEET;
        case INVTYPE_WRISTS:         return EQUIPMENT_SLOT_WRISTS;
        case INVTYPE_HANDS:          return EQUIPMENT_SLOT_HANDS;
        case INVTYPE_FINGER:         return EQUIPMENT_SLOT_FINGER1;
        case INVTYPE_TRINKET:        return EQUIPMENT_SLOT_TRINKET1;
        case INVTYPE_CLOAK:          return EQUIPMENT_SLOT_BACK;
        case INVTYPE_WEAPON:
        case INVTYPE_2HWEAPON:
        case INVTYPE_WEAPONMAINHAND: return EQUIPMENT_SLOT_MAINHAND;
        case INVTYPE_SHIELD:
        case INVTYPE_WEAPONOFFHAND:
        case INVTYPE_HOLDABLE:       return EQUIPMENT_SLOT_OFFHAND;
        case INVTYPE_RANGED:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_THROWN:         return EQUIPMENT_SLOT_RANGED;
        default:                     return -1;
    }
}

static uint32 GetEquippedItemLevel(uint32 botGuid, uint8 slot, uint32& outGuid)
{
    outGuid = 0;
    auto result = CharacterDatabase.PQuery(
        "SELECT ci.item, ii.itemEntry FROM character_inventory ci "
        "JOIN item_instance ii ON ci.item = ii.guid "
        "WHERE ci.guid = '%u' AND ci.bag = 0 AND ci.slot = '%u'",
        botGuid, slot);
    if (!result)
        return 0;

    Field* fields = result->Fetch();
    outGuid = fields[0].GetUInt32();
    uint32 itemEntry = fields[1].GetUInt32();
    delete result;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemEntry);
    return proto ? proto->ItemLevel : 0;
}

bool AhBot::TryEquipItem(uint32 bidder, uint32 itemGuidLow, ItemPrototype const* proto)
{
    int8 primarySlot = InventoryTypeToEquipSlot(proto->InventoryType);
    if (primarySlot < 0)
        return false;

    auto charResult = CharacterDatabase.PQuery(
        "SELECT race, class, level FROM characters WHERE guid = '%u'", bidder);
    if (!charResult)
        return false;

    Field* charFields = charResult->Fetch();
    uint32 race  = charFields[0].GetUInt32();
    uint32 cls   = charFields[1].GetUInt32();
    uint32 level = charFields[2].GetUInt32();
    delete charResult;

    if (proto->RequiredLevel && level < proto->RequiredLevel)
        return false;
    if (proto->AllowableClass && !(proto->AllowableClass & (1 << (cls - 1))))
        return false;
    if (proto->AllowableRace && !(proto->AllowableRace & (1 << (race - 1))))
        return false;

    // For rings and trinkets pick the slot with the lower-level current item
    bool isDualSlot = (proto->InventoryType == INVTYPE_FINGER || proto->InventoryType == INVTYPE_TRINKET);
    uint8 slot = (uint8)primarySlot;
    uint32 currentGuid = 0;
    uint32 currentLevel = GetEquippedItemLevel(bidder, slot, currentGuid);

    if (isDualSlot)
    {
        uint8 altSlot = (proto->InventoryType == INVTYPE_FINGER) ? EQUIPMENT_SLOT_FINGER2 : EQUIPMENT_SLOT_TRINKET2;
        uint32 altGuid = 0;
        uint32 altLevel = GetEquippedItemLevel(bidder, altSlot, altGuid);
        if (altLevel < currentLevel)
        {
            slot = altSlot;
            currentGuid = altGuid;
            currentLevel = altLevel;
        }
    }

    if (proto->ItemLevel <= currentLevel)
        return false;

    sLog.outString("[AhBot] Equipping upgrade on bot guid=%u slot=%u: %s ilvl=%u (was ilvl=%u)",
            bidder, slot, proto->Name1.c_str(), proto->ItemLevel, currentLevel);

    CharacterDatabase.BeginTransaction();
    // Delete all item_instances for everything currently in this slot, then clear the slot.
    // A slot may have multiple rows if inventory was previously corrupted; clean them all.
    CharacterDatabase.PExecute(
        "DELETE ii FROM item_instance ii "
        "JOIN character_inventory ci ON ci.item = ii.guid "
        "WHERE ci.guid = '%u' AND ci.bag = 0 AND ci.slot = '%u'",
        bidder, slot);
    CharacterDatabase.PExecute("DELETE FROM character_inventory WHERE guid='%u' AND bag=0 AND slot='%u'", bidder, slot);
    CharacterDatabase.PExecute("UPDATE item_instance SET owner_guid='%u' WHERE guid='%u'", bidder, itemGuidLow);
    CharacterDatabase.PExecute("INSERT INTO character_inventory (guid, bag, slot, item, item_template) VALUES ('%u', 0, '%u', '%u', '%u')",
            bidder, slot, itemGuidLow, proto->ItemId);
    CharacterDatabase.CommitTransaction();

    return true;
}

int AhBot::Answer(int auction, Category* category, ItemBag* inAuctionItems)
{
    const AuctionHouseEntry* ahEntry = sAuctionHouseStore.LookupEntry(auctionIds[auction]);
    if (!ahEntry)
        return 0;

    int answered = 0;
    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);
    std::vector<AuctionSnapshot> auctionEntryMap = auctionHouse->GetAuctionsSnapshot();
    int64 availableMoney = GetAvailableMoney(auctionIds[auction]);

    std::vector<AuctionSnapshot> entries = LoadAuctions(auctionEntryMap, category, auction);
    sLog.outDetail("[AhBot] Answer AH %u category %s: scanning %zu entries, money=%ld",
            auctionIds[auction], category->GetName().c_str(), entries.size(), availableMoney);

    for (std::vector<AuctionSnapshot>::const_iterator itr = entries.begin(); itr != entries.end(); ++itr)
    {
        const AuctionSnapshot& snap = *itr;
        uint32 owner = snap.owner;
        if (owner == sAhBotConfig.guid)
            continue;

        uint32 account = sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(HIGHGUID_PLAYER, owner));
        if (!account)
        {
            sLog.outDetail("[AhBot] Skipping entry %u (owner guid=%u): account lookup failed — owner not in DB?",
                    snap.Id, owner);
            continue;
        }
        if (sPlayerbotAIConfig.IsInRandomAccountList(account))
        {
            sLog.outDetail("[AhBot] Skipping entry %u (owner guid=%u account=%u): owner is a bot account",
                    snap.Id, owner, account);
            continue;
        }

        Item *item = sAuctionMgr.GetAItem(snap.itemGuidLow);
        if (!item || !item->GetCount())
        {
            sLog.outString("[AhBot] Skipping entry %u from real player (guid=%u account=%u): item not found in aitem map",
                    snap.Id, owner, account);
            continue;
        }

        const ItemPrototype* proto = item->GetProto();
        sLog.outString("[AhBot] Evaluating %s (x%d) entry=%u from real player (guid=%u account=%u) AH=%u startbid=%u buyout=%u",
                proto->Name1.c_str(), item->GetCount(), snap.Id, owner, account, auctionIds[auction],
                snap.startbid, snap.buyout);

        std::vector<uint32> items = availableItems.Get(category);
        if (std::find(items.begin(), items.end(), proto->ItemId) == items.end())
        {
            sLog.outString("[AhBot] SKIP %s (x%d): not in bot's available item pool for category %s",
                    proto->Name1.c_str(), item->GetCount(), category->GetName().c_str());
            continue;
        }

        uint32 answerCount = GetAnswerCount(proto->ItemId, auctionIds[auction], sAhBotConfig.itemBuyMaxInterval);
        uint32 maxAnswerCount = category->GetMaxAllowedItemAuctionCount(proto);
        if (maxAnswerCount && answerCount > maxAnswerCount)
        {
            sLog.outString("[AhBot] SKIP %s (x%d): already answered %u times (max=%u) within interval",
                    proto->Name1.c_str(), item->GetCount(), answerCount, maxAnswerCount);
            continue;
        }

        if (proto->RequiredLevel > sAhBotConfig.maxRequiredLevel || proto->ItemLevel > sAhBotConfig.maxItemLevel)
        {
            sLog.outString("[AhBot] SKIP %s (x%d): reqLevel=%u itemLevel=%u exceeds max (reqLevel<=%u itemLevel<=%u)",
                    proto->Name1.c_str(), item->GetCount(),
                    proto->RequiredLevel, proto->ItemLevel,
                    sAhBotConfig.maxRequiredLevel, sAhBotConfig.maxItemLevel);
            continue;
        }

        std::ostringstream priceExplain;
        uint32 price = category->GetPricingStrategy()->GetBuyPrice(proto, auctionIds[auction], &priceExplain);
        if (!price)
        {
            sLog.outString("[AhBot] SKIP %s (x%d): buy price is 0 (%s)",
                    proto->Name1.c_str(), item->GetCount(), priceExplain.str().c_str());
            continue;
        }

        uint32 bidPrice = item->GetCount() * price;
        uint32 buyoutPrice = item->GetCount() * urand(price, 4 * price / 3);

        uint32 curPrice = snap.bid;
        if (!curPrice) curPrice = snap.startbid;
        if (!curPrice) curPrice = snap.buyout;

        uint32 bidder = GetRandomBidder(auctionIds[auction]);
        if (!bidder)
        {
            sLog.outError("[AhBot] No bidders for auction %d", auctionIds[auction]);
            break;
        }

        if (curPrice > buyoutPrice)
        {
            sLog.outString("[AhBot] SKIP %s (x%d): listing price %u > bot max price %u (price/unit=%u)",
                    proto->Name1.c_str(), item->GetCount(), curPrice, buyoutPrice, price);
            CheckSendMail(bidder, buyoutPrice, snap);
            continue;
        }

        if (availableMoney < (int64)curPrice)
        {
            sLog.outString("[AhBot] SKIP %s (x%d): listing price %u > available money %ld",
                    proto->Name1.c_str(), item->GetCount(), curPrice, availableMoney);
            continue;
        }

        uint32 minBid = 0, minBuyout = 0;
        FindMinPrice(auctionEntryMap, snap, item, &minBid, &minBuyout);

        if (minBid && snap.bid && minBid < snap.bid)
        {
            sLog.outString("[AhBot] SKIP %s (x%d): current bid %u > cheaper listing %u (minBid)",
                    proto->Name1.c_str(), item->GetCount(), snap.bid, minBid);
            continue;
        }

        if (minBid && snap.startbid && minBid < snap.startbid)
        {
            sLog.outString("[AhBot] SKIP %s (x%d): startbid %u > cheaper listing %u (minBid)",
                    proto->Name1.c_str(), item->GetCount(), snap.startbid, minBid);
            CheckSendMail(bidder, minBid, snap);
            continue;
        }

        double priceLevel = (double)curPrice / (double)buyoutPrice;
        uint32 buytime = GetBuyTime(snap.Id, proto->ItemId, auctionIds[auction], category, priceLevel);
        if (time(0) < buytime)
        {
            sLog.outString("[AhBot] SKIP %s (x%d): buy delay not expired, will act in %ld seconds",
                    proto->Name1.c_str(), item->GetCount(), (long)(buytime - time(0)));
            continue;
        }

        // Do not complete the purchase here. This runs on the bot thread, and
        // paying the seller reaches into mail, their session and possibly their
        // live Player object. Record the decision; the world thread executes it
        // from AhBot::Update().
        PendingPurchase pending;
        pending.auctionId   = snap.Id;
        pending.bidder      = bidder;
        pending.bidAmount   = curPrice + urand(1, 1 + bidPrice / 10);
        pending.unitPrice   = price;
        pending.minBuyout   = minBuyout;
        pending.houseIndex  = auction;
        {
            std::lock_guard<std::mutex> g(queuedWorkMutex);
            queuedPurchases.push_back(pending);
        }

        availableMoney -= curPrice;

        sLog.outString("[AhBot] Queued buy: %dx %s on AH %u for %u (bidder guid=%u)",
                item->GetCount(), proto->Name1.c_str(), auctionIds[auction], pending.bidAmount, bidder);

        answered++;
    }

    return answered;
}

uint32 AhBot::GetTime(std::string category, uint32 id, uint32 auctionHouse, uint32 type)
{
    auto results = CharacterDatabase.PQuery("SELECT MAX(buytime) FROM ahbot_history WHERE item = '%u' AND won = '%u' AND auction_house = '%u' AND category = '%s'",
        id, type, factions[auctionHouse], category.c_str());
    std::unique_ptr<QueryResult> results_guard(results);

    if (!results)
        return 0;

    Field* fields = results->Fetch();
    uint32 result = fields[0].GetUInt32();

    return result;
}

void AhBot::SetTime(std::string category, uint32 id, uint32 auctionHouse, uint32 type, uint32 value)
{
    CharacterDatabase.PExecute("DELETE FROM ahbot_history WHERE item = '%u' AND won = '%u' AND auction_house = '%u' AND category = '%s'",
        id, type, factions[auctionHouse], category.c_str());

    CharacterDatabase.PExecute("INSERT INTO ahbot_history (buytime, item, bid, buyout, category, won, auction_house) "
        "VALUES ('%u', '%u', '%u', '%u', '%s', '%u', '%u')",
        value, id, 0, 0,
        category.c_str(), type, factions[auctionHouse]);
}

uint32 AhBot::GetBuyTime(uint32 entry, uint32 itemId, uint32 auctionHouse, Category*& category, double priceLevel)
{
    uint32 entryTime = GetTime("entry", entry, auctionHouse, AHBOT_WON_DELAY);
    if (entryTime > time(0))
        return entryTime;

    uint32 result = entryTime;

    std::string categoryName = category->GetName();
    uint32 categoryTime = GetTime(categoryName, 0, auctionHouse, AHBOT_WON_DELAY);
    uint32 itemTime = GetTime("item", itemId, auctionHouse, AHBOT_WON_DELAY);

    if (categoryTime < time(0)) categoryTime = time(0);
    if (itemTime < time(0)) itemTime = time(0);

    double rarity = category->GetPricingStrategy()->GetRarityPriceMultiplier(itemId);
    categoryTime += urand(sAhBotConfig.itemBuyMinInterval, sAhBotConfig.itemBuyMaxInterval) * priceLevel;
    itemTime += urand(sAhBotConfig.itemBuyMinInterval, sAhBotConfig.itemBuyMaxInterval) * priceLevel / rarity;
    entryTime = std::max(categoryTime, itemTime);

    SetTime(categoryName, 0, auctionHouse, AHBOT_WON_DELAY, categoryTime);
    SetTime("item", itemId, auctionHouse, AHBOT_WON_DELAY, itemTime);
    SetTime("entry", entry, auctionHouse, AHBOT_WON_DELAY, entryTime);

    return result ? result : entryTime;
}

uint32 AhBot::GetSellTime(uint32 itemId, uint32 auctionHouse, Category*& category)
{
    uint32 itemSellTime = GetTime("item", itemId, auctionHouse, AHBOT_SELL_DELAY);
    uint32 itemBuyTime = GetTime("item", itemId, auctionHouse, AHBOT_WON_DELAY);
    uint32 itemTime = std::max(itemSellTime, itemBuyTime);

    if (itemTime > time(0))
        return itemTime;

    uint32 result = itemTime;

    std::string categoryName = category->GetDisplayName();
    uint32 categorySellTime = GetTime(categoryName, 0, auctionHouse, AHBOT_SELL_DELAY);
    uint32 categoryBuyTime = GetTime(categoryName, 0, auctionHouse, AHBOT_WON_DELAY);
    uint32 categoryTime = std::max(categorySellTime, categoryBuyTime);

    if (categoryTime < time(0)) categoryTime = time(0);
    if (itemTime < time(0)) itemTime = time(0);

    double rarity = category->GetPricingStrategy()->GetRarityPriceMultiplier(itemId);
    categoryTime += urand(sAhBotConfig.itemSellMinInterval, sAhBotConfig.itemSellMaxInterval);
    itemTime += urand(sAhBotConfig.itemSellMinInterval, sAhBotConfig.itemSellMaxInterval) * rarity;
    itemTime = std::max(itemTime, categoryTime);

    SetTime(categoryName, 0, auctionHouse, AHBOT_SELL_DELAY, categoryTime);
    SetTime("item", itemId, auctionHouse, AHBOT_SELL_DELAY, itemTime);

    return result ? result : itemTime;
}

int AhBot::AddAuctions(int auction, Category* category, ItemBag* inAuctionItems)
{
    std::vector<uint32>& inAuction = inAuctionItems->Get(category);

    int32 maxAllowedAuctionCount = categoryMaxAuctionCount[category->GetDisplayName()];
    if (inAuctionItems->GetCount(category) >= maxAllowedAuctionCount)
    {
        sLog.outDetail("[AhBot] Category '%s' on AH %u: at cap (%d/%d), skipping",
            category->GetDisplayName().c_str(), auctionIds[auction],
            inAuctionItems->GetCount(category), maxAllowedAuctionCount);
        return 0;
    }

    int added = 0;
    int ladded = 0;
    std::vector<uint32> available = availableItems.Get(category);
    for (int32 i = 0; i <= maxAllowedAuctionCount && available.size() > 0 && inAuctionItems->GetCount(category) < maxAllowedAuctionCount; ++i)
    {
        uint32 index = urand(0, available.size() - 1);
        uint32 itemId = available[index];

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto)
            continue;

        int32 maxAllowedItems = category->GetMaxAllowedItemAuctionCount(proto);
        if (maxAllowedItems && inAuctionItems->GetCount(category, proto->ItemId) >= maxAllowedItems)
        {
            sLog.outDetail("%s in auction %d: has reached max %d/%d",
                proto->Name1.c_str(), auctionIds[auction], inAuctionItems->GetCount(category, proto->ItemId), maxAllowedItems);
            continue;
        }

        uint32 sellTime = GetSellTime(proto->ItemId, auctionIds[auction], category);
        if (time(0) - sellTime < 0)
        {
            ladded += 1;
            sLog.outDetail( "%s in auction %d: will add in %ld seconds",
                    proto->Name1.c_str(), auctionIds[auction], sellTime - time(0));
            continue;
        }
        else if (time(0) - sellTime > sAhBotConfig.maxSellInterval)
        {
            sLog.outDetail( "%s in auction %d: too old (%ld secs)",
                    proto->Name1.c_str(), auctionIds[auction], time(0) - sellTime);
            continue;
        }
        inAuctionItems->Add(proto);
        added += AddAuction(auction, category, proto);
    }

    if (added > 0 || ladded > 0)
        sLog.outString("[AhBot] Category '%s' on AH %u: %d new listing(s), %d pending (sell delay not elapsed)",
            category->GetDisplayName().c_str(), auctionIds[auction], added, ladded);


    return added;
}

int AhBot::AddAuction(int auction, Category* category, ItemPrototype const* proto)
{
    uint32 owner = GetRandomBidder(auctionIds[auction]);
    if (!owner)
    {
        sLog.outError("[AhBot] No valid bidder found for auction house %u — cannot list %s", auctionIds[auction], proto->Name1.c_str());
        return 0;
    }

    std::string name;
    if (!sObjectMgr.GetPlayerNameByGUID(ObjectGuid(HIGHGUID_PLAYER, owner), name))
    {
        sLog.outError("[AhBot] Owner GUID %u has no character record — cannot list %s", owner, proto->Name1.c_str());
        return 0;
    }

    uint32 price = category->GetPricingStrategy()->GetSellPrice(proto, auctionIds[auction]);

    updateMarketPrice(proto->ItemId, price, auctionIds[auction]);

    price = category->GetPricingStrategy()->GetSellPrice(proto, auctionIds[auction]);

    uint32 stackCount = urand(1, category->GetStackCount(proto));
    if (!price || !stackCount)
        return 0;

    if (price > sAhBotConfig.stackReducePrice)
        stackCount /= (price / sAhBotConfig.stackReducePrice);

    if (!stackCount)
        stackCount = 1;

    if (urand(0, 100) <= sAhBotConfig.underPriceProbability * 100)
        price = price * 100 / urand(100, 200);

    uint32 bidPrice = PricingStrategy::RoundPrice(stackCount * price);
    uint32 buyoutPrice = PricingStrategy::RoundPrice(stackCount * urand(price, 4 * price / 3));

    Item* item = Item::CreateItem(proto->ItemId, stackCount);
    if (!item)
        return 0;

    uint32 randomPropertyId = Item::GenerateItemRandomPropertyId(proto->ItemId);
    if (randomPropertyId)
        item->SetItemRandomProperties(randomPropertyId);
    item->ClearUpdateMask(false);

    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(auctionIds[auction]);
    if (!ahEntry)
        return 0;

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);

    uint32 auction_time = uint32(urand(8, 24) * HOUR * sWorld.getConfig(CONFIG_FLOAT_RATE_AUCTION_TIME));

    AuctionEntry* auctionEntry = new AuctionEntry;
    auctionEntry->Id = sObjectMgr.GenerateAuctionID();
    auctionEntry->itemGuidLow = item->GetObjectGuid().GetCounter();
    auctionEntry->itemTemplate = item->GetEntry();
    auctionEntry->itemCount = item->GetCount();
    auctionEntry->itemRandomPropertyId = item->GetItemRandomPropertyId();
    auctionEntry->owner = owner;
    auctionEntry->startbid = bidPrice;
    auctionEntry->bidder = 0;
    auctionEntry->bid = 0;
    auctionEntry->buyout = buyoutPrice;
    auctionEntry->expireTime = time(nullptr) + auction_time;
    //auctionEntry->moneyDeliveryTime = 0;
    auctionEntry->deposit = 0;
    auctionEntry->auctionHouseEntry = ahEntry;

    auctionHouse->AddAuction(auctionEntry);


    sAuctionMgr.AddAItem(item);

    item->SaveToDB();
    auctionEntry->SaveToDB();

    sLog.outString("[AhBot] Listed: %dx %s on AH %u for %ug%us..%ug%us (owner: %s guid=%u)",
        stackCount, proto->Name1.c_str(), auctionIds[auction],
        bidPrice / 10000, (bidPrice % 10000) / 100,
        buyoutPrice / 10000, (buyoutPrice % 10000) / 100,
        name.c_str(), owner);
    return 1;
}

void AhBot::HandleCommand(std::string command)
{
    if (!sAhBotConfig.enabled)
        return;

    if (command == "expire")
    {
        for (int i = 0; i < MAX_AUCTIONS; i++)
            Expire(i);
        CharacterDatabase.PExecute("DELETE FROM ahbot_category");
        CharacterDatabase.PExecute("UPDATE ahbot_history SET buytime = buytime - 3600 * 24;");

        return;
    }

    if (command == "stats")
    {
        for (int i = 0; i < MAX_AUCTIONS; i++)
            PrintStats(i);

        return;
    }

    if (command == "update")
    {
        activateAhbotThread();
        return;
    }

    if (command == "dump")
    {
        Dump();
        return;
    }

    uint32 itemId = atoi(command.c_str());
    if (!itemId)
    {
        sLog.outString("ahbot stats - show short summary");
        sLog.outString("ahbot expire - expire all auctions");
        sLog.outString("ahbot update - update all auctions");
        sLog.outString("ahbot <itemId> - show item price");
        return;
    }

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return;

    for (int i=0; i<CategoryList::instance.size(); i++)
    {
        Category* category = CategoryList::instance[i];
        if (category->Contains(proto))
        {
            std::vector<uint32> items = availableItems.Get(category);
            if (std::find(items.begin(), items.end(), proto->ItemId) == items.end())
                continue;

            std::ostringstream out;
            out << proto->Name1.c_str() << " (" << category->GetDisplayName() << "), "
                    << category->GetMaxAllowedAuctionCount() << "x" << category->GetMaxAllowedItemAuctionCount(proto)
                    << "x" << category->GetStackCount(proto) << " max"
                    << "\n";
            for (int auction = 0; auction < MAX_AUCTIONS; auction++)
            {
                const AuctionHouseEntry* ahEntry = sAuctionHouseStore.LookupEntry(auctionIds[auction]);
                out << "--- auction house " << auctionIds[auction] << "(faction: " << factions[auctionIds[auction]] << ", money: "
                    << GetAvailableMoney(auctionIds[auction])
                    << ") ---\n";

                std::ostringstream exp1;
                out << "sell: " << ChatHelper::formatMoney(category->GetPricingStrategy()->GetSellPrice(proto, auctionIds[auction], true, &exp1));
                out << " ("  << exp1.str().c_str() << ")\n";

                std::ostringstream exp2;
                out << "buy: " << ChatHelper::formatMoney(category->GetPricingStrategy()->GetBuyPrice(proto, auctionIds[auction], &exp2));
                out << " ("  << exp2.str().c_str() << ")\n";

                out << "market: " << ChatHelper::formatMoney(category->GetPricingStrategy()->GetMarketPrice(proto->ItemId, auctionIds[auction]))
                    << "\n";
            }
            sLog.outString("%s",out.str().c_str());
        }
    }
}

void AhBot::Expire(int auction)
{
    if (!sAhBotConfig.enabled)
        return;

    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(auctionIds[auction]);
    if(!ahEntry)
        return;

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);

    // This one writes expireTime, so it needs the live entries rather than a
    // snapshot. Each iteration is a cheap set lookup, so just hold the lock
    // across the whole loop.
    int count = 0;
    {
        AuctionHouseObject::Guard g(auctionHouse->GetLock());
        AuctionHouseObject::AuctionEntryMapBounds bounds = auctionHouse->GetAuctionsBounds_locked();
        for (AuctionHouseObject::AuctionEntryMap::iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            if (IsBotAuction(itr->second->owner))
            {
                itr->second->expireTime = sWorld.GetGameTime();
                count++;
            }
        }
    }

    sLog.outString("%d auctions marked as expired in auction %d", count, auctionIds[auction]);
}

void AhBot::PrintStats(int auction)
{
    if (!sAhBotConfig.enabled)
        return;

    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(auctionIds[auction]);
    if(!ahEntry)
        return;

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);

    sLog.outString("%u auctions available on auction house %d", auctionHouse->GetCount(), auctionIds[auction]);
}

void AhBot::AddToHistory(AuctionEntry* entry, uint32 won)
{
    if (!sAhBotConfig.enabled || !entry)
        return;

    if (!IsBotAuction(entry->owner) && !IsBotAuction(entry->bidder))
        return;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry->itemTemplate);
    if (!proto)
        return;

    std::string category = "";
    for (int i = 0; i < CategoryList::instance.size(); i++)
    {
        if (CategoryList::instance[i]->Contains(proto))
        {
            category = CategoryList::instance[i]->GetName();
            break;
        }
    }

    if (!won)
    {
        won = AHBOT_WON_PLAYER;
        if (IsBotAuction(entry->bidder))
            won = AHBOT_WON_SELF;
    }

    sLog.outDetail( "AddToHistory: market price adjust");
    int count = entry->itemCount ? entry->itemCount : 1;
    updateMarketPrice(proto->ItemId, entry->buyout / count, entry->auctionHouseEntry->houseId);

    uint32 now = time(0);
    CharacterDatabase.PExecute("INSERT INTO ahbot_history (buytime, item, bid, buyout, category, won, auction_house) "
        "VALUES ('%u', '%u', '%u', '%u', '%s', '%u', '%u')",
        now, entry->itemTemplate, entry->bid ? entry->bid : entry->startbid, entry->buyout,
        category.c_str(), won, factions[entry->auctionHouseEntry->houseId]);
}

uint32 AhBot::GetAnswerCount(uint32 itemId, uint32 auctionHouse, uint32 withinTime)
{
    uint32 count = 0;

    auto results = CharacterDatabase.PQuery("SELECT COUNT(*) FROM ahbot_history WHERE "
        "item = '%u' AND won in (2, 3) AND auction_house = '%u' AND buytime > '%lu'",
        itemId, factions[auctionHouse], time(0) - withinTime);
    std::unique_ptr<QueryResult> results_guard(results);
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            count = fields[0].GetUInt32();
        } while (results->NextRow());
    }

    return count;
}

void AhBot::CleanupHistory()
{
    uint32 when = time(0) - 3600 * 24 * sAhBotConfig.historyDays;
    CharacterDatabase.PExecute("DELETE FROM ahbot_history WHERE buytime < '%u'", when);
}

uint32 AhBot::GetAvailableMoney(uint32 auctionHouse)
{
    int64 result = sAhBotConfig.alwaysAvailableMoney;

    std::map<uint32, uint32> data;
    data[AHBOT_WON_PLAYER] = 0;
    data[AHBOT_WON_SELF] = 0;

    const AuctionHouseEntry* ahEntry = sAuctionHouseStore.LookupEntry(auctionHouse);
    auto results = CharacterDatabase.PQuery(
        "SELECT won, SUM(bid) FROM ahbot_history WHERE auction_house = '%u' GROUP BY won HAVING won > 0 ORDER BY won",
        factions[auctionHouse]);
    std::unique_ptr<QueryResult> results_guard(results);
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            data[fields[0].GetUInt32()] = fields[1].GetUInt32();

        } while (results->NextRow());
    }

    results = CharacterDatabase.PQuery(
        "SELECT max(buytime) FROM ahbot_history WHERE auction_house = '%u' AND won = '2'",
        factions[auctionHouse]);
    results_guard.reset(results);
    if (results)
    {
        Field* fields = results->Fetch();
        uint32 lastBuyTime = fields[0].GetUInt32();
        uint32 now = time(0);
        if (lastBuyTime && now > lastBuyTime)
        result += (now - lastBuyTime) / 3600 / 24 * sAhBotConfig.alwaysAvailableMoney;
    }

    std::vector<AuctionSnapshot> auctionEntryMap = sAuctionMgr.GetAuctionsMap(ahEntry)->GetAuctionsSnapshot();
    for (std::vector<AuctionSnapshot>::const_iterator itr = auctionEntryMap.begin(); itr != auctionEntryMap.end(); ++itr)
    {
        if (!IsBotAuction(itr->bidder))
            continue;

        result -= itr->bid;
    }

    result += (data[AHBOT_WON_PLAYER] - data[AHBOT_WON_SELF]);
    return result < 0 ? 0 : (uint32)result;
}

void AhBot::CheckCategoryMultipliers()
{
    auto results = CharacterDatabase.PQuery("SELECT category, multiplier, max_auction_count, expire_time FROM ahbot_category");
    std::unique_ptr<QueryResult> results_guard(results);
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            categoryMultipliers[fields[0].GetString()] = fields[1].GetFloat();
            categoryMaxAuctionCount[fields[0].GetString()] = fields[2].GetInt32();
            categoryMultiplierExpireTimes[fields[0].GetString()] = fields[3].GetUInt64();

        } while (results->NextRow());
    }

    CharacterDatabase.PExecute("DELETE FROM ahbot_category");

    std::set<std::string> tmp;
    for (int i = 0; i < CategoryList::instance.size(); i++)
    {
        std::string name = CategoryList::instance[i]->GetDisplayName();

        if (tmp.find(name) != tmp.end())
            continue;

        tmp.insert(name);
        if (categoryMultiplierExpireTimes[name] <= (uint64)time(0) || categoryMultipliers[name] <= 0)
        {
            uint32 k = urand(1, 100);
            double m = 1.0;
            double r = (double)urand(100, 200) / 100.0;
            if (k < 50) m = r; // 1..2
            else if (k < 80) m = 1 + r; // 2..3
            else if (k < 90) m = 2 + r; // 3..4
            else m = 3 + r; // 4..5
            categoryMultipliers[name] = m;
            categoryMultiplierExpireTimes[name] = time(0) + urand(4, 7) * 3600 * 24;
        }

        categoryMaxAuctionCount[name] = CategoryList::instance[i]->GetMaxAllowedAuctionCount();

        CharacterDatabase.PExecute("INSERT INTO ahbot_category (category, multiplier, max_auction_count, expire_time) "
                "VALUES ('%s', '%f', '%u', '%zu')",
                name.c_str(), categoryMultipliers[name], categoryMaxAuctionCount[name], categoryMultiplierExpireTimes[name]);
    }
}


void AhBot::updateMarketPrice(uint32 itemId, double price, uint32 auctionHouse)
{
    double marketPrice = 0;

    auto results = CharacterDatabase.PQuery("SELECT price FROM ahbot_price WHERE item = '%u' AND auction_house = '%u'", itemId, auctionHouse);
    std::unique_ptr<QueryResult> results_guard(results);
    if (results)
    {
        marketPrice = results->Fetch()[0].GetFloat();
    }

    if (marketPrice > 0)
        marketPrice = (marketPrice + price) / 2;
    else
        marketPrice = price;

    CharacterDatabase.PExecute("DELETE FROM ahbot_price WHERE item = '%u' AND auction_house = '%u'", itemId, auctionHouse);
    CharacterDatabase.PExecute("INSERT INTO ahbot_price (item, price, auction_house) VALUES ('%u', '%lf', '%u')", itemId, marketPrice, auctionHouse);
}

bool AhBot::IsBotAuction(uint32 bidder)
{
    return allBidders.find(bidder) != allBidders.end();
}

uint32 AhBot::GetRandomBidder(uint32 auctionHouse)
{
    uint32 faction = factions[auctionHouse];
    std::vector<uint32> guids = bidders[faction];
    if (guids.empty())
    {
        sLog.outError("[AhBot] GetRandomBidder: no bidders registered for AH %u (faction %u)", auctionHouse, faction);
        return 0;
    }

    std::vector<uint32> online;
    for (std::vector<uint32>::iterator i = guids.begin(); i != guids.end(); ++i)
    {
        uint32 guid = *i;
        std::string name;
        if (!sObjectMgr.GetPlayerNameByGUID(ObjectGuid(HIGHGUID_PLAYER, guid), name))
        {
            sLog.outError("[AhBot] GetRandomBidder: GUID %u has no character record (AH %u faction %u) — skipping", guid, auctionHouse, faction);
            continue;
        }

        online.push_back(guid);
    }

    if (online.empty())
    {
        sLog.outError("[AhBot] GetRandomBidder: all %zu bidder GUID(s) for AH %u failed character lookup", guids.size(), auctionHouse);
        return 0;
    }

    int index = urand(0, online.size() - 1);
    return online[index];
}

void AhBot::LoadRandomBots()
{
    sLog.outString("[AhBot] LoadRandomBots: scanning %zu random bot account(s)", sPlayerbotAIConfig.randomBotAccounts.size());

    for (std::list<uint32>::iterator i = sPlayerbotAIConfig.randomBotAccounts.begin(); i != sPlayerbotAIConfig.randomBotAccounts.end(); i++)
    {
        uint32 accountId = *i;
        if (!sAccountMgr.GetCharactersCount(accountId))
            continue;

        auto result = CharacterDatabase.PQuery("SELECT guid, race FROM characters WHERE account = '%u'", accountId);
        std::unique_ptr<QueryResult> result_guard(result);
        if (!result)
            continue;

        do
        {
            Field* fields = result->Fetch();
            uint32 guid = fields[0].GetUInt32();
            uint8 race = fields[1].GetUInt8();
            uint32 auctionHouse = PlayerbotAI::IsOpposing(race, RACE_HUMAN) ? 2 : 1;
            bidders[auctionHouse].push_back(guid);
            bidders[3].push_back(guid);
            allBidders.insert(guid);
        } while (result->NextRow());
    }

    if (allBidders.empty() && sAhBotConfig.guid)
    {
        sLog.outString("[AhBot] No bot-account bidders found — falling back to AhBot.GUID=%llu", (unsigned long long)sAhBotConfig.guid);
        uint32 guid = sAhBotConfig.guid;
        allBidders.insert(guid);
        for (int i = 1; i <= 3; i++)
        {
            bidders[i].push_back(guid);
        }
    }

    sLog.outString("[AhBot] Bidders ready: Alliance=%zu Horde=%zu Neutral=%zu (total unique=%zu)",
        bidders[1].size(), bidders[2].size(), bidders[3].size(), allBidders.size());
}

int32 AhBot::GetSellPrice(ItemPrototype const* proto)
{
    if (!sAhBotConfig.enabled)
        return 0;

    int32 maxPrice = 0;
    for (int i=0; i<CategoryList::instance.size(); i++)
    {
        Category* category = CategoryList::instance[i];
        if (!category->Contains(proto))
            continue;

        std::vector<uint32> items = availableItems.Get(category);
        if (std::find(items.begin(), items.end(), proto->ItemId) == items.end())
            continue;

        for (int auction = 0; auction < MAX_AUCTIONS; auction++)
        {
            int32 price = (int32)category->GetPricingStrategy()->GetSellPrice(proto, auctionIds[auction]);
            if (!price)
                price = (int32)category->GetPricingStrategy()->GetBuyPrice(proto, auctionIds[auction]);

            if (price > maxPrice)
                maxPrice = price;
        }
    }

    return maxPrice;
}

int32 AhBot::GetBuyPrice(ItemPrototype const* proto)
{
    if (!sAhBotConfig.enabled)
        return 0;

    int32 maxPrice = 0;
    for (int i=0; i<CategoryList::instance.size(); i++)
    {
        Category* category = CategoryList::instance[i];
        if (!category->Contains(proto))
            continue;

        std::vector<uint32> items = availableItems.Get(category);
        if (std::find(items.begin(), items.end(), proto->ItemId) == items.end())
            continue;

        for (int auction = 0; auction < MAX_AUCTIONS; auction++)
        {
            int32 price = (int32)category->GetPricingStrategy()->GetBuyPrice(proto, auctionIds[auction]);
            if (!price)
                continue;

            if (price > maxPrice)
                maxPrice = price;
        }
    }

    return maxPrice;
}

double AhBot::GetRarityPriceMultiplier(const ItemPrototype* proto)
{
    if (!sAhBotConfig.enabled)
        return 1.0;

    for (int i=0; i<CategoryList::instance.size(); i++)
    {
        Category* category = CategoryList::instance[i];
        if (!category->Contains(proto))
            continue;

        return category->GetPricingStrategy()->GetRarityPriceMultiplier(proto->ItemId);
    }

    return 1.0;

}

bool AhBot::IsUsedBySkill(const ItemPrototype* proto, uint32 skillId)
{
    if (!sAhBotConfig.enabled)
        return false;

    for (int i=0; i<CategoryList::instance.size(); i++)
    {
        Category* category = CategoryList::instance[i];
        if (category->GetSkillId() == skillId && category->Contains(proto))
            return true;
    }

    return false;
}

void AhBot::CheckSendMail(uint32 bidder, uint32 price, const AuctionSnapshot& entry)
{
    if (!sAhBotConfig.sendmail)
        return;

    time_t entryTime = GetTime("entry", entry.Id, entry.houseId, AHBOT_SENDMAIL);
    if (entryTime > time(0))
        return;

    const AuctionHouseEntry* ahEntry = sAuctionHouseStore.LookupEntry(entry.houseId);
    if (!ahEntry)
        return;

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);
    std::vector<AuctionSnapshot> auctionEntryMap = auctionHouse->GetAuctionsSnapshot();
    for (std::vector<AuctionSnapshot>::const_iterator itr = auctionEntryMap.begin(); itr != auctionEntryMap.end(); ++itr)
    {
        const AuctionSnapshot& otherEntry = *itr;
        if (otherEntry.owner == entry.owner && otherEntry.Id != entry.Id && otherEntry.itemTemplate == entry.itemTemplate)
        {
            time_t otherEntryTime = GetTime("entry", otherEntry.Id, entry.houseId, AHBOT_SENDMAIL);
            if (otherEntryTime > time(0))
                return;
        }
    }

    // Sending resolves the receiver through ObjectAccessor and can reach their
    // live Player, so hand it to the world thread rather than doing it here.
    PendingProposition proposition;
    proposition.auctionId   = entry.Id;
    proposition.owner       = entry.owner;
    proposition.itemGuidLow = entry.itemGuidLow;
    proposition.bidder      = bidder;
    proposition.price       = price;
    proposition.houseId     = entry.houseId;
    proposition.expireTime  = entry.expireTime;

    std::lock_guard<std::mutex> g(queuedWorkMutex);
    queuedPropositions.push_back(proposition);
}

void AhBot::RunQueuedWork()
{
    std::vector<PendingPurchase> purchases;
    std::vector<PendingProposition> propositions;
    {
        std::lock_guard<std::mutex> g(queuedWorkMutex);
        if (queuedPurchases.empty() && queuedPropositions.empty())
            return;
        purchases.swap(queuedPurchases);
        propositions.swap(queuedPropositions);
    }

    for (std::vector<PendingPurchase>::const_iterator i = purchases.begin(); i != purchases.end(); ++i)
        ExecutePurchase(*i);

    for (std::vector<PendingProposition>::const_iterator i = propositions.begin(); i != propositions.end(); ++i)
        ExecuteProposition(*i);
}

void AhBot::ExecutePurchase(const PendingPurchase& p)
{
    const AuctionHouseEntry* ahEntry = sAuctionHouseStore.LookupEntry(auctionIds[p.houseIndex]);
    if (!ahEntry)
        return;

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);
    if (!auctionHouse)
        return;

    // The seller may have cancelled, or the auction may have expired or sold,
    // between the bot deciding and us getting here.
    AuctionEntry* entry = auctionHouse->GetAuction(p.auctionId);
    if (!entry)
        return;

    Item* item = sAuctionMgr.GetAItem(entry->itemGuidLow);
    if (!item || !item->GetCount())
        return;

    ItemPrototype const* proto = item->GetProto();
    if (!proto)
        return;

    entry->bidder = p.bidder;
    entry->bid = p.bidAmount;

    if ((entry->buyout && (entry->bid >= entry->buyout || 100 * (entry->buyout - entry->bid) / std::max(p.unitPrice, 1u) < 25)) &&
            !(p.minBuyout && entry->buyout && p.minBuyout < entry->buyout))
    {
        entry->bid = entry->buyout;
        sLog.outString("[AhBot] Bought: %dx %s on AH %u for %u (bidder guid=%u)",
                item->GetCount(), proto->Name1.c_str(), auctionIds[p.houseIndex], entry->buyout, p.bidder);
    }
    else
    {
        sLog.outString("[AhBot] Bought (at bid): %dx %s on AH %u for %u (bidder guid=%u)",
                item->GetCount(), proto->Name1.c_str(), auctionIds[p.houseIndex], entry->bid, p.bidder);
    }

    updateMarketPrice(proto->ItemId, entry->buyout / item->GetCount(), auctionIds[p.houseIndex]);

    // Pay the seller immediately and finalize the auction.
    // If the item is an upgrade for the bidder bot, equip it directly in the DB.
    // Otherwise discard it - letting items go through the normal mail/login path
    // causes inventory corruption when bot inventories are full.
    uint32 itemGuidLow = entry->itemGuidLow;
    sAuctionMgr.SendAuctionSuccessfulMail(entry);
    if (!TryEquipItem(entry->bidder, itemGuidLow, proto))
        CharacterDatabase.PExecute("DELETE FROM item_instance WHERE guid='%u'", itemGuidLow);
    sAuctionMgr.RemoveAItem(itemGuidLow);
    delete item;
    AddToHistory(entry, AHBOT_WON_BID);
    entry->DeleteFromDB();
    auctionHouse->RemoveAuction(entry);
    delete entry;

    CharacterDatabase.PExecute("DELETE FROM ahbot_history WHERE item = '%u' AND won = 4 AND auction_house = '%u' ",
            proto->ItemId, factions[auctionIds[p.houseIndex]]);
}

void AhBot::ExecuteProposition(const PendingProposition& p)
{
    Item* item = sAuctionMgr.GetAItem(p.itemGuidLow);
    if (!item || !item->GetProto())
        return;

    std::string name;
    if (!sObjectMgr.GetPlayerNameByGUID(ObjectGuid(HIGHGUID_PLAYER, p.bidder), name))
        return;

    std::ostringstream body;
    body << "Hello,\n";
    body << "\n";
    body << "I see you posted " << ChatHelper::formatItem(item, item->GetCount());
    body << " to the AH and I really need that at the moment. Could you lower your price at least to ";
    body << ChatHelper::formatMoney(PricingStrategy::RoundPrice(p.price)) << "? I'll buy it then.\n";
    body << "\n";
    body << "Regards,\n";
    body << name << "\n";

    std::ostringstream title; title << "AH Proposition: " << item->GetProto()->Name1.c_str();
    MailDraft draft(title.str(), body.str());
    ObjectGuid receiverGuid(HIGHGUID_PLAYER, p.owner);
    draft.SendMailTo(MailReceiver(receiverGuid), MailSender(MAIL_NORMAL, p.bidder));

    SetTime("entry", p.auctionId, p.houseId, AHBOT_SENDMAIL, p.expireTime);
}

void AhBot::Dump()
{
    for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto)
            continue;

        bool first = true;
        for (int i=0; i<CategoryList::instance.size(); i++)
        {
            Category* category = CategoryList::instance[i];
            if (category->Contains(proto))
            {
                std::vector<uint32> items = availableItems.Get(category);
                if (find(items.begin(), items.end(), proto->ItemId) == items.end())
                    continue;

                std::ostringstream out;
                if (first)
                {
                    out << proto->ItemId << " (" << proto->Name1.c_str() << ") x" << category->GetStackCount(proto) << " - ";
                    first = false;
                }

                int auction = 0;
                const AuctionHouseEntry* ahEntry = sAuctionHouseStore.LookupEntry(auctionIds[auction]);
                out << "SELL: "
                    << ChatHelper::formatMoney(category->GetPricingStrategy()->GetSellPrice(proto, auctionIds[auction], true))
                    << ", BUY: "
                    << ChatHelper::formatMoney(category->GetPricingStrategy()->GetBuyPrice(proto, auctionIds[auction]))
                    << " (" << category->GetDisplayName() << ")";
                sLog.outString("%s",out.str().c_str());
            }
        }
    }
}

void AhBot::CleanupPropositions()
{
    uint32 deliverTime = time(0) - 3600 * 24 * 2;
    auto result = CharacterDatabase.PQuery("select id, receiver from mail where subject like 'AH Proposition%%' and deliver_time <= '%u'", deliverTime);
    std::unique_ptr<QueryResult> result_guard(result);
    if (!result)
        return;

    int count = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 id = fields[0].GetUInt32();
        uint32 receiver = fields[1].GetUInt32();
        Player *player = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, receiver));
        if (player) player->RemoveMail(id);
        count++;
    } while (result->NextRow());

    if (count > 0)
    {
        CharacterDatabase.PExecute("delete from mail where subject like 'AH Proposition%%' and deliver_time <= '%u'", deliverTime);
        sLog.outBasic("%d old AH propositions removed", count);
    }
}

void AhBot::DeleteMail(std::list<uint32> buffer)
{
    std::ostringstream sql;
    sql << "delete from mail where id in ( ";
    bool first = true;
    for (std::list<uint32>::iterator j = buffer.begin(); j != buffer.end(); ++j)
    {
        if (first) first = false; else sql << ",";
        sql << "'" << *j << "'";
    }
    sql << ")";
    CharacterDatabase.Execute(sql.str().c_str());
}

INSTANTIATE_SINGLETON_1( ahbot::AhBot );
