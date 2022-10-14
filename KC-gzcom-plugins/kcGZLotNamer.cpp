#include "include/cIGZApp.h"
#include "include/cIGZCheatCodeManager.h"
#include "include/cIGZFrameWork.h"
#include "include/cRZCOMDllDirector.h"
#include "include/cIGZMessage2.h"
#include "include/cIGZMessageServer2.h"
#include "include/cIGZMessage2Standard.h"
#include "include/cISC4AdvisorSystem.h"
#include "include/cISC4App.h"
#include "include/cISC4BuildingDevelopmentSimulator.h"
#include "include/cISC4City.h"
#include "include/cISC4Lot.h"
#include "include/cISC4LotConfiguration.h"
#include "include/cISC4LotConfigurationManager.h"
#include "include/cISC4LotDeveloper.h"
#include "include/cISC4LotManager.h"
#include "include/cISC4Occupant.h"
#include "include/cISC4OccupantManager.h"
#include "include/cISC4BuildingOccupant.h"
#include "include/cISC4RegionalCity.h"
#include "include/cISCPropertyHolder.h"
#include "include/cRZMessage2COMDirector.h"
#include "include/cRZMessage2Standard.h"
#include "include/cRZBaseString.h"
#include "include/GZServPtrs.h"
#include "include/SC4Rect.h"
#include <list>
#include <string>
#include <iostream>
#include <fstream>
#include <format>
#include <regex>
#include <algorithm>
#include <cpr/cpr.h>
#include <rapidjson/document.h>

#ifndef SIG_KOPA
#define SIG_KOPA 0x4b4f5041
#endif

#ifdef _DEBUG
#include <Windows.h>
#include "SG_InputBox.h"
#define DBGMSG(x) MessageBoxA(NULL, x, "Plugin Debug", MB_OK)
#define DBGINPUT(x) SG_InputBox::GetString("Plugin Debug", x)
#else
#define DBGMSG(x) std::cerr << x << std::endl
#define DBGINPUT(x) std::cin >> x
#endif

static const uint32_t kGZIID_cIGZCheatCodeManager = 0xa1085722;
static const uint32_t kGZIID_cISC4App = 0x26ce01c0;
static const uint32_t kGZIID_cISC4Occupant = 649819774;
static const uint32_t kGZIID_cISC4BuildingOccupant = 0x87dda3a9;
static const uint32_t kGZIID_cISCExemplarPropertyHolder = 180532727;
static const uint32_t kGZIID_cISCPropertyHolder = 622944899;

static const uint32_t kGZMSG_CheatIssued = 0x230e27ac;
static const uint32_t kGZMSG_CityInited = 0x26d31ec1;
static const uint32_t kGZMSG_MonthPassed = 0x66956816;
static const uint32_t kGZMSG_OccupantInserted = 0x99ef1142;
static const uint32_t kGZMSG_OccupantRemoved = 0x99ef1143;
static const uint32_t kSC4MessageInsertBuildingOccupant = 0x8B8B2CED;

static const uint32_t kSCPROP_ExemplarID = 0x21;
static const uint32_t kSCPROP_CityExclusionGroup = 0xea2e078b;
static const uint32_t kOccupantType_Building = 0x278128a0;
static const uint32_t kOccupantType_Default = 0x74758925;
static const uint32_t kSC4CLSID_cSC4BuildingOccupant = 0xA9BD882D;

static const uint32_t kKCLotNamerPluginCOMDirectorID = 0x44d0baa0;

static const uint32_t kKCCheatRandomizeNames = 0x44ccbaa0;
static const char* kszKCCheatRandomizeNames = "NameGame";

#ifdef _DEBUG
static const char* dbgOccupantLog = "C:\\occupants.log";
#endif

static const char* namesListFilename = "kcGZLotNamer_names.txt";
constexpr auto hexfmt = "{:#010x}";
#define HEXFMT(x) std::format("{:#010x}", x)

class kcLotNamerMatch
{
public:

    kcLotNamerMatch() { };

    kcLotNamerMatch(std::vector<std::string> inames)
    {
        names = inames;
    }

    kcLotNamerMatch(uint16_t ipriority, std::vector<std::string> inames)
    {
        names = inames;             // initial names list
        priority = ipriority;       // initial priority
    }

    bool cmpPriority(kcLotNamerMatch* matchOther)
    {
        // return true if this should take priority over other
 
        // higher priority mod takes priority first
        // within same priority mod, lower regular priority takes precedent
        // if absolute priority is the same, returns false
        // each match definition should be given its own globally unique priority number totally independent of priority mod
        uint16_t otherPriorityMod = matchOther->priorityMod;
        if (priorityMod > otherPriorityMod) return true;
        if (priorityMod < otherPriorityMod) return false;

        // otherwise priority mod is the same, so we compare regular priority
        return priority < matchOther->priority;
    }

    static bool cmp(kcLotNamerMatch* lhs, kcLotNamerMatch* rhs) 
    {
        // std::sort friendly compare
        return lhs->cmpPriority(rhs);
    }

    virtual bool checkMatch(cISC4Occupant* thisOccupant)
    {
        // Needs to be implemented by subclasses
        // Actually does the job of checking an SC4 occupant against the match definition
        // Return true if matching
    }

protected:
    const static uint16_t priorityMod = 0;      // meant to give certain classes of match absolute priority over others (e.g. due to specificity)
    uint16_t priority = 0;
    std::vector<std::string> names;
};

class kcOccGroupLotNamerMatch : public kcLotNamerMatch
{
public:

    kcOccGroupLotNamerMatch(uint32_t iOccupantGroup, std::vector<std::string> inames)
    {
        names = inames;
        dwOccupantGroup = iOccupantGroup;
    }

    kcOccGroupLotNamerMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::vector<std::string> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroup = iOccupantGroup;
    }

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        return thisOccupant->IsOccupantGroup(dwOccupantGroup);
    }

protected:
    const static uint16_t priorityMod = 1;
    uint32_t dwOccupantGroup;
};

//class kcRegexLotNamerMatch : public kcLotNamerMatch
//{
//public:
//
//    kcRegexLotNamerMatch(std::regex iMatchName, std::vector<std::string> inames)
//    {
//        names = inames;
//        rxMatchName = iMatchName;
//    }
//
//    kcRegexLotNamerMatch(uint16_t ipriority, std::regex iMatchName, std::vector<std::string> inames)
//    {
//        names = inames;
//        priority = ipriority;
//        rxMatchName = iMatchName;
//    }
//
//protected:
//    const static uint16_t priorityMod = 2;
//    std::regex rxMatchName;
//};

class kcStringLotNamerMatch : public kcLotNamerMatch
{
public:

    kcStringLotNamerMatch(std::string iMatchName, std::vector<std::string> inames)
    {
        names = inames;
        szMatchName = iMatchName;
    }

    kcStringLotNamerMatch(uint16_t ipriority, std::string iMatchName, std::vector<std::string> inames)
    {
        names = inames;
        priority = ipriority;
        szMatchName = iMatchName;
    }

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        cISC4BuildingOccupant* bOccupant;
        if (thisOccupant->QueryInterface(kGZIID_cISC4BuildingOccupant, (void**)&bOccupant))
        {
            cRZBaseString thisOccupantName;
            bOccupant->GetName(thisOccupantName);
            return thisOccupantName.CompareTo(cRZBaseString(szMatchName), true);        // let's try going case-insensitive for now
        }
        else
        {
            DBGMSG("Unable to get BuildingOccupant interface when expecting a building occupant in kcStringLotNamerMatch.checkMatch().");
            return false;
        }
    }

protected:
    const static uint16_t priorityMod = 8;
    std::string szMatchName;
};

class kcPartialOccGroupRegexMatch : public kcLotNamerMatch
{
    // might not implement with version 1
public:

    kcPartialOccGroupRegexMatch(uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<std::string> inames)
    {
        names = inames;
        dwOccupantGroup = iOccupantGroup;
        rxMatchName = iMatchRegex;
    }

    kcPartialOccGroupRegexMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<std::string> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroup = iOccupantGroup;
        rxMatchName = iMatchRegex;      // might want to do iMatchRegex.erase(iMatchRegex.begin()) to get rid of initial ^ if using the same section header indicator
                                        // should probably just use a different indicator though, so a ^ can be seen as deliberate and preserved
    }

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        cISC4BuildingOccupant* bOccupant;
        if (thisOccupant->QueryInterface(kGZIID_cISC4BuildingOccupant, (void**)&bOccupant))
        {
            cRZBaseString thisOccupantName;
            bOccupant->GetName(thisOccupantName);
            return std::regex_search(thisOccupantName.ToChar(), rxMatchName) && thisOccupant->IsOccupantGroup(dwOccupantGroup);
        }
        else
        {
            DBGMSG("Unable to get BuildingOccupant interface when expecting a building occupant in kcOccGroupRegexMatch.checkMatch().");
            return false;
        }
    }

protected:
    const static uint16_t priorityMod = 4;
    std::regex rxMatchName;
    uint32_t dwOccupantGroup;
};

class kcOccGroupRegexMatch : public kcLotNamerMatch
{
public:

    kcOccGroupRegexMatch(uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<std::string> inames)
    {
        names = inames;
        dwOccupantGroup = iOccupantGroup;
        rxMatchName = iMatchRegex;
    }

    kcOccGroupRegexMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<std::string> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroup = iOccupantGroup;
        rxMatchName = iMatchRegex;
    }

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        cISC4BuildingOccupant* bOccupant;
        if (thisOccupant->QueryInterface(kGZIID_cISC4BuildingOccupant, (void**)&bOccupant))
        {
            cRZBaseString thisOccupantName;
            bOccupant->GetName(thisOccupantName);
            return std::regex_match(thisOccupantName.ToChar(), rxMatchName) && thisOccupant->IsOccupantGroup(dwOccupantGroup);
        }
        else
        {
            DBGMSG("Unable to get BuildingOccupant interface when expecting a building occupant in kcOccGroupRegexMatch.checkMatch().");
            return false;
        }
    }

protected:
    const static uint16_t priorityMod = 16;     // leaves room for more classes between direct string and this combined match definition
    std::regex rxMatchName;
    uint32_t dwOccupantGroup;
};

class kcOccGroupStringMatch : public kcLotNamerMatch
{
public:

    kcOccGroupStringMatch(uint32_t iOccupantGroup, std::string iMatchString, std::vector<std::string> inames)
    {
        names = inames;
        dwOccupantGroup = iOccupantGroup;
        szMatchName = iMatchString;
    }

    kcOccGroupStringMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::string iMatchString, std::vector<std::string> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroup = iOccupantGroup;
        szMatchName = iMatchString;
    }

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        cISC4BuildingOccupant* bOccupant;
        if (thisOccupant->QueryInterface(kGZIID_cISC4BuildingOccupant, (void**)&bOccupant))
        {
            cRZBaseString thisOccupantName;
            bOccupant->GetName(thisOccupantName);

            // let's try going case-insensitive for now
            return thisOccupantName.CompareTo(cRZBaseString(szMatchName), true) && thisOccupant->IsOccupantGroup(dwOccupantGroup);
        }
        else
        {
            DBGMSG("Unable to get BuildingOccupant interface when expecting a building occupant in kcStringLotNamerMatch.checkMatch().");
            return false;
        }
    }

protected:
    const static uint16_t priorityMod = 17;
    std::string szMatchName;
    uint32_t dwOccupantGroup;
};

class kcNameChoice
{
public:
    kcNameChoice() { }

    kcNameChoice(std::string iName)
    {
        data = iName;
        init_data = iName;
        name_resolved = true;
    }

    std::string getName()
    {
        // base implementation just returns name stored in data
        // URL implementation will be more involved
        // this method should resolve the name expression, store the result
        // and set "name_resolved" to true if this step can be skipped until reload
        if (!name_resolved) name_resolved = true;
        return data;
    }

    cRZBaseString getSCName()
    {
        // if getName is implemented right, this shouldn't need to be overridden
        if (name_resolved) return cRZBaseString(data);
        else return cRZBaseString(getName());
    }

protected:
    std::string data;
    std::string init_data;
    bool name_resolved = false;
};

class kcURLNameChoice : public kcNameChoice
{
public:
    kcURLNameChoice(std::string iName)
    {
        data = "";
        init_data = iName;
        name_resolved = false;

        if (!iName.contains(""))
        { }
    }
};

class kcGZLotNamerPluginCOMDirector : public cRZMessage2COMDirector
{
public:

    kcGZLotNamerPluginCOMDirector() {
        // Initialize variables here.
        // We can create objects using this COM director by satisfying
        // requests to GZCOM::GetClassObject if the class ID matches
        // our own.
    }

    virtual ~kcGZLotNamerPluginCOMDirector() {
        // You could do something here, but everything except the OS is
        // dead by the time you get here.
    }

    bool QueryInterface(uint32_t riid, void** ppvObj) {
        if (riid == GZCLSID::kcIGZMessageTarget2) {
            *ppvObj = static_cast<cIGZMessageTarget2*>(this);
            AddRef();
            return true;
        }
        else {
            return cRZCOMDllDirector::QueryInterface(riid, ppvObj);
        }
    }

    uint32_t AddRef(void) {
        return cRZCOMDllDirector::AddRef();
    }

    uint32_t Release(void) {
        return cRZCOMDllDirector::Release();
    }

    uint32_t GetDirectorID() const {
        return kKCLotNamerPluginCOMDirectorID;
    }

    bool OnStart(cIGZCOM* pCOM) {
        // This automatically gets called by the framework after loading
        // the library.
        //
        // Register for callbacks so we can get PreFrameWorkInit,
        // PreAppInit, and other callbacks.
        cIGZFrameWork* const pFramework = RZGetFrameWork();
        if (pFramework) {
            if (pFramework->GetState() < cIGZFrameWork::kStatePreAppInit) {
                pFramework->AddHook(static_cast<cIGZFrameWorkHooks*>(this));
            }
            else {
                PreAppInit();
            }
        }

        return true;
    }

    bool PreFrameWorkInit(void) { return true; }
    bool PreAppInit(void) { return true; }

    bool PostAppInit(void) {
        // Basic app and framework setup, if we don't have these available we're screwed, so may as well check
        cIGZFrameWork* const pFramework = RZGetFrameWork();
        if (!pFramework)
        {
            DBGMSG("No framework.");
            return false;
        }

        cIGZApp* const pApp = pFramework->Application();
        if (!pApp)
        {
            DBGMSG("No app from framework.");
            return false;
        }

        cISC4App* pISC4App;
        if (!pApp->QueryInterface(kGZIID_cISC4App, (void**)&pISC4App))
        {
            DBGMSG("Got app from framework, but it doesn't support SC4App interface. Are you playing The Sims or SimCity 3000?");
            return false;
        }

        thisApp = pISC4App;

        // Set up data directories
        cRZBaseString userDataDir;
        pISC4App->GetUserDataDirectory(userDataDir);
        if (userDataDir.Strlen())
        {
            userDataDirectory = (std::string)userDataDir.ToChar();
        }

        cRZBaseString userPluginsDir;
        pISC4App->GetUserPluginDirectory(userPluginsDir);
        if (userPluginsDir.Strlen())
        {
            userPluginDirectory = (std::string)userPluginsDir.ToChar();
        }

        cRZBaseString regionDir;
        pISC4App->GetRegionsDirectory(regionDir);
        if (regionDir.Strlen())
        {
            userRegionsDirectory = (std::string)regionDir.ToChar();
        }

        // register for messages
        cIGZMessageServer2Ptr pMsgServ;
        if (pMsgServ) {
            pMsgServ->AddNotification(this, kGZMSG_CityInited);
            //pMsgServ->AddNotification(this, kGZMSG_MonthPassed);
            pMsgServ->AddNotification(this, kSC4MessageInsertBuildingOccupant);
            //pMsgServ->AddNotification(this, kGZMSG_OccupantRemoved);
        }

        return true;
    }

    bool PreAppShutdown(void) { return true; }
    bool PostAppShutdown(void) { return true; }
    bool PostSystemServiceShutdown(void) { return true; }
    bool AbortiveQuit(void) { return true; }
    bool OnInstall(void) { return true; }

    bool DoMessage(cIGZMessage2* pMessage) {
        cIGZMessage2Standard* pStandardMsg = static_cast<cIGZMessage2Standard*>(pMessage);
        uint32_t dwType = pMessage->GetType();
        std::string szMsgType = HEXFMT(dwType);

        //DBGMSG(szMsgType.c_str());

        if (dwType == kGZMSG_CityInited)
        {
            cISC4City* thisCity = thisApp->GetCity();
            cISC4Region* thisRegion = thisApp->GetRegion();
            if (InitializeNames() < 1)
            {
                DBGMSG("Failed to initialize names.");
                return false;
            }

            // Register cheats
            cIGZCheatCodeManager* pCheatMgr = thisApp->GetCheatCodeManager();
            if (pCheatMgr && pCheatMgr->QueryInterface(kGZIID_cIGZCheatCodeManager, (void**)&pCheatMgr)) {
                RegisterCheats(pCheatMgr);
            }
            else DBGMSG("Failed to get CheatCodeManager interface. This isn't a hard stop, but our cheats won't be registered.");
        }
        else if (dwType == kGZMSG_CheatIssued)
        {
            DBGMSG("Cheat issued.");
            uint32_t dwCheatID = pStandardMsg->GetData1();
            cIGZString* pszCheatData = static_cast<cIGZString*>(pStandardMsg->GetVoid2());
            std::string szCheatID = HEXFMT(dwCheatID);
            DBGMSG(szCheatID.c_str());
            DBGMSG(pszCheatData->ToChar());

            if (dwCheatID == kKCCheatRandomizeNames) RandomizeAll();
        }
        else if (dwType == kSC4MessageInsertBuildingOccupant)
        {
            //DBGMSG("Occupant Inserted. Getting occupant.");
            cISC4Occupant* pOccupant = (cISC4Occupant*)pStandardMsg->GetVoid1();
            if (!pOccupant || pOccupant->GetType() != kOccupantType_Building) return true;
            
            DoInsertBuildingOccupant(pOccupant);

            //LogInsertOccupant(bOccupant);
            // RandomizeOne(pOccupant);
        }

        return true;
    }

protected:

    std::map<int, std::vector<std::string>> namesByOccupantGroup;
    std::map<std::string, std::vector<std::string>> namesByDirectName;
    std::vector<int> occupantGroupPriorities;
    std::map<int, std::vector<std::string>> exclusionsByOccupantGroup;
    std::string userDataDirectory = "%USERPROFILE%\\Documents\\SimCity 4";
    std::string userPluginDirectory = "%USERPROFILE%\\Documents\\SimCity 4\\Plugins";
    std::string userRegionsDirectory = "%USERPROFILE%\\Documents\\SimCity 4\\Regions";
    cISC4App* thisApp;

    bool DoInsertBuildingOccupant(cISC4Occupant* pOccupant) {
        /*
        * Handle a new building occupant. Should ensure the passed Occupant is actually
        * a Building Occupant and then pass the original Occupant interface pointer
        * to the RandomizeOne() method. RandomizeOne() should get the Building Occupant
        * interface pointer again.
        */

        // This commented out stuff would be needed if we want to use the lot properties
        // instead of/in addition to the building properties
        /*cIGZFrameWork* const pFramework = RZGetFrameWork();
        if (!pFramework)
        {
            DBGMSG("No framework.");
            return false;
        }

        cIGZApp* const pApp = pFramework->Application();
        if (!pApp)
        {
            DBGMSG("No app from framework.");
            return false;
        }

        cISC4App* pISC4App;
        if (!pApp->QueryInterface(kGZIID_cISC4App, (void**)&pISC4App))
        {
            DBGMSG("Got app from framework, but it doesn't support SC4App interface.");
            return false;
        }

        cISC4City* pCity = pISC4App->GetCity();
        if (!pCity)
        {
            DBGMSG("No city.");
            return false;
        }

        cISC4LotManager* pLotManager = pCity->GetLotManager();
        if (!pLotManager)
        {
            DBGMSG("Have city, but no lot manager.");
            return false;
        }

        cISC4Lot* thisLot = pLotManager->GetOccupantLot(pOccupant);
        if (!thisLot)
        {
            DBGMSG("No occupant lot.");
            return false;
        }*/

        cISC4BuildingOccupant* bOccupant;
        if (pOccupant->QueryInterface(kGZIID_cISC4BuildingOccupant, (void**)&bOccupant))
        {
            cRZBaseString thisOccupantName;
            bOccupant->GetName(thisOccupantName);
            std::string bldgName = "";
            if (thisOccupantName.Strlen() > 0)
                bldgName = "'" + (std::string)thisOccupantName.ToChar() + "'";
            else
                bldgName = "<Empty building name>";
            DBGMSG(bldgName.c_str());
        }
        else
        {
            DBGMSG("Unable to get BuildingOccupant interface.");
            return false;
        }

        std::set<uint32_t> thisOccupantGroups;
        pOccupant->GetOccupantGroups(thisOccupantGroups);

        //cISCPropertyHolder* buildingPropertyHolder = pOccupant->AsPropertyHolder();

        return true;
    }

    void RegisterCheats(cIGZCheatCodeManager* pCheatMgr) {
        // The message ID parameter has no effect, so just pass zero like
        // the rest of the game does.
        pCheatMgr->AddNotification2(this, SIG_KOPA);

        cRZBaseString szCheatName(kszKCCheatRandomizeNames);
        pCheatMgr->RegisterCheatCode(kKCCheatRandomizeNames, szCheatName);
    }

    int InitializeNames()
    {
        // Should we use the plugin data directory instead of the user data directory?
        // That might allow other mods to add their own...
        /*
        * File format:
        * 
        * Each name on a new line. Sections specified by special characters. Section continues until a new section specifier or EOF.
        * Therefore,
        * Names cannot start with: ~, ~^, +, -, <, >, ;, or *
        * Names cannot contain: ;
        * 
        * The file is split by line, but afterwards whitespace is stripped before identification and processing.
        * 
        * Buildings which do not have any occupant groups listed in their exemplar that are defined in this file will
        * be ignored/excluded by default, unless they match a specific name. Chances of choosing a particular name can
        * be adjusted by listing that name multiple times in the same match section.
        * 
        * ~<uint32>			following names apply to occupant group <uint32> (follows stoui rules for base detection)
        * ~^<regex>			following names apply to occupant names matching the given regex, including the initial ^ (BOL anchor), requires an occupant group
        * ~<string>			following names apply to a specific building occupant name, overriding occupant groups (basically if no int or regex)
        * +<uint32>         following names apply to this occupant group in addition to previously set occupant group(s)
        * +<string>			following names apply to a specific building occupant name, added to currently set occupant groups
        * ++<string>        specified string should be added to previous name with a space between (mainly for use with results of URL lines)
        * -<uint32>			following building occupant names in this occupant group should be excluded from randomization
        * **				when used the within a -<uint32> header, indicates exclude ALL occupants with this group, even if
        *					they are in another defined group or have an exact name match
        * *<uint16_t>       add preceding name to the list the given number of times, for controlling probabilities, works with URL inputs as well
        * >https://url.com  get a URL and parse its contents as JSON. Will use the "name" attribute of the returned JSON by default, see "URL inputs" below
        * ;<string>			line comment. Can also go after data on a line, the rest of the line will be ignored.
        * **PRIORITIES**	(literal, not case-sensitive) following <uint32> lines define occupant group priorities 
        *					if a building occupant is in multiple defined groups. Specific original building occupant names
        *					that are defined with '~' always take priority over any occupant group definitions.
        * <example.txt		include another file at this point (note, see "**PRIORITIES**" below)
        * <!                do not include default files. Only valid within region folders because it's mainly only saying to skip:
        *                       the region file auto-include (if used in a city names file)
        *                       the plugins directory auto-includes
        *                       the main auto-include at the Documents\SimCity 4\ directory level, which by default includes another file containing SC4 default names
        *                   if this is used, then any other files you want to include must be done explicitly
        * 
        * There shall be a special case for a ~<uint32> line followed by a ~^<regex< or ~<string> line with no other lines between:
        * These will indicate the string or regex is subordinate to the given occupant group ID. For performance reasons, regex match
        * definitions must be paired with an occupant group ID. These always override a plain occupant group definition
        * or a plain string/regex definition. So if you have a section of names for occupant group 0x11010 (R$)
        * and a separate section of names for the string "Apartments", then such a combination section of 0x11010 and "Apartments"
        * will take precedence. If an occupant matches multiple such combination section specifiers, we should use the first matching
        * one that's loaded, unless **PRIORITIES** are set in any loaded file (see "**PRIORITIES**" below). For included files, that 
        * means the file that's including the other takes precedence. Within a file, a match definition that's earlier in the file 
        * will take precedence over one later in the file. Load order for auto-inclusion should go:
        * 
        * Region folder (region-specific names) > plugins folder (names added by other mods) > user data folder (global and default names)
        * Match definitions and names will be added to each other. Names can appear multiple times within a match definition to adjust
        * probabilities. If the same match definition is present in multiple files, their lists of names will simply be combined.
        * 
        * The match definition objects should be stored in a couple std::map objects keyed with the occupant group (uint32) and the
        * occupant name (std::string), each storing the appropriate types of match definition objects. Combined occupant group and
        * exact string (kcOccGroupStringMatch) goes in the occupant group map. We'll also have to keep track of exclusions mapped by
        * occupant group. If, while parsing the files, we encounter a completely excluded group with "**", then that's the only element
        * we need in that group's exclusion list (just assign, don't append).
        * 
        * So then for each building occupant we work on, we first check if it should be excluded and we make a list of match definitions to 
        * try. First we get the building's occupant groups. If any of those occupant groups are in the exclusion mapping, check if its
        * exclusion list is just {"**"} (exclude, return from the randomizeOne() function), and if not, then check if this occupant's name
        * is in any of those matching exclusion lists.
        * 
        * Next, for each of the building's occupant groups, we get the list of match definitions for it from that mapping, leaving out
        * group + name definitions that don't match this occupant's name. Next we add the match definition for the exact name, if one exists
        * in the mapping. Then we can sort the list of match definitions to try by priority by passing kcLotNamerMatch::cmp to std::sort, and
        * finally iterate through the list, calling checkMatch() until a match is found.
        * 
        * **PRIORITIES**:
        * ===============
        * This plugin also has the ability to specify that certain occupant groups or explicit names should take precedence over others 
        * regardless of load order. This changes the behavior of the priority assignment on load and check. As files are loaded, if a 
        * **PRIORITIES** section is encountered, it will be stored. Any **PRIORITIES** section found later in the load order will be ignored 
        * (since we're loading from most specific, city, to least specific, global). If a **PRIORITIES** section was found, then after all files 
        * are loaded the priorities of the listed match definitions will be reassigned, all placed before any match definitions not listed in the
        * **PRIORITIES** section. As part of this, match priority modifiers are also changed: 0x8000 (decimal 32768) will be added to definitions
        * listed in this section. You can use this to, for example, set a particular name or occupant group above all others in priority, or give
        * defined exact name matches higher priority than regex matches (which require combination with an occupant group, making them by default
        * more specific and higher priority. Note however, that regex matches will be included with their occupant group if it's listed in this
        * section, getting the same change to their priority modifiers. So that means that in this **PRIORITIES** section:
        * 
        * **PRIORITIES**
        * Cousin Vinnie's Place
        * 0x11010
        * 
        * The name list for the exact name match "Cousin Vinnie's Place" will override any regex match definitions that it also matches unless
        * the building belongs to occupant group 0x11010 (R$).
        * 
        * URL inputs:
        * ===========
        * A name starting with '>' indicates it should be parsed as a URL plus a JSON pointer. The JSON pointer should be separated from the URL
        * with one or more spaces or tab characters, so the URL needs to already be percent-encoded. Example:
        * 
        * >https://api.namefake.com/japanese-japan/random/      /company
        * 
        * You can use the ++ line prefix to indicate two name lines should be concatenated. Behind the scenes, the lines will be stored
        * in the same object. For this version, this is only really useful with URL name lines. You can use this on the URL line itself 
        * to add a prefix to it (the preceding name line), or you can use it on the following line to add a suffix. When concatenating, 
        * a space will be added between, so no need to add your own. Examples:
        * 
        * House of
        * ++>https://api.namefake.com/      /name
        * 
        * or
        * 
        * >https://api.namefake.com/        /maiden_name
        * ++Household
        * 
        * You can further specify a slice of the reulting string from the JSON object, or (probably more useful) even a regex match.
        * This should be separated from the JSON pointer with one or more spaces or tab characters. If using regex, you must specify
        * a capture group. Like with a regex-based occupant name match, the operator for this (^) is included in the regex string,
        * so you MUST craft your regexes with that beginning anchor in mind. Examples:
        * 
        * ; get only the last name
        * >https://api.namefake.com/        /name       ^.*\b(\w+)$
        * ++Household
        * 
        * ; get only first 10 characters
        * >https://api.namefake.com/        /name       0   10
        * 
        * ; get only the last 10 characters
        * >https://api.namefake.com/        /name       -10   10
        * 
        * ; get 7 characters in the middle starting with the 6th character (0-indexed)
        * >https://api.namefake.com/        /name       5   7
        * 
        * A negative number in the length of a string slice is invalid, but a negative number in the position indicates to start
        * that many characters from the end of the string.
        * 
        * If you're going to use a URL-based name definition to get random names alongside a list of names, you may want to control the
        * probabilities choosing one (or each) of your static names compared to grabbing one from the URL. This is done with the * line prefix
        * on the following line. Example:
        * 
        * ~0x11010      ; R$, low-wealth residential
        * ; get only the last name
        * >https://api.namefake.com/english-united-states/random        /name       ^.*\b(\w+)$
        * ++Household
        * *60        ; adds to the choice list 60 times
        * Cousin Bob's Place        ; adds to the choice list once
        * Humble Holt's Abode
        * Kilburn Kottage
        * Crib Terrace
        * *5
        * Hi-Life Hut
        * *5
        * >https://api.namefake.com/german_germany/random        /name       ^.*\b(\w+)$        ; namefake.com is inconsistent with their URLs, not me
        * ++Haus
        * *10
        * >https://api.namefake.com/german_austria/random        /name       ^.*\b(\w+)$
        * ++Haus
        * *2
        * Casa de
        * ++>https://api.namefake.com/spanish-spain/random        /name       ^.*\b(\w+)$
        * *15
        * 
        * This example should have 100 total choices for the specified occupant group with the following probabilities: 60% American English surname
        * plus "Household", 12% a Germanic (German or Austrian) surname plus "Haus", 15% the words "Casa de" plus a Spanish surname, and 1% chance each
        * of three different names from the base (deluxe?) game.
        * 
        * To do in a version 2.0?:
        * Be able to split this file up by including named files in a base file. Will allow to separate the game's
        *   default set of names into its own file.
        * Be able to do this per-region, maybe after/as a city loads (on city loaded/load message?)
        * ...eh, might add this into version 1.0 anyway as long as I've got it planned out well enough.
        * 
        * e.g. (for v. 2.0):
        *	kcGZLotNamer_global.txt and kcGZLotNamer_default.txt in Documents\SimCity 4
        *	kcGZLotNamer_names.txt in each Documents\SimCity 4\Regions\... folder, optionally including the default.txt
        *	global can be empty, as any definitions present will be added to the region's file automatically without explicit inclusion
        *	This method should keep track of included files and prevent duplication
        *	The only directories that should be checked for included filenames are the UserDataDirectory and the current Region's directory
        *	Maybe if a file is present named with the current city's name, we load that automatically first?
        *	That would allow even more granularity!
        * 
        * To do in a version 3.0:
        * Be able to do more advanced filtering both by name (regex?) and by other exemplar properties
        * ...actually regex might be easier than I initially thought to implement with names, maybe even easier than globbing
        * 
        * How to:
        *	1. Enumerate files to read
        *	2. Open file for reading, and fgets() on it in a loop until EOF
        *		i.		Trim whitespace from both sides
        *		ii.		Check starting characters for section change
        *		iii.	Change section context and section key variables when that happens
        *		iv.		Use section context and key to add lines to appropriate list if no special char match
        */
    }

    void RandomizeAll() { }

    void RandomizeOne(cISC4Occupant* pOccupant) { }

    void LogBuildingOccupant(cISC4BuildingOccupant* thisBldgOccupant) 
    {
        DBGMSG("In LogBuildingOccupant()");
        // first get the building name
        cRZBaseString thisOccupantName;
        thisBldgOccupant->GetName(thisOccupantName);
        const char* bldgName = "";
        if (thisOccupantName.Strlen() > 0)
            const char* bldgName = thisOccupantName.ToChar();
        else
            const char* bldgName = "<Empty building name>";

        DBGMSG(bldgName);

        // let's try and output the hex building type too

        const std::string thisBldgTypeID = std::format(hexfmt, thisBldgOccupant->GetBuildingType());
        DBGMSG(thisBldgTypeID.c_str());

        std::ofstream txtOccupantLog;
        DBGMSG("Opening log file...");
        txtOccupantLog.open(dbgOccupantLog, std::ios::app);
        txtOccupantLog << thisBldgTypeID << " -- " << thisOccupantName.ToChar() << std::endl;
        txtOccupantLog.close();
        DBGMSG("Wrote to log.");
    }
};


cRZCOMDllDirector* RZGetCOMDllDirector() {
    static kcGZLotNamerPluginCOMDirector sDirector;
    return &sDirector;
}
