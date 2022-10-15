#define NOMINMAX
// ^Windows.h will #define over the names "min" and "max" (for any namespace!) if you don't define this

#ifndef RAPIDJSON_HAS_STDSTRING
#define RAPIDJSON_HAS_STDSTRING true
#endif

#include "include/cIGZApp.h"
#include "include/cIGZCheatCodeManager.h"
#include "include/cIGZFrameWork.h"
#include "include/cRZCOMDllDirector.h"
#include "include/cIGZMessage2.h"
#include "include/cIGZMessageServer2.h"
#include "include/cIGZMessage2Standard.h"
//#include "include/cISC4AdvisorSystem.h"
#include "include/cISC4App.h"
//#include "include/cISC4BuildingDevelopmentSimulator.h"
#include "include/cISC4City.h"
#include "include/cISC4Lot.h"
//#include "include/cISC4LotConfiguration.h"
//#include "include/cISC4LotConfigurationManager.h"
//#include "include/cISC4LotDeveloper.h"
//#include "include/cISC4LotManager.h"
#include "include/cISC4Occupant.h"
#include "include/cISC4OccupantManager.h"
#include "include/cISC4BuildingOccupant.h"
//#include "include/cISC4RegionalCity.h"
#include "include/cISCPropertyHolder.h"
#include "include/cRZMessage2COMDirector.h"
#include "include/cRZMessage2Standard.h"
#include "include/cRZBaseString.h"
#include "include/GZServPtrs.h"
//#include "include/SC4Rect.h"
//#include <list>
#include <string>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <format>
#include <regex>
#include <algorithm>
#include <random>
#include <chrono>
#include <cpr/cpr.h>
#include <rapidjson/document.h>
#include <rapidjson/pointer.h>

#ifndef SIG_KOPA
#define SIG_KOPA 0x4b4f5041
// ^ literally "KOPA" if little-endian, "APOK" if big-endian, I guess
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

namespace json = rapidjson;
namespace requests = cpr;           // lul, it's modeled after Python's requests, may as well call it that
using clock = std::chrono::steady_clock;
using dt = std::chrono::time_point<clock>;      // datetime
using td = std::chrono::duration<double, std::ratio<1>>;        // timedelta in seconds

using namespace std::literals;      // for duration literals e.g. 60s

// Random number generator wrapper
// Uses MT19937 (32-bit Mersenne Twister) with default settings, seeded by the system random device
// Occasionally re-seeds itself for good measure (interval hardcoded to 60 seconds right now)
// Can be re-seeded FROM SYSTEM DEVICE manually, cannot set actual seed manually
// Provides convenience functions for some types of random numbers
// This dll is set up with a global instance of this for the various name selectors to use
class kcRandom
{
public:
    kcRandom() 
    {
        seed_interval = 60s;
        seed();
    };

    double rand()
    {
        // return a double in the range of 0.0-1.0
        seed_maybe();
        return (double)mt_rand() / (double)mt_rand.max();
    }

    int rand_int()
    {
        seed_maybe();
        return mt_rand();
    }

    int rand_int(int max)
    {
        return rand_int(0, max);
    }

    int rand_int(int min, int max)
    {
        // with the default min of 0, this should be able to be used with any size() method to get a random index
        seed_maybe();
        return min + mt_rand() / ((mt_rand.max() + 1u) / (max - 1));
    }

    double rand_float(double max)
    {
        return rand_float(0.0, max);
    }

    double rand_float(double min, double max)
    {
        seed_maybe();
        return min + (double)mt_rand() / (((double)mt_rand.max() + 1.0) / max);
    }

    bool seed_maybe(td interval = 60s)
    {
        // check when last seeded and seed if more than interval has passed
        // returns true if seeded, false if not
        if (clock::now() - last_seeded > interval)
        {
            seed();
            return true;
        }
        else
        {
            return false;
        }
    }

    void seed()
    {
        mt_rand.seed(dev_rand());
        last_seeded = clock::now();
    }

private:
    dt last_seeded;
    td seed_interval;
    std::random_device dev_rand;
    std::mt19937 mt_rand;
};
static kcRandom g_random;       // global random instance

// Base class, but also handles plain string name choices
// Supports our name concatenation syntax by giving each line to be concatenated
// its own instance of this, which stores a pointer to itself in the kcNameChoice
// for the previous line, in its suffix variable.
class kcNameChoice
{
public:

    kcNameChoice* suffix;

    kcNameChoice() { }

    kcNameChoice(std::string iName)
    {
        data = iName;
        init_data = iName;
        name_resolved = true;
    }

    kcNameChoice(std::string iName, kcNameChoice* pref)
    {
        kcNameChoice(iName);
        pref->suffix = this;
    }

    std::string getName()
    {
        // base implementation just returns name stored in data
        // URL implementation will be more involved
        // this method should resolve the name expression, store the result
        // and set "name_resolved" to true if the name is stable and this step can be skipped until reload
        // you don't actually *have* to use the data member if the name resolution is always dynamic, but it *should* store the last result
        // errors in name resolution should generally return "" and skip the suffix, otherwise always add + getSuffix() to the return (it will return "" if there is no suffix)
        // the suffix should not be saved in data when resolving the name
        // a space is added before the suffix by getSuffix() if one exists and resolves to more than ""
        if (!name_resolved) name_resolved = true;
        return data + getSuffix();
    }

    cRZBaseString getSCName()
    {
        // return the name as a cRZ string for SimCity 4
        // if getName is implemented right, this shouldn't need to be overridden
        if (name_resolved) return cRZBaseString(data);
        else return cRZBaseString(getName());
    }

protected:
    std::string data;
    std::string init_data;
    bool name_resolved = false;

    std::string getSuffix()
    {
        if (!suffix)
        {
            // no suffix, nothing to be added
            return "";
        }

        std::string suff_result = suffix->getName();
        if (suff_result.length())
        {
            // insert a space at the beginning so it can be added to our data
            // this process should be chainable by making multiple chained ++ lines in the file
            suff_result.insert(0, 1, ' ');
            return suff_result;
        }
        else
        {
            return "";
        }
    }
};

// Name choice based on a URL+JSON selection:
// ~0x1001      ; Commercial
// >https://api.namefake.com/japanese-japan/random/      /company
// 
// Also supports selectors after the JSON pointer, by regex and by index+length:
// ~0x1000      ; Residential
// >https://api.namefake.com/        /name       ^.*\b(\w+)$
// ++Household      ; regex above should just get last name
//
// See the names file format for more details
class kcURLNameChoice : public kcNameChoice
{
public:

    bool use_regex = false;         // making this public so it could potentially be switched off in certain conditions

    kcURLNameChoice(std::string iName)
    {
        data = "";
        init_data = iName;
        name_resolved = false;

        // should get the URL, the JSON, and the optional regex or start and length specifiers, as well as optional comments
        std::regex namedef_parser(R"(^(\S+)\s+(#?\/\S+)\s*(\^.+?|\-?\d+(\s+|,)\d+)?\s*(;.*)?$)", std::regex::optimize);
        std::smatch mr;

        if (!iName.starts_with("http://") && !iName.starts_with("https://"))
        {
            // malformed URL, we'll just leave it empty
            DBGMSG(std::format("Malformed URL: {}", iName).c_str());
            name_resolved = true;
            return;
        }

        // split iName up to get the URL, json pointer, and the optional regex or start/length
        std::regex_match(iName, mr, namedef_parser);

        if (mr.size() < 2)
        {
            // didn't get at least a URL and JSON pointer
            DBGMSG(std::format("Malformed URL line, less than 2 matches: {}", iName).c_str());
            name_resolved = true;
            return;
        }
        else if (mr.size() > 2)
        {
            std::string locator = mr[2];
            char check_char = locator.front();
            if (check_char == '^')
            {
                result_regex = std::regex(locator, std::regex::optimize);
                use_regex = true;
            }
            else if (check_char == '-' || (0x30 <= check_char < 0x40))      // hopefully looking at a start_idx and length_spec
            {

                if (locator.contains(',')) num_fmt = "%d,%d";
                else num_fmt = "%d %d";

                std::sscanf(num_fmt, locator.c_str(), start_idx, length_spec);
            }
            // anything else is a comment or space, ignore it
        }

        url = requests::Url(mr[0]);
        json_pointer = json::Pointer(mr[1]);

    }

    std::string getName()
    {
        std::string this_name;
        requests::Response r = requests::Get(url);

        if (!(200 <= r.status_code < 400))
        {
            DBGMSG(std::format("HTTP error {}, URL {}", r.status_code, url).c_str());
            data = "";
            return "";           // HTTP error
        }
        last_response = r;

        json::Document r_json;
        r_json.Parse(r.text);

        json::Value* j_result = json_pointer.Get(r_json);
        if (!j_result)
        {
            DBGMSG(std::format("JSON pointer returned no result, URL <{}> Pointer {}", url, json_pointer).c_str());
            data = "";
            return "";           // JSON pointer returned no result
        }

        std::string sz_result = j_result->GetString();

        std::string final_result;
        if (use_regex)
        {
            std::smatch mr;
            std::regex_match(sz_result, mr, result_regex);
            if (mr.empty())
            {
                DBGMSG(std::format("Got no regex match from \"{}\" with r\"{}\"", sz_result, result_regex).c_str());
                data = "";
                return "";
            }
            else if (mr.size() == 1)
            {
                final_result = mr[0].str();
            }
            else
            {
                // get a random match group
                final_result = mr[g_random.rand_int(mr.size())].str();
            }
        }
        else
        {
            final_result = sz_result;
        }

        if (start_idx != 0 || length_spec != 0)
        {
            int this_length_spec;
            int this_start_idx;
            int result_len = final_result.length();
            if (length_spec < 0)
            {
                // reading backwards from the start point, reversing the result
                std::reverse(final_result.begin(), final_result.end());
                this_length_spec = length_spec * -1;
                if (start_idx < 0)      // negative, index from original end, now beginning
                {
                    int this_start_idx = start_idx * -1;
                    if (this_start_idx >= result_len) this_start_idx = result_len - 1;         // Negative index that would've been clamped to beginning is now actually past the end of the string
                    DBGMSG(std::format("Substring start (reading backwards, originally negative index) would be past end of string: \"{}\" [{}], length {}", final_result, start_idx, final_result.length()).c_str());
                    data = "";
                    return "";
                }
                else if (start_idx >= result_len)
                {
                    DBGMSG(std::format("Substring start (reading backwards, originally positive index) would be past end of string: \"{}\" [{}], length {}", final_result, start_idx, final_result.length()).c_str());
                    data = "";
                    return "";
                }
                else
                {
                    // was indexed from beginning, now indexed from end because the string is reversed
                    int this_start_idx = result_len - start_idx;
                    if (this_start_idx < 0) this_start_idx = 0;         // clamp to beginning (former end)
                }
            }
            else
            {
                this_length_spec = length_spec;
                if (start_idx < 0)      // negative, index from end
                {
                    int this_start_idx = result_len - start_idx;
                    if (this_start_idx < 0) this_start_idx = 0;         // index-from-end puts us back past the beginning, clamp to beginning
                }
                else if (start_idx >= result_len)
                {
                    DBGMSG(std::format("Substring start would be past end of string: \"{}\" [{}], length {}", final_result, start_idx, final_result.length()).c_str());
                    data = "";
                    return "";
                }
                else int this_start_idx = start_idx;
            }

            std::string ret = final_result.substr(this_start_idx, this_length_spec);
            data = ret;
            return ret + getSuffix();
        }
        else
        {
            data = final_result;
            return final_result + getSuffix();
        }
    }

private:
    requests::Url url;
    requests::Response last_response;
    json::Pointer json_pointer;
    std::regex result_regex;
    int start_idx = 0;
    int length_spec = 0;            // 0 should mean "remainder of string"
    const char* num_fmt;            // picks between using a comma or space to separate the start_idx and length_spec, used once
    const static std::regex namedef_parser;
};

// Base class
class kcLotNamerMatch
{
public:

    std::vector<kcNameChoice> names;

    kcLotNamerMatch() { };

    kcLotNamerMatch(std::vector<kcNameChoice> inames)
    {
        names = inames;
    }

    kcLotNamerMatch(uint16_t ipriority, std::vector<kcNameChoice> inames)
    {
        names = inames;             // initial names list
        priority = ipriority;       // initial priority
    }

    // return a random name from the list as a cRZ string
    cRZBaseString getOne()
    {
        return names[g_random.rand_int(names.size())].getSCName();      // C++ or Python? 🤔 
    }

    // same as above but a std::string
    std::string getOneStr()
    {
        return names[g_random.rand_int(names.size())].getName();
    }

    // return true if this should take priority over other
    // higher priority mod takes priority first
    // within same priority mod, lower regular priority takes precedent
    // if absolute priority is the same, returns false
    // each match definition should be given its own globally unique priority number totally independent of priority mod
    bool cmpPriority(kcLotNamerMatch* matchOther)
    {
        uint16_t otherPriorityMod = matchOther->priorityMod;
        if (priorityMod > otherPriorityMod) return true;
        if (priorityMod < otherPriorityMod) return false;

        // otherwise priority mod is the same, so we compare regular priority
        return priority < (matchOther->priority);       // harder for me to read without the parens because of the opposing angle brackets
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
};

// Match based on occupant group ID:
// ~0x1000      ; Residential
class kcOccGroupLotNamerMatch : public kcLotNamerMatch
{
public:

    kcOccGroupLotNamerMatch(uint32_t iOccupantGroup, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroup = iOccupantGroup;
    }

    kcOccGroupLotNamerMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::vector<kcNameChoice> inames)
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

// Match based on occupant name:
// ~Bob's Grease Pit
class kcStringLotNamerMatch : public kcLotNamerMatch
{
public:

    kcStringLotNamerMatch(std::string iMatchName, std::vector<kcNameChoice> inames)
    {
        names = inames;
        szMatchName = iMatchName;
    }

    kcStringLotNamerMatch(uint16_t ipriority, std::string iMatchName, std::vector<kcNameChoice> inames)
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

// Match based on occupant group ID && a partial regex match (regex search)
// ~0x1000
// ~%[^a-zA-Z0-9_\s]        ; match any Residential occupant with a non-word, non-whitespace character in its name
class kcPartialOccGroupRegexMatch : public kcLotNamerMatch
{
    // might not implement this with version 1
    // this is a version of the occupant group + regex match that doesn't require a full match (usually requiring at least a BOL anchor)
public:

    kcPartialOccGroupRegexMatch(uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroup = iOccupantGroup;
        rxMatchName = iMatchRegex;
    }

    kcPartialOccGroupRegexMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroup = iOccupantGroup;
        rxMatchName = iMatchRegex;      // might want to do iMatchRegex.erase(iMatchRegex.begin()) to get rid of initial ^ if using the same section header indicator
                                        // should probably just use a different indicator though, so a ^ can be seen as deliberate and preserved
                                        // how about %?
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

// Match based on occupant group ID && a complete regex match
// ~0x1000
// ~^.*\b[aA]partments?(?: [cC]omplex)?$|.*\b[tT]enements?(?: [cC]omplex)?$
// ; should match anything like "Apartment", "Apartment Complex", "Tenements", "Tenement Complex", etc.
class kcOccGroupRegexMatch : public kcLotNamerMatch
{
public:

    kcOccGroupRegexMatch(uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroup = iOccupantGroup;
        rxMatchName = iMatchRegex;
    }

    kcOccGroupRegexMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
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

// Match based on occupant group ID && exact, complete string match
// ~0x1000
// ~Cousin Bob's Place
class kcOccGroupStringMatch : public kcLotNamerMatch
{
public:

    kcOccGroupStringMatch(uint32_t iOccupantGroup, std::string iMatchString, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroup = iOccupantGroup;
        szMatchName = iMatchString;
    }

    kcOccGroupStringMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::string iMatchString, std::vector<kcNameChoice> inames)
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

class kcGZLotNamerPluginCOMDirector : public cRZMessage2COMDirector
{
public:

    kcGZLotNamerPluginCOMDirector() { }

    virtual ~kcGZLotNamerPluginCOMDirector() { }

    // We provide pretty much just our own interface on top of whatever cRZMessage2COMDirector does
    bool QueryInterface(uint32_t riid, void** ppvObj) {
        if (riid == GZCLSID::kcIGZMessageTarget2) {
            *ppvObj = static_cast<cIGZMessageTarget2*>(this);
            AddRef();
            return true;
        }
        else {
            return cRZMessage2COMDirector::QueryInterface(riid, ppvObj);
        }
    }

    uint32_t AddRef(void) {
        return cRZMessage2COMDirector::AddRef();
    }

    uint32_t Release(void) {
        return cRZMessage2COMDirector::Release();
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

    // Game should be loaded enough for us to do some of our setup, but no City yet
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

    // Process a GZMessage2 and dispatch other functions
    // On city init: get city and region pointers, initialize name matching lists, register cheat code
    // On cheat issued: check if it's ours, then randomize all RCI building names in the city not marked historical
    // On building occupant inserted: pass the occupant to the check function
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

    // right now this is just debugging stuff
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
        * with one or more spaces or tab characters, so the URL needs to already be percent-encoded. The JSON pointer should also be in URI fragment
        * encoding, also percent-encoded. The JSON pointer should therefore start with a hash symbol, but if it doesn't we will add one before
        * having rapidjson interpret it as a pointer, but it still needs the slash, the "reference token". Example:
        * 
        * >https://api.namefake.com/japanese-japan/random/      #/company
        * 
        * You can use the ++ line prefix to indicate two name lines should be concatenated. Behind the scenes, the lines will be stored
        * in the same object. For this version, this is only really useful with URL name lines. You can use this on the URL line itself 
        * to add a prefix to it (the preceding name line), or you can use it on the following line to add a suffix. When concatenating, 
        * a space will be added between, so no need to add your own. Examples:
        * 
        * House of
        * ++>https://api.namefake.com/      #/name
        * 
        * or
        * 
        * >https://api.namefake.com/        #/maiden_name
        * ++Household
        * 
        * You can further specify a slice of the reulting string from the JSON object, or (probably more useful) even a regex match.
        * This should be separated from the JSON pointer with one or more spaces or tab characters. If using regex, you must specify
        * a capture group. Like with a regex-based occupant name match, the operator for this (^) is included in the regex string,
        * so you MUST craft your regexes with that beginning anchor in mind. Examples:
        * 
        * ; get only the last name
        * >https://api.namefake.com/        #/name       ^.*\b(\w+)$
        * ++Household
        * 
        * ; get only first 10 characters
        * >https://api.namefake.com/        #/name       0   10
        * 
        * ; get only the last 10 characters
        * >https://api.namefake.com/        #/name       -10   10
        * 
        * ; get 7 characters in the middle starting with the 6th character (0-indexed)
        * >https://api.namefake.com/        #/name       5   7
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
