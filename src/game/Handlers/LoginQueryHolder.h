/*
 * MaNGOS — moved out of CharacterHandler.cpp so the bot module can
 * construct LoginQueryHolder instances for synthetic bot logins.
 */

#ifndef MANGOSSERVER_LOGINQUERYHOLDER_H
#define MANGOSSERVER_LOGINQUERYHOLDER_H

#include "Database/DatabaseEnv.h"
#include "Database/SqlOperations.h"
#include "ObjectGuid.h"
#include "SessionTransport.h"

class LoginQueryHolder : public SqlQueryHolder
{
private:
    uint32 m_accountId;
    ObjectGuid m_guid;
    SessionTransport m_transport;
public:
    LoginQueryHolder(uint32 accountId, ObjectGuid guid, SessionTransport transport = SessionTransport::Network)
        : SqlQueryHolder(guid.GetCounter()), m_accountId(accountId), m_guid(guid), m_transport(transport) { }
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
    SessionTransport GetTransport() const
    {
        return m_transport;
    }
    bool Initialize();
};

#endif
