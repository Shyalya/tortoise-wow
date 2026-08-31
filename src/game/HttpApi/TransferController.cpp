#include "TransferController.hpp"

#include "HttpApi/Authorizers/ApiKeyAuthorizer.hpp"

#include "World.h"
#include "ObjectMgr.h"
#include "Mail.h"
#include "AccountMgr.h"

#include <algorithm>
#include <cctype>

using namespace httplib;

namespace HttpApi
{
    namespace
    {
        constexpr std::size_t MaxTransferPayloadLength = 16 * 1024 * 1024;

        void SetClientError(Response& resp, int status, const char* message)
        {
            resp.status = status;
            resp.set_content(message, "text/plain");
        }

        bool IsJsonRequest(const Request& req)
        {
            if (!req.has_header("Content-Type"))
                return false;

            std::string contentType = req.get_header_value("Content-Type");
            const auto parameters = contentType.find(';');
            if (parameters != std::string::npos)
                contentType.resize(parameters);

            while (!contentType.empty() && std::isspace(static_cast<unsigned char>(contentType.back())))
                contentType.pop_back();

            auto first = contentType.begin();
            while (first != contentType.end() && std::isspace(static_cast<unsigned char>(*first)))
                ++first;
            contentType.erase(contentType.begin(), first);

            std::transform(contentType.begin(), contentType.end(), contentType.begin(),
                [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

            return contentType == "application/json";
        }

        bool ReadPayload(const ContentReader& reader, std::string& body, bool& payloadTooLarge)
        {
            return reader([&body, &payloadTooLarge](const char* data, size_t dataLength)
            {
                if (payloadTooLarge || body.size() > MaxTransferPayloadLength ||
                    dataLength > MaxTransferPayloadLength - body.size())
                {
                    payloadTooLarge = true;
                    // Keep consuming the entity so a keep-alive connection cannot
                    // interpret the remainder as the next HTTP request.
                    return true;
                }

                body.append(data, dataLength);
                return true;
            });
        }
    }

    TransferController::TransferController(std::string key)
    {
        _authorizer = std::make_unique<ApiKeyAuthorizer>(key.c_str());
    }

    std::string DumpReturnToString(DumpReturn dumpReturn)
    {
        switch (dumpReturn)
        {
            case DUMP_SUCCESS:
                return "Dump success";
            case DUMP_FILE_OPEN_ERROR:
                return "Error with file open";
            case DUMP_TOO_MANY_CHARS:
                return "Too many characters on import";
            case DUMP_UNEXPECTED_END:
                return "Unexpeced end on the import file";
            case DUMP_FILE_BROKEN:
                return "Dump file broken";
        }
        return "";
    }

    //This is part 1 of transfer procedure, will EXTRACT char data.
    void InitTransferAction(const Request& req, Response& resp, const ContentReader& reader)
    {
        std::string body;
        bool payloadTooLarge = false;
        bool const readSucceeded = ReadPayload(reader, body, payloadTooLarge);
        if (payloadTooLarge || !readSucceeded)
        {
            const bool tooLarge = payloadTooLarge || resp.status == 413;
            SetClientError(resp, tooLarge ? 413 : 400, tooLarge ? "Payload too large." : "Invalid request.");
            if (!readSucceeded)
                resp.set_header("Connection", "close");
            return;
        }

        if (!IsJsonRequest(req))
        {
            SetClientError(resp, 415, "Content-Type must be application/json.");
            return;
        }

        sLog.out(LOG_API, "Init transfer started.");

        rapidjson::Document d;
        d.Parse(body.data(), body.size());

        if (d.HasParseError())
        {
            SetClientError(resp, 400, "Bad JSON.");
            sLog.out(LOG_API, "Bad JSON for init transfer from %s.", req.remote_addr.c_str());
            return;
        }

        if (!d.IsObject() || !d.HasMember("lowGuid") || !d["lowGuid"].IsUint())
        {
            SetClientError(resp, 400, "Bad JSON.");
            sLog.out(LOG_API, "Invalid init transfer request from %s.", req.remote_addr.c_str());
            return;
        }

        uint32 lowGuid = d["lowGuid"].GetUint();

        if (!sObjectMgr.GetPlayerDataByGUID(lowGuid))
        {
            SetClientError(resp, 404, "Not found.");
            sLog.out(LOG_API, "Init transfer could not find player by supplied GUID %u, aborting.", lowGuid);
            return;
        }

        auto playerData = sObjectMgr.GetPlayerDataByGUID(lowGuid);


        auto accountData = sAccountMgr.GetAccountData(playerData->uiAccount);

        if (!accountData)
        {
            SetClientError(resp, 404, "Not found.");
            sLog.out(LOG_API, "Init transfer could not find player account by supplied GUID %u , acc ID %u, aborting.", lowGuid, playerData->uiAccount);
            return;
        }

        //1st of Oct, 2023 for now.
        constexpr uint64 CreationCutoffTimestamp = 1696122966;

        if (accountData->CreatedAt > CreationCutoffTimestamp)
        {
            SetClientError(resp, 403, "Transfer not permitted.");
            return;
        }



        //Convert shellcoin current price, remove the shellcoins from the player and compensate in gold.

        auto result = std::unique_ptr<QueryResult>(CharacterDatabase.PQuery("SELECT SUM(count) FROM item_instance WHERE itemEntry = 81118 AND owner_guid = %u GROUP BY owner_guid",
            lowGuid));


        if (result)
        {
            auto shellcoinCount = (*result)[0].GetUInt32();

            auto extraMoney = sObjectMgr.GetShellCoinSellPrice() * shellcoinCount;

            CharacterDatabase.DirectPExecute("UPDATE characters SET money = money + %u WHERE guid = %u", extraMoney, lowGuid);
            CharacterDatabase.DirectPExecute("DELETE FROM item_instance WHERE itemEntry = 81118 AND owner_guid = %u", lowGuid);
        }

        //Add fashion coins because transferred chars lose their xmog on transfer.

        result = std::unique_ptr<QueryResult>(CharacterDatabase.PQuery("SELECT COUNT(*) FROM item_instance WHERE transmogrifyId != 0 AND owner_guid = %u", lowGuid));

        if (result)
        {
            uint32 count = result->Fetch()[0].GetUInt32();
            if (count > 0)
            {
                MailDraft draft;

                draft.SetSubjectAndBody("Fashion coins", "Fashion coins");

                if (Item* item = Item::CreateItem(51217, count, 0))
                {
                    item->SaveToDB(true);
                    draft.AddItem(item);
                }

                MailSender sender(MAIL_NORMAL, (uint32)0, MAIL_STATIONERY_GM);

                draft.SendMailTo(MailReceiver(nullptr, lowGuid), sender, MAIL_CHECK_MASK_NONE, 0, 0, true);
            }
        }

        std::string pDumpData;
        PlayerDumpWriter().ReturnDump(pDumpData, lowGuid);

        rapidjson::Document retDoc;
        retDoc.SetObject();

        rapidjson::Value transferStatusValue{ true };
        rapidjson::Value realmId{ realmID };
        retDoc.AddMember("transferStatus", transferStatusValue, retDoc.GetAllocator());

        auto dataRef = rapidjson::StringRef(pDumpData.c_str());

        retDoc.AddMember("data", dataRef, retDoc.GetAllocator());
        retDoc.AddMember("realmId", realmID, retDoc.GetAllocator());

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        retDoc.Accept(writer);
        resp.set_content(buffer.GetString(), "application/json");
    }

    std::unordered_map<std::string, time_t> transferredNames;


    //This is part 2 of the transfer procedure. This will IMPORT the pdump data and call the necessary import functions.
    //This should be done on the world thread on the OTHER server where extractions take place to do a successful transfer.
    void ProceedTransferAction(const Request& req, Response& resp, const ContentReader& reader)
    {
        std::string body;
        bool payloadTooLarge = false;
        bool const readSucceeded = ReadPayload(reader, body, payloadTooLarge);
        if (payloadTooLarge || !readSucceeded)
        {
            const bool tooLarge = payloadTooLarge || resp.status == 413;
            SetClientError(resp, tooLarge ? 413 : 400, tooLarge ? "Payload too large." : "Invalid request.");
            if (!readSucceeded)
                resp.set_header("Connection", "close");
            return;
        }

        if (!IsJsonRequest(req))
        {
            SetClientError(resp, 415, "Content-Type must be application/json.");
            return;
        }

        rapidjson::Document d;
        d.Parse(body.data(), body.size());


        if (d.HasParseError())
        {
            SetClientError(resp, 400, "Bad JSON.");
            sLog.out(LOG_API, "Bad JSON for proceed transfer from %s.", req.remote_addr.c_str());
            return;
        }

        if (!d.IsObject())
        {
            SetClientError(resp, 400, "Bad JSON.");
            sLog.out(LOG_API, "Proceed transfer request was not an object from %s.", req.remote_addr.c_str());
            return;
        }

        if (!d.HasMember("data") || !d.HasMember("targetAccountId") || !d.HasMember("source_guid") ||
            !d["data"].IsString() || !d["targetAccountId"].IsUint() || !d["source_guid"].IsUint())
        {
            SetClientError(resp, 400, "Bad JSON.");
            sLog.out(LOG_API, "Invalid proceed transfer request from %s.", req.remote_addr.c_str());
            return;
        }

        std::string pdumpData = d["data"].GetString();
        uint32 accountId = d["targetAccountId"].GetUint();
        uint32 oldGuidLow = d["source_guid"].GetUint();


        sLog.out(LOG_API, "Accepting transfer for target account %u.", accountId);

        uint32 guid = 0;
        std::string charName = "";
        
        std::shared_ptr<uint32> guidPtr = std::make_shared<uint32>(0);
        std::function<void(bool)> transCallback = [guidPtr, accountId, oldGuidLow](bool transSuccess)
        {
            if (transSuccess)
            {
                //only set char active if transaction for migration transfer succeeded.
                CharacterDatabase.PExecute("UPDATE `characters` SET `active` = 1 WHERE `guid` = %u", *guidPtr);
                CharacterDatabase.PExecute("UPDATE `characters` SET `customFlags` = `customFlags` | 0x20 WHERE `guid` = %u", *guidPtr); // add WAS_TRANSFERRED custom flag to take away items after login.

                //Set all purchase logs to new char guid to fix HC not getting proper refunds.
                if (*guidPtr && oldGuidLow)               
                    LoginDatabase.PExecute("UPDATE shop_logs SET guid = %u WHERE guid = %u", *guidPtr, oldGuidLow);
            }
            else
                sLog.out(LOG_API, "FAILED to run transaction for account ID %u", accountId);
        };

        auto res = PlayerDumpReader().LoadStringDump(pdumpData, accountId, charName, guid, &transCallback);
        sLog.out(LOG_API, "Result of transfer for targetAccount:%u\nres:%s.\nnewGuid:%u\nplayername:%s", accountId, DumpReturnToString(res).c_str(), guid, charName.c_str());

        if (res == DumpReturn::DUMP_SUCCESS) 
        {
            *guidPtr = guid;

            if (!charName.empty() && transferredNames.find(charName) != transferredNames.end())
            {
                auto now = time(nullptr);
                if (now - transferredNames[charName] < 60)
                {
                    sLog.out(LOG_API, "ALREADY IMPORTED CHAR. Aborting. AccountId:%u, newGuid:%u,playername:%s", accountId, guid, charName.c_str());
                    CharacterDatabase.PExecute("UPDATE `characters` SET `account` = 0 WHERE `guid` = %u", guid);
                }
            }
            else
                transferredNames[charName] = time(nullptr);

            sLog.out(LOG_API, "Sucessfully accepted transfer import. AccountId:%u, newGuid:%u,playername:%s", accountId, guid, charName.c_str());
        }
        else
            sLog.out(LOG_API, "FAILED dump import.Account:%u\nres:%u.\newGuid:%u\nplayername:%s\ndump result:%s", accountId, (uint32)res, guid, charName.c_str(), DumpReturnToString(res)
            .c_str());

        rapidjson::Document retDoc;
        retDoc.SetObject();

        rapidjson::Value transferResult{ res };
        retDoc.AddMember("transferResult", transferResult, retDoc.GetAllocator());

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        
        retDoc.Accept(writer);
        resp.set_content(buffer.GetString(), "application/json");
    }
}
