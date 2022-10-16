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
#include "include/cISC4Region.h"
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
#include <set>
#include <string>
#include <locale>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <filesystem>
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

static const uint32_t MATCHTGT_OCCGROUP = 1;        // MATCHTGT = the target mapping to add this definition to (the important one)
static const uint32_t MATCHTGT_NAME = 1 << 1;
static const uint32_t MATCHTYPE_INT = 1 << 15;      // MATCHTYPE = the main type of match being made (not used)
static const uint32_t MATCHTYPE_STRING = 1 << 16;
static const uint32_t MATCHTYPE_REGEX = 1 << 17;
static const uint32_t MATCHMOD_EXCEPTIONS = 1 << 30;    // MATCHMOD = any modifier signifying flags (not used)
static const uint32_t MATCHMOD_PARTIAL = 1 << 31;   

static const char* kCitySpecialFileName = "??CITY";     // allows plugin-included files with same name as the city names def file
static const char* kRegionSpecialFileName = "??REGION";
static const char* kDefaultSpecialFileName = "??DEFAULT";

#ifdef _DEBUG
static const char* dbgOccupantLog = "C:\\occupants.log";
#endif

static const char* namesListFilename = "kcGZLotNamer_names.txt";
static const char* DOTNAMES = ".namedef";       // file extension for our name definition files--this is easier than the prefix idea I think
constexpr auto hexfmt = "{:#010x}";
#define HEXFMT(x) std::format("{:#010x}", x)

namespace json = rapidjson;
namespace requests = cpr;           // lul, it's modeled after Python's requests, may as well call it that
namespace pathlib = std::filesystem;
using clock = std::chrono::steady_clock;
using dt = std::chrono::time_point<clock>;      // datetime
using td = std::chrono::duration<double, std::ratio<1>>;        // timedelta in seconds

using namespace std::literals;      // for duration literals e.g. 60s

// easier generic contains on a vector
template<class T, class V = std::vector<T>>
bool kContains(V& haystack, T& needle, bool sensitive = true)
{
    if (sensitive)
    {
        return std::any_of(haystack.begin(), haystack.end(), [needle](T& x) { return x == needle; });
    }
    else
    {
        l = std::locale();
        return std::any_of(haystack.begin(), haystack.end(), [needle](T& x) {return std::toupper(x, l) == std::toupper(needle, l); });
    }
}

// likewise for extending a vector
template<class T>
void iextend(std::vector<T>& first, std::vector<T>& other)
{
    std::for_each(other.begin(), other.end(), [first](T o) { first.push_back(o); });
}

template<class T>
std::vector<T> extend(std::vector<T>& first, std::vector<T>& other)
{
    std::vector<T> new_first = first;
    iextend(new_first, other);
}

//using std::vector::contains;

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

    static const uint32_t match_flags = 0x00;

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

    void addOne(kcNameChoice &nc)
    {
        names.push_back(nc);
    }

    void addMany(std::vector<kcNameChoice>& ncs)
    {
        std::for_each(ncs.begin(), ncs.end(), [this](kcNameChoice& nc) { addOne(nc); });
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
    std::vector<kcNameChoice> names;
};

// Match based on occupant group ID:
// ~0x1000      ; Residential
class kcOccGroupLotNamerMatch : public kcLotNamerMatch
{
public:

    static const uint32_t match_flags = MATCHTGT_OCCGROUP | MATCHTYPE_INT;

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

    static const uint32_t match_flags = MATCHTGT_NAME | MATCHTYPE_STRING;

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

    const static uint32_t match_flags = MATCHTGT_OCCGROUP | MATCHTYPE_REGEX | MATCHMOD_PARTIAL;

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
        if (!thisOccupant->IsOccupantGroup(dwOccupantGroup)) return false;

        cISC4BuildingOccupant* bOccupant;
        if (thisOccupant->QueryInterface(kGZIID_cISC4BuildingOccupant, (void**)&bOccupant))
        {
            cRZBaseString thisOccupantName;
            bOccupant->GetName(thisOccupantName);
            return std::regex_search(thisOccupantName.ToChar(), rxMatchName);
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

    const static uint32_t match_flags = MATCHTGT_OCCGROUP | MATCHTGT_NAME | MATCHTYPE_REGEX;

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
        if (!thisOccupant->IsOccupantGroup(dwOccupantGroup)) return false;

        cISC4BuildingOccupant* bOccupant;
        if (thisOccupant->QueryInterface(kGZIID_cISC4BuildingOccupant, (void**)&bOccupant))
        {
            cRZBaseString thisOccupantName;
            bOccupant->GetName(thisOccupantName);
            return std::regex_match(thisOccupantName.ToChar(), rxMatchName);
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

    const static uint32_t match_flags = MATCHTGT_OCCGROUP | MATCHTGT_NAME | MATCHTYPE_STRING;

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
        if (!thisOccupant->IsOccupantGroup(dwOccupantGroup)) return false;

        cISC4BuildingOccupant* bOccupant;
        if (thisOccupant->QueryInterface(kGZIID_cISC4BuildingOccupant, (void**)&bOccupant))
        {
            cRZBaseString thisOccupantName;
            bOccupant->GetName(thisOccupantName);

            // let's try going case-insensitive for now
            return thisOccupantName.CompareTo(cRZBaseString(szMatchName), true);
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

// This class, instead of using the names variable to return a name, uses it in checkMatch()
// getOne() and getOneStr() are overridden to return an empty cRZBaseString and an empty std::string, respectively
class kcLotNamerExclusions : public kcLotNamerMatch
{
public:
    std::vector<std::string> names;
    const static uint32_t match_flags = MATCHTGT_OCCGROUP | MATCHMOD_EXCEPTIONS;

    kcLotNamerExclusions(uint32_t iOccupantGroup, std::vector<std::string> inames)
    {
        //if (std::any_of(inames.begin(), inames.end(), [](std::string n) { return n == "**"; }))
        if (kContains(inames, "**"))
        {
            names = std::vector { std::string("**") };
            match_all = true;
        }
        else
        {
            names = inames;
        }
        dwOccupantGroup = iOccupantGroup;
    }

    cRZBaseString getOne() { return cRZBaseString(""); }
    std::string getOneStr() { return ""; }

    void addOne(std::string& nc)
    {
        if (match_all) return;

        if (nc == "**")
        {
            names = std::vector{ std::string("**") };
            match_all = true;
        }
        else
        {
            names.push_back(nc);
        }
    }

    void addMany(std::vector<std::string>& ncs)
    {
        if (match_all) return;

        iextend(names, ncs);
    }

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        if (!thisOccupant->IsOccupantGroup(dwOccupantGroup)) return false;

        if (match_all) return true;

        cISC4BuildingOccupant* bOccupant;
        std::string this_name;
        if (thisOccupant->QueryInterface(kGZIID_cISC4BuildingOccupant, (void**)&bOccupant))
        {
            cRZBaseString thisOccupantName;
            bOccupant->GetName(thisOccupantName);

            this_name = thisOccupantName.ToChar();
        }
        else
        {
            DBGMSG("Unable to get BuildingOccupant interface when expecting a building occupant in kcLotNamerExclusions.checkMatch().");
            return false;
        }

        return kContains(names, this_name, false);

    }

protected:
    const static uint16_t priorityMod = 0;
    uint32_t dwOccupantGroup;
    bool match_all = false;     // match all names in this occ group
};

class kcGZLotNamerPluginCOMDirector : public cRZMessage2COMDirector
{
public:

    pathlib::path userDataDirectory = "%USERPROFILE%\\Documents\\SimCity 4";
    pathlib::path userPluginDirectory = "%USERPROFILE%\\Documents\\SimCity 4\\Plugins";
    pathlib::path userRegionsDirectory = "%USERPROFILE%\\Documents\\SimCity 4\\Regions";
    pathlib::path currentRegionDirectory;
    std::string currentCityName;

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
            userDataDirectory = pathlib::canonical(userDataDir.ToChar());
        }

        cRZBaseString userPluginsDir;
        pISC4App->GetUserPluginDirectory(userPluginsDir);
        if (userPluginsDir.Strlen())
        {
            userPluginDirectory = pathlib::canonical(userPluginsDir.ToChar());
        }

        cRZBaseString regionDir;
        pISC4App->GetRegionsDirectory(regionDir);
        if (regionDir.Strlen())
        {
            userRegionsDirectory = pathlib::canonical(regionDir.ToChar());
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
            // City loaded, lets initialize our names
            cISC4City* thisCity = thisApp->GetCity();
            cISC4Region* thisRegion = thisApp->GetRegion();
            pathlib::path regDirName = thisRegion->GetDirectoryName();
            currentRegionDirectory = pathlib::canonical(userRegionsDirectory / regDirName);
            cRZBaseString citName;
            thisCity->GetCityName(citName);
            currentCityName = citName.ToChar();
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

    std::map<int, std::vector<kcLotNamerMatch>> namesByOccupantGroup;
    std::map<std::string, std::vector<kcLotNamerMatch>> namesByDirectName;
    std::vector<int> occupantGroupPriorities;
    std::map<int, kcLotNamerExclusions> exclusionsByOccupantGroup;
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
        /*
        * 
        * To do:
        *   0. Initialize local copies of our main map variables
        *	1. Enumerate files to read
        *       i.      Auto load <currentCityName>.namedef if it exists, plus any direct includes
        *       ii.     Auto load <currentRegionDirectory>/region.namedef if it exists, plus any direct includes, unless excluded
        *       iii.    Auto load any *.namedef in <userPluginsDirectory> if they exist, plus any direct includes, unless excluded
        *       iv.     Auto load <userDataDirectory>/default.namedef if it exists, plus any direct includes, unless excluded
        *           Nb. 1) We should keep track of loaded filenames to make sure they're only loaded once
        *           Nb. 2) Mod authors should use includes in <userPluginsDirectory> for items in subdirectories, which otherwise wouldn't be auto-included
        *           Nb. 3) Users are encouraged to rename our released <userDataDirectory>/default.namedef to e.g. built-in.namedef and include it (or not)
        *                   from their own default.namedef or custom *.namedef if they want to define their own global set of names
        *           Nb. 4) Included files follow a sort of namespace structure... they can only access files from their own directory, subdirectories, or
        *                   the UserDataDirectory as a fallback. A city/region level namedef file can override files included by plugins by including a file
        *                   with the same include name. That does mean subdirectories, too, though. So Regions\Berlin\region.namedef would have to include
        *                   e.g. foo/bar = Regions\Berlin\foo\bar.namedef to override a plugin including foo/bar = Plugins\foo\bar.namedef.
        *   2. Initialize file-level tracking variables
        *	3. Open file for reading, and fgets() on it in a loop until EOF
        *		i.		Trim whitespace from both sides (including newlines)
        *		ii.		Check starting characters in if-elseif-else tree
        *		iii.	Change local section context and section key variables as needed (swapping out with the local map variables as needed)
        *		iv.		Use section context and key to add lines to appropriate list if no special char match
        */

        std::map<int, kcLotNamerExclusions> exclusions_map;
        std::map<std::string, std::vector<kcLotNamerMatch>> namesByName_map;
        std::map<int, std::vector<kcLotNamerMatch>> namesByGroup_map;

        std::unordered_map<std::string, pathlib::path> includeFiles_map;      // maps requested include file name with found include file path
        std::vector<std::string> includeFiles_list;         // just lists requested include file names in load order for enumeration of the map

        // check city first
        pathlib::path cityNames_path = currentRegionDirectory / (currentCityName + DOTNAMES);
        if (pathlib::is_regular_file(cityNames_path))
        {
            includeFiles_map["??CITY"] = cityNames_path;
            
        }
    }

    std::vector<std::string> checkIncludes(pathlib::path this_filep)
    {
        // read a file, but just check for includes for now, we'll parse the namedefs on second pass
    }

    void checkIncludesRecursive(pathlib::path this_filep, std::unordered_map<std::string, pathlib::path> &inc_map)
    {
        // TODO: When you went to bed, you were trying to figure out how to properly implement this recursive include file search
        std::vector<std::string> this_includes = checkIncludes(this_filep);
        if (this_includes.size())
        {
            // resolve included file names to canonical paths
            for (auto& inc : this_includes)
            {
                if (!inc_map.contains(inc))
                {
                    pathlib::path res_inc = resolveInclude(currentRegionDirectory, inc);
                    inc_map[inc] = res_inc;

                    // aaaaand recurse! Shouldn't be any risk of an infinite loop since we're always checking if we've already added a particular include
                    checkIncludesRecursive(res_inc, inc_map);
                }
            }
        }
    }

    pathlib::path resolveInclude(pathlib::path folder, std::string include_name)
    {
        // get canonical path for an include name in the given namespace/folder
        
    }

    std::vector<kcLotNamerMatch> parseOne(pathlib::path this_filep)
    {
        // read a file and a return a vector of name match definitions to be sorted by the caller based on their flags
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
