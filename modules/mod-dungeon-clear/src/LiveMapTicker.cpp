// Live-Ticker fuer die Lagekarte im Panel.
//
// Schreibt einmal je Sekunde eine Datei mit Position, Leben, Mana, Gruppe und Ziel
// jedes eingeloggten Spielers. Mehr tut er nicht: kein Eingriff in Bewegung, Kampf
// oder Datenbank, kein zusaetzlicher Thread. Die Panel-Seite liest die Datei und
// zeichnet Punkte auf die Instanz- oder Kontinentkarte.
//
// Warum hier und nicht als eigenes Modul: ein neues Modul heisst neue CMake- und
// Loader-Verdrahtung, und dafuer ist der Nutzen zu klein. Die Datei steht fuer sich,
// haengt an nichts aus mod-dungeon-clear und laesst sich jederzeit wieder loesen.
//
// Aufgehaengt an PLAYERHOOK_ON_UPDATE, weil dieser Baum keinen WorldScript kennt.
// Welcher Spieler den Takt ausloest, ist gleichgueltig -- geschrieben wird ohnehin
// die ganze Sitzungsliste, und ein statischer Zeitstempel sorgt dafuer, dass das
// hoechstens einmal je Sekunde passiert.
//
// Aus per Vorgabe. LiveMap.File in der Konfiguration nennt das Ziel; ohne Eintrag
// laeuft hier gar nichts.

#include "ScriptMgr.h"
#include "Config/Config.h"
#include "Group.h"
#include "Log.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "World.h"
#include "WorldSession.h"

#include <cstdio>
#include <string>

namespace
{
    void SchreibeLage()
    {
        static uint32      s_cfgWhen = 0;
        static std::string s_pfad;

        const uint32 jetzt = (uint32)time(nullptr);

        // Der Pfad wird alle fuenf Sekunden neu gelesen, damit "reload config"
        // reicht, um den Ticker an- oder auszuschalten.
        if (jetzt - s_cfgWhen >= 5)
        {
            s_cfgWhen = jetzt;
            s_pfad    = sConfig.GetStringDefault("LiveMap.File", "");
        }

        if (s_pfad.empty())
            return;

        std::string aus;

        aus.reserve(128 * 1024);

        char kopf[64];
        snprintf(kopf, sizeof(kopf), "{\"t\":%u,\"bots\":[", jetzt);
        aus += kopf;

        bool erster = true;

        // NICHT ueber die Sitzungsliste.
        //
        // sWorld.GetAllSessions() liefert die VERBINDUNGEN -- und ein Playerbot hat
        // keine. Bei 544 eingeloggten Figuren kam damit eine leere Liste heraus.
        // sObjectAccessor.GetPlayers() zaehlt dagegen, wer in der Welt steht, egal wer
        // ihn steuert; genau so macht es auch DcStrategyGate.
        for (auto const& kv : sObjectAccessor.GetPlayers())
        {
            Player* p = kv.second;

            if (!p || !p->IsInWorld())
                continue;

            const uint32 hmax = p->GetMaxHealth();
            const uint32 pmax = p->GetMaxPower(p->GetPowerType());

            Unit* ziel = p->GetVictim();

            char zeile[512];

            snprintf(zeile, sizeof(zeile),
                     "%s{\"n\":\"%s\",\"l\":%u,\"c\":%u,\"m\":%u,"
                     "\"x\":%.1f,\"y\":%.1f,\"z\":%.1f,"
                     "\"hp\":%u,\"pw\":%u,\"r\":%d,\"g\":%u,\"s\":%u,\"tg\":\"%s\"}",
                     erster ? "" : ",",
                     p->GetName(),
                     (uint32)p->GetLevel(),
                     (uint32)p->getClass(),
                     p->GetMapId(),
                     p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(),
                     hmax ? (uint32)(p->GetHealth() * 100 / hmax) : 0u,
                     pmax ? (uint32)(p->GetPower(p->GetPowerType()) * 100 / pmax) : 0u,
                     -1,                        // Rolle kennt dieser Server nicht
                     p->GetGroup() ? p->GetGroup()->GetId() : 0u,
                     !p->IsAlive() ? 0u : (p->IsInCombat() ? 2u : 1u),
                     ziel ? ziel->GetName() : "");

            aus += zeile;
            erster = false;
        }

        aus += "]}";

        // Unter einem Zwischennamen schreiben und umbenennen: so bekommt ein Leser
        // nie ein halbes Bild zu sehen.
        const std::string tmp = s_pfad + ".tmp";

        if (FILE* f = fopen(tmp.c_str(), "wb"))
        {
            fwrite(aus.data(), 1, aus.size(), f);
            fclose(f);
            rename(tmp.c_str(), s_pfad.c_str());
        }
    }
}

class LiveMapTickerScript : public PlayerScript
{
public:
    LiveMapTickerScript()
        : PlayerScript("LiveMapTickerScript", {
            PLAYERHOOK_ON_UPDATE
        }) {}

    void OnUpdate(Player* /*player*/, uint32 /*p_time*/) override
    {
        static uint32 s_letzte = 0;

        const uint32 jetzt = (uint32)time(nullptr);

        if (jetzt == s_letzte)
            return;

        s_letzte = jetzt;

        SchreibeLage();
    }
};

void AddSC_livemap_ticker()
{
    new LiveMapTickerScript();
}
