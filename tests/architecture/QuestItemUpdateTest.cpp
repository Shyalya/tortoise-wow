#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
using uint32 = uint32_t;
using uint16 = uint16_t;
using uint8 = uint8_t;
constexpr uint16 MAX_QUEST_LOG_SIZE = 25;
constexpr uint32 SMSG_QUESTUPDATE_ADD_ITEM = 1;
#define DEBUG_LOG(...) ((void)0)
struct WorldPacket
{
    uint32 opcode;
    std::vector<uint32> values;
    WorldPacket(uint32 code, uint32) : opcode(code) {}
    WorldPacket& operator<<(uint32 value) { values.push_back(value); return *this; }
};
struct Quest
{
    uint32 ReqItemId[4] = {101, 102, 103, 104};
    uint32 objectives = 0;
    uint32 GetQuestId() const { return 17; }
    uint32 GetReqCreatureOrGOcount() const { return objectives; }
};
struct Session
{
    uint32 packets = 0;
    std::vector<uint32> payload;
    void SendPacket(WorldPacket* packet)
    {
        if (packet->opcode != SMSG_QUESTUPDATE_ADD_ITEM) std::abort();
        payload = packet->values;
        ++packets;
    }
};
struct Player
{
    Session session;
    uint16 slot = 0;
    uint32 counterWrites = 0;
    Session* GetSession() { return &session; }
    uint16 FindQuestSlot(uint32) { return slot; }
    void SetQuestSlotCounter(uint16, uint8, uint8) { ++counterWrites; }
    void SendQuestUpdateAddItem(Quest const*, uint32, uint32, uint32);
};
#include "QuestItemUpdate.inc"
int main()
{
    // Execute the actual handler, retaining traps for the previous field write.
    // Cover item-only/mixed objectives, final/missing quest slots, all item
    // indices, repeated updates, and item totals exceeding packed kill bits.
    Player player;
    Quest quest;
    for (uint16 slot : {uint16(0), uint16(12), uint16(24), uint16(25)})
        for (uint32 objectives : {0u, 1u, 4u, 8u})
            for (uint32 item = 0; item < 4; ++item)
                for (uint32 count : {1u, 63u, 100u})
                {
                    player.slot = slot;
                    quest.objectives = objectives;
                    player.SendQuestUpdateAddItem(&quest, item, 62, count);
                    if (player.counterWrites || player.session.payload !=
                        std::vector<uint32>{quest.ReqItemId[item], count}) std::abort();
                }
    if (player.session.packets != 192) std::abort();
    std::cout << "Native item update packet preserved; no packed quest counter writes\n";
}
