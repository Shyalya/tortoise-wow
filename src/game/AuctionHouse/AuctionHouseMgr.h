/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _AUCTION_HOUSE_MGR_H
#define _AUCTION_HOUSE_MGR_H

#include <vector>
#include <memory>
#include <mutex>

#include "Common.h"
#include "SharedDefines.h"
#include "Policies/Singleton.h"
#include "DBCStructure.h"
#include "Log.h"

class Item;
class Player;
class Unit;
class WorldPacket;

#define MIN_AUCTION_TIME (2*HOUR)

enum AuctionError
{
    AUCTION_OK                          = 0,                // depends on enum AuctionAction
    AUCTION_ERR_INVENTORY               = 1,                // depends on enum InventoryChangeResult
    AUCTION_ERR_DATABASE                = 2,                // ERR_AUCTION_DATABASE_ERROR (default)
    AUCTION_ERR_NOT_ENOUGH_MONEY        = 3,                // ERR_NOT_ENOUGH_MONEY
    AUCTION_ERR_ITEM_NOT_FOUND          = 4,                // ERR_ITEM_NOT_FOUND
    AUCTION_ERR_HIGHER_BID              = 5,                // ERR_AUCTION_HIGHER_BID
    AUCTION_ERR_BID_INCREMENT           = 7,                // ERR_AUCTION_BID_INCREMENT
    AUCTION_ERR_BID_OWN                 = 10,               // ERR_AUCTION_BID_OWN
    AUCTION_ERR_RESTRICTED_ACCOUNT      = 13                // ERR_RESTRICTED_ACCOUNT
};

enum AuctionAction
{
    AUCTION_STARTED     = 0,                                // ERR_AUCTION_STARTED
    AUCTION_REMOVED     = 1,                                // ERR_AUCTION_REMOVED
    AUCTION_BID_PLACED  = 2                                 // ERR_AUCTION_BID_PLACED
};

enum AuctionClientQueryType
{
    AUCTION_QUERY_LIST,
    AUCTION_QUERY_LIST_OWNER,
    AUCTION_QUERY_LIST_BIDDER
};

struct AuctionEntry
{
    uint32 Id;
    uint32 itemGuidLow;
    uint32 itemTemplate;
    uint32 owner;
    uint32 ownerAccount;
    uint32 startbid;                                        // maybe useless
    uint32 bid;
    uint32 buyout;
    std::string lockedIpAddress;
    time_t depositTime;
    time_t expireTime;
    uint32 bidder;
    uint32 deposit;                                         // deposit can be calculated only when creating auction
    AuctionHouseEntry const* auctionHouseEntry;             // in AuctionHouse.dbc

    // bot accesses itemRandomPropertyId / itemCount on AuctionEntry.
    // Penqle stores these on the Item, not on AuctionEntry. Stub fields are 0/1
    //
    int32 itemRandomPropertyId = 0;
    uint32 itemCount = 1;
    // AuctionBidWinning: cmangos has it as a method (notify winner). Stub no-op.
    void AuctionBidWinning(class Player* /*winner*/) const {}
    // UpdateBid: cmangos has it (raise bid + notify). Stub no-op.
    void UpdateBid(uint32 /*newbid*/, class Player* /*newbidder*/ = nullptr) {}

    // helpers
    uint32 GetHouseId() const { return auctionHouseEntry->houseId; }
    uint32 GetHouseFaction() const { return auctionHouseEntry->faction; }
    uint32 GetAuctionCut() const;
    uint32 GetAuctionOutBid() const;
    bool BuildAuctionInfo(WorldPacket & data) const;
    void DeleteFromDB() const;
    void SaveToDB() const;
    bool IsAvailableFor(Player* player);
};

struct AuctionHouseClientQuery
{
    uint32 accountId;
    std::wstring wsearchedname;
    uint8 levelmin;
    uint8 levelmax;
    uint8 usable;
    uint32 listfrom, auctionSlotID, auctionMainCategory, auctionSubCategory, quality;
    uint32 outbiddedCount;
    std::vector<uint32> outbiddedAuctionIds;
};

// A by-value copy of the auction fields the auction-house bot reads.
//
// AhBot runs its check on a detached thread of its own (ahbot/AhBot.cpp uses
// boost::thread) and a single pass takes ~50 seconds per auction house. All
// through that pass the world thread keeps adding, removing and - down in
// AuctionHouseObject::Update - *deleting* AuctionEntry objects. Walking the
// live map from the bot thread therefore dies in _Rb_tree_increment whenever a
// rebalance happens mid-iteration, and any AuctionEntry* the bot holds on to
// across the pass is a use-after-free waiting for its moment.
//
// So the bot never sees the live map. It takes a snapshot - copied under the
// lock in a few milliseconds - and works off that for as long as it likes.
// When it finally wants to act on an entry it looks the id up again under the
// lock and re-checks that the auction is still there.
struct AuctionSnapshot
{
    uint32 Id;
    uint32 itemGuidLow;
    uint32 itemTemplate;
    uint32 owner;
    uint32 ownerAccount;
    uint32 startbid;
    uint32 bid;
    uint32 buyout;
    uint32 bidder;
    uint32 houseId;
    uint32 itemCount;
    time_t expireTime;
};

//this class is used as auctionhouse instance
class AuctionHouseObject
{
    public:
        AuctionHouseObject() {}
        ~AuctionHouseObject()
        {
            Guard g(m_auctionsLock);
            for (const auto& itr : AuctionsMap)
                delete itr.second;
        }

        typedef std::map<uint32, AuctionEntry*> AuctionEntryMap;
        typedef std::multimap<uint32, AuctionEntry*> AuctionMultiMap;
        typedef std::pair<AuctionEntryMap::iterator, AuctionEntryMap::iterator> AuctionEntryMapBounds;

        // Guards all three maps below. Recursive because Update() and
        // RemoveAllAuctions() both call RemoveAuction() while already holding
        // it. Lock order where both are taken: this one before
        // AuctionHouseMgr::m_itemsLock, never the other way round.
        typedef std::lock_guard<std::recursive_mutex> Guard;
        std::recursive_mutex& GetLock() const { return m_auctionsLock; }

        // Raw iterators into the live map: only valid while GetLock() is held
        // for the whole loop. Fine for the short world-thread scans that can
        // afford to hold it - the bot thread must use GetAuctionsSnapshot()
        // instead, see the comment on AuctionSnapshot above.
        AuctionEntryMapBounds GetAuctionsBounds_locked() { return { AuctionsMap.begin(), AuctionsMap.end() }; }

        std::vector<AuctionSnapshot> GetAuctionsSnapshot() const;

        uint32 GetCount() { Guard g(m_auctionsLock); return AuctionsMap.size(); }

        void AddAuction(AuctionEntry *ah);

        AuctionEntry* GetAuction(uint32 id) const
        {
            Guard g(m_auctionsLock);
            AuctionEntryMap::const_iterator itr = AuctionsMap.find( id );
            return itr != AuctionsMap.end() ? itr->second : nullptr;
        }

        bool RemoveAuction(AuctionEntry* entry);

        void RemoveAllAuctions(Player* player);

        void Update();

        void BuildListBidderItems(WorldPacket& data, Player* player, uint32 listfrom, uint32& count, uint32& totalcount);
        void BuildListOwnerItems(WorldPacket& data, Player* player, uint32 listfrom, uint32& count, uint32& totalcount);
        void BuildListAuctionItems(WorldPacket& data, Player* player,
                AuctionHouseClientQuery const& query,
            uint32& count, uint32& totalcount);
        uint32 GetAccountAuctionCount(uint32 accountId) { Guard g(m_auctionsLock); return AccountAuctionMap.count(accountId); }
    private:
        mutable std::recursive_mutex m_auctionsLock;
        // Map BUYOUT prices to entry for pre-sorted results. We maintain it in
        // a map rather than build the list on query for performance reasons.
        // Similarly, maintain a map of account ID -> auction entry
        AuctionMultiMap OrderedAuctionMap;
        AuctionMultiMap AccountAuctionMap;
        AuctionEntryMap AuctionsMap;
};

class AuctionHouseMgr
{
    public:
        AuctionHouseMgr();
        ~AuctionHouseMgr();

        typedef std::unordered_map<uint32, Item*> ItemMap;

        AuctionHouseObject* GetAuctionsMap(AuctionHouseEntry const* house);
        // cmangos's AuctionHouseType-keyed lookup.
        AuctionHouseObject* GetAuctionsMap(uint32 /*type*/) { return GetAuctionsMap((AuctionHouseEntry const*)nullptr); }

        Item* GetAItem(uint32 id)
        {
            std::lock_guard<std::mutex> g(m_itemsLock);
            ItemMap::const_iterator itr = mAitems.find(id);
            if (itr != mAitems.end())
            {
                return itr->second;
            }
            return nullptr;
        }

        //auction messages
        void SendAuctionWonMail( AuctionEntry * auction );
        void SendAuctionSuccessfulMail( AuctionEntry * auction );
        void SendAuctionExpiredMail( AuctionEntry * auction );
        static uint32 GetAuctionDeposit(AuctionHouseEntry const* entry, uint32 time, Item *pItem);

        static uint32 GetAuctionHouseId(uint32 factionTemplateId);
        static uint32 GetAuctionHouseTeam(AuctionHouseEntry const* house);
        static AuctionHouseEntry const* GetAuctionHouseEntry(Unit* unit);
        static AuctionHouseEntry const* GetAuctionHouseEntry(uint32 factionId);

    public:
        //load first auction items, because of check if item exists, when loading
        void LoadAuctionItems();
        void LoadAuctions();
        void LoadAuctionHouses();

        void AddAItem(Item* it);
        bool RemoveAItem(uint32 id);

        void Update();

    private:
        AuctionHouseObject* MakeNewAuctionHouseObject();
        std::unordered_map<uint32, AuctionHouseObject*> m_mAuctionHouses;
        std::vector<std::unique_ptr<AuctionHouseObject>> m_vRealAuctionHouses;

        mutable std::mutex  m_itemsLock;
        ItemMap             mAitems;
};

extern AuctionHouseMgr sAuctionMgr;

#endif
