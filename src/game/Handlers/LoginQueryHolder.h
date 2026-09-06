/*
 * MaNGOS — moved out of CharacterHandler.cpp so the bot module can
 * construct LoginQueryHolder instances for synthetic bot logins.
 */

#ifndef MANGOSSERVER_LOGINQUERYHOLDER_H
#define MANGOSSERVER_LOGINQUERYHOLDER_H

#include "Database/DatabaseEnv.h"
#include "Database/SqlOperations.h"
#include "ObjectGuid.h"

class LoginQueryHolder : public SqlQueryHolder
{
private:
    uint32 m_accountId;
    ObjectGuid m_guid;
    uint32 m_loginRequestTime = 0;
public:
    void SetLoginRequestTime(uint32 value) { m_loginRequestTime = value; }
    uint32 GetLoginRequestTime() const { return m_loginRequestTime; }
    LoginQueryHolder(uint32 accountId, ObjectGuid guid)
        : SqlQueryHolder(guid.GetCounter()), m_accountId(accountId), m_guid(guid) { }
    ~LoginQueryHolder()
    {
        // Queries should NOT be deleted by user
        DeleteAllResults();
    }
    ObjectGuid GetGuid() const
    {
        return m_guid;
    }
    uint32 GetAccountId() const
    {
        return m_accountId;
    }
    bool Initialize();
};

#endif
