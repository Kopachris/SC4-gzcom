﻿#define NOMINMAX
// ^Windows.h will #define over the names "min" and "max" (for any namespace!) if you don't define this

#ifndef RAPIDJSON_HAS_STDSTRING
#define RAPIDJSON_HAS_STDSTRING true
#endif

/*************************************
* TO DO LIST: (what's left?)
* --------------------------
* 
* Actually implement RandomizeOne()
* Then implement RandomizeAll()
* Add debug statements
* Review code again
* Compile, fix stuff that's keeping it from compiling
* Test, fix stuff that goes wrong in testing
* Clean up code formatting (rename a lot of this shit so it makes more sense) and make a new VS solution for this, new GitHub repo
* Maybe move some utility stuff into their own files, or combine with the strtrim thing and throw those in their own repo too
**************************************/

// standard library includes
#include <set>
#include <string>
#include <locale>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <format>
#include <functional>
#include <regex>
#include <algorithm>
#include <random>
#include <chrono>

// GZCOM framework includes
#include "include/cIGZApp.h"
#include "include/cIGZCheatCodeManager.h"
#include "include/cIGZFrameWork.h"
#include "include/cRZCOMDllDirector.h"
#include "include/cIGZMessage2.h"
#include "include/cIGZMessageServer2.h"
#include "include/cIGZMessage2Standard.h"
#include "include/cISC4App.h"
#include "include/cISC4City.h"
#include "include/cISC4Region.h"
#include "include/cISC4Lot.h"
#include "include/cISC4Occupant.h"
#include "include/cISC4OccupantManager.h"
#include "include/cISC4BuildingOccupant.h"
#include "include/cISCPropertyHolder.h"
#include "include/cRZMessage2COMDirector.h"
#include "include/cRZMessage2Standard.h"
#include "include/cRZBaseString.h"
#include "include/GZServPtrs.h"

// 3rd-party library includes
#include <cpr/cpr.h>
#include <rapidjson/document.h>
#include <rapidjson/pointer.h>

// personal and project includes
#include "strtrim.h"

//==============================
//*********END INCLUDES*********
//==============================

// I'll probably get rid of this
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

// constants
static const uint32_t kKCLotNamerPluginCOMDirectorID = 0x44d0baa0;

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
static const uint32_t kOccupantType_Building = 0x278128a0;
static const uint32_t kOccupantType_Default = 0x74758925;
static const uint32_t kSC4CLSID_cSC4BuildingOccupant = 0xA9BD882D;

static const uint32_t kKCCheatRandomizeNames = 0x44ccbaa0;
static const char* kszKCCheatRandomizeNames = "NameGame";

static const uint16_t MATCHTYPE_INT = 1;      // MATCHTYPE = the main type of match being made
static const uint16_t MATCHTYPE_STRING = 1 << 1;
static const uint16_t MATCHTYPE_REGEX = 1 << 2;
static const uint16_t MATCHMOD_EXCEPTIONS = 1 << 15;    // MATCHMOD = any modifier signifying flags
static const uint16_t MATCHMOD_PARTIAL = 1 << 16;   

static const char* kCitySpecialFileName = "??CITY";     // allows plugin-included files with same name as the city names def file
static const char* kRegionSpecialFileName = "??REGION";
static const char* kDefaultSpecialFileName = "??DEFAULT";
static const char* kInvalidNamesFile = "??INVALIDFILE";     // tried to read as .namedef file but file was invalid for some reason
static const char* kDisabledIncludes = "??NOINCLUDES";      // auto-inclusions have been disabled, explicit inclusions only from now on

#ifdef _DEBUG
static const char* dbgOccupantLog = "C:\\occupants.log";
#endif

static const char* DOTNAMES = ".namedef";       // file extension for our name definition files--this is easier than the prefix idea I think

// a couple utility things
constexpr auto hexfmt = "{:#010x}";
#define HEXFMT(x) std::format("{:#010x}", x)

// Namespaces
namespace json = rapidjson;
namespace requests = cpr;           // lul, it's modeled after Python's requests, may as well call it that
namespace pathlib = std::filesystem;
using clock = std::chrono::steady_clock;
using dt = std::chrono::time_point<clock>;      // datetime
using td = std::chrono::duration<double, std::ratio<1>>;        // timedelta in seconds

using namespace std::literals;      // for duration literals e.g. 60s
using namespace strtrim;            // for string trimming helpers

// easier generic contains on a vector -- DON'T PASS sensitive=false UNLESS YOU CAN CALL std::toupper() ON IT
// the == operator on kcLotNamerMatch classes checks against target, flags, key, and priority modifier, but
// not the lower half of the priority or the actual list of names
// the point being if we've already got a match def with all those parameters, we don't need to make another
// and can just add the new list of names to the existing match def
template<class T, class V = std::vector<T>>
bool kContains(V& h, T& n, bool sensitive = true)
{
    if (sensitive)
    {
        return std::any_of(h.begin(), h.end(), [&](T& x) { return x == n; });
    }
    else
    {
        l = std::locale();
        return std::any_of(h.begin(), h.end(), [&](T& x) { return std::toupper(x, l) == std::toupper(n, l); });
    }
}

// easier generic find index on a vector -- DON'T PASS sensitive=false UNLESS YOU CAN CALL std::toupper() ON IT
template<class T, class V = std::vector<T>>
int kFind(V& h, T& n, bool sensitive = true)
{
    if (sensitive)
    {
        std::random_access_iterator res_it = std::find(h.begin(), h.end(), n);
        if (res_it != h.end())
        {
            return res_it - h.begin();
        }
        else
        {
            return -1;
        }
    }
    else
    {
        l = std::locale();
        std::random_access_iterator res_it = std::find(h.begin(), h.end(), [&](T& x) { return std::toupper(x, l) == std::toupper(n, l); });
        if (res_it != h.end())
        {
            return res_it - h.begin();
        }
        else
        {
            return -1;
        }
    }
}

// easier generic find and get pointer to existing element on a vector -- DON'T PASS sensitive=false UNLESS YOU CAN CALL std::toupper() ON IT
template<class T, class V = std::vector<T>>
T* kGetItem(V& h, T& n, bool sensitive = true)
{
    res_idx = kFind(h, n, sensitive);
    if (res_idx > -1)
    {
        return &h[res_idx];
    }
    else
    {
        return nullptr;
    }
}

// likewise for extending a vector
template<class T>
void iextend(std::vector<T>& first, std::vector<T>& other)
{
    for (T o : other) first.push_back(o);
}

template<class T>
std::vector<T> extend(std::vector<T>& first, std::vector<T>& other)
{
    std::vector<T> new_first = first;
    iextend(new_first, other);
    return new_first;
}

bool get_uint(std::string s, uint32_t* i)
{
    // try to get an unsigned integer from s, putting it in i
    // returns true if successful, false if no integer found
    // just a try { } wrapper around stoul() with any base set, but more convenient to write since I'll be using it a lot
    try
    {
        *i = std::stoul(s, nullptr, 0);
        return true;
    }
    catch (std::invalid_argument&) { return false; }
    catch (std::out_of_range&) { return false; }
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

    kcURLNameChoice(std::string iName, kcNameChoice* pref)
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
        pref->suffix = this;
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
                    this_start_idx = start_idx * -1;
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
                    this_start_idx = result_len - start_idx;
                    if (this_start_idx < 0) this_start_idx = 0;         // clamp to beginning (former end)
                }
            }
            else if (length_spec > 0)
            {
                this_length_spec = length_spec;
                if (start_idx < 0)      // negative, index from end
                {
                    this_start_idx = result_len - start_idx;
                    if (this_start_idx < 0) this_start_idx = 0;         // index-from-end puts us back past the beginning, clamp to beginning
                }
                else if (start_idx >= result_len)
                {
                    DBGMSG(std::format("Substring start would be past end of string: \"{}\" [{}], length {}", final_result, start_idx, final_result.length()).c_str());
                    data = "";
                    return "";
                }
                else this_start_idx = start_idx;
            }
            else
            {
                // length_spec is 0, return entire string from start idx
                // still gotta handle negative start index
                this_length_spec = result_len;
                if (start_idx < 0)      // negative, index from end
                {
                    this_start_idx = result_len - start_idx;
                    if (this_start_idx < 0) this_start_idx = 0;         // index-from-end puts us back past the beginning, clamp to beginning
                }
                else if (start_idx >= result_len)
                {
                    DBGMSG(std::format("Substring start would be past end of string: \"{}\" [{}], length {}", final_result, start_idx, final_result.length()).c_str());
                    data = "";
                    return "";
                }
                else this_start_idx = start_idx;
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

    enum MatchTarget { occGroup, occName };

    static const uint16_t match_flags = 0x00;
    static const MatchTarget match_target;

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

    static inline bool operator==(kcLotNamerMatch& lhs, kcLotNamerMatch& rhs)
    {
        return (lhs.match_flags == rhs.match_flags) && 
            (lhs.match_target == rhs.match_target) && 
            (lhs.priorityMod == rhs.priorityMod) &&
            (lhs.getKey() == rhs.getKey());
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
        // this one may be overridden to handle any validation needed
        names.push_back(nc);
    }

    void addMany(std::vector<kcNameChoice>& ncs)
    {
        // this one should be fine if addOne is implemented right
        for (kcNameChoice& nc : ncs) addOne(nc);
    }

    void addMany(kcLotNamerMatch& other)
    {
        for (kcNameChoice& nc : other.names) addOne(nc);
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

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        // Needs to be implemented by subclasses
        // Actually does the job of checking an SC4 occupant against the match definition
        // Return true if matching
    }

    std::set<uint32_t> getKey() { }

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

    static const uint16_t match_flags = MATCHTYPE_INT;
    static const MatchTarget match_target = occGroup;

    kcOccGroupLotNamerMatch(uint32_t iOccupantGroup, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroups = { iOccupantGroup };
    }

    kcOccGroupLotNamerMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::vector<kcNameChoice> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroups = { iOccupantGroup } ;
    }

    kcOccGroupLotNamerMatch(std::set<uint32_t> iOccupantGroups, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroups = iOccupantGroups;

    }

    kcOccGroupLotNamerMatch(uint16_t ipriority, std::set<uint32_t> iOccupantGroups, std::vector<kcNameChoice> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroups = iOccupantGroups;
    }

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        for (auto& og : dwOccupantGroups)
        {
            if (!thisOccupant->IsOccupantGroup(og)) return false;
        }
        return true;
    }

    std::set<uint32_t> getKey()
    {
        return dwOccupantGroups;
    }

protected:
    const static uint16_t priorityMod = 1;
    std::set<uint32_t> dwOccupantGroups;
};

// Match based on occupant name:
// ~Bob's Grease Pit
class kcStringLotNamerMatch : public kcLotNamerMatch
{
public:

    static const uint16_t match_flags = MATCHTYPE_STRING;
    static const MatchTarget match_target = occName;

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

    std::string getKey()
    {
        return szMatchName;
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

    static const uint16_t match_flags = MATCHTYPE_REGEX | MATCHMOD_PARTIAL;
    static const MatchTarget match_target = occGroup;

    kcPartialOccGroupRegexMatch(uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroups = { iOccupantGroup };
        rxMatchName = iMatchRegex;
    }

    kcPartialOccGroupRegexMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroups = { iOccupantGroup };
        rxMatchName = iMatchRegex;
    }

    kcPartialOccGroupRegexMatch(std::set<uint32_t> iOccupantGroups, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroups = iOccupantGroups;
        rxMatchName = iMatchRegex;
    }

    kcPartialOccGroupRegexMatch(uint16_t ipriority, std::set<uint32_t> iOccupantGroups, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroups = iOccupantGroups;
        rxMatchName = iMatchRegex;
    }

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        for (auto& og : dwOccupantGroups)
        {
            if (!thisOccupant->IsOccupantGroup(og)) return false;
        }

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

    std::set<uint32_t> getKey()
    {
        return dwOccupantGroups;
    }

protected:
    const static uint16_t priorityMod = 4;
    std::regex rxMatchName;
    std::set<uint32_t> dwOccupantGroups;
};

// Match based on occupant group ID && a complete regex match
// ~0x1000
// ~^.*\b[aA]partments?(?: [cC]omplex)?$|.*\b[tT]enements?(?: [cC]omplex)?$
// ; should match anything like "Apartment", "Apartment Complex", "Tenements", "Tenement Complex", etc.
class kcOccGroupRegexMatch : public kcLotNamerMatch
{
public:

    static const uint16_t match_flags = MATCHTYPE_REGEX;
    static const MatchTarget match_target = occGroup;

    kcOccGroupRegexMatch(uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroups = { iOccupantGroup };
        rxMatchName = iMatchRegex;
    }

    kcOccGroupRegexMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroups = { iOccupantGroup };
        rxMatchName = iMatchRegex;
    }

    kcOccGroupRegexMatch(std::set<uint32_t> iOccupantGroups, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroups = iOccupantGroups;
        rxMatchName = iMatchRegex;
    }

    kcOccGroupRegexMatch(uint16_t ipriority, std::set<uint32_t> iOccupantGroups, std::regex iMatchRegex, std::vector<kcNameChoice> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroups = iOccupantGroups;
        rxMatchName = iMatchRegex;
    }

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        for (auto& og : dwOccupantGroups)
        {
            if (!thisOccupant->IsOccupantGroup(og)) return false;
        }
        return true;

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

    std::set<uint32_t> getKey()
    {
        return dwOccupantGroups;
    }

protected:
    const static uint16_t priorityMod = 16;     // leaves room for more classes between direct string and this combined match definition
    std::regex rxMatchName;
    std::set<uint32_t> dwOccupantGroups;
};

// Match based on occupant group ID && exact, complete string match
// ~0x1000
// ~Cousin Bob's Place
class kcOccGroupStringMatch : public kcLotNamerMatch
{
public:

    static const uint16_t match_flags = MATCHTYPE_STRING;
    static const MatchTarget match_target = occGroup;

    kcOccGroupStringMatch(uint32_t iOccupantGroup, std::string iMatchString, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroups = { iOccupantGroup };
        szMatchName = iMatchString;
    }

    kcOccGroupStringMatch(uint16_t ipriority, uint32_t iOccupantGroup, std::string iMatchString, std::vector<kcNameChoice> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroups = { iOccupantGroup };
        szMatchName = iMatchString;
    }

    kcOccGroupStringMatch(std::set<uint32_t> iOccupantGroups, std::string iMatchString, std::vector<kcNameChoice> inames)
    {
        names = inames;
        dwOccupantGroups = iOccupantGroups;
        szMatchName = iMatchString;
    }

    kcOccGroupStringMatch(uint16_t ipriority, std::set<uint32_t> iOccupantGroups, std::string iMatchString, std::vector<kcNameChoice> inames)
    {
        names = inames;
        priority = ipriority;
        dwOccupantGroups = iOccupantGroups;
        szMatchName = iMatchString;
    }

    bool checkMatch(cISC4Occupant* thisOccupant)
    {
        for (auto& og : dwOccupantGroups)
        {
            if (!thisOccupant->IsOccupantGroup(og)) return false;
        }
        return true;

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

    std::set<uint32_t> getKey()
    {
        return dwOccupantGroups;
    }

protected:
    const static uint16_t priorityMod = 17;
    std::string szMatchName;
    std::set<uint32_t> dwOccupantGroups;
};

// This class, instead of using the names variable to return a name, uses it in checkMatch()
// getOne() and getOneStr() are overridden to return an empty cRZBaseString and an empty std::string, respectively
class kcLotNamerExclusions : public kcLotNamerMatch
{
public:
    std::vector<std::string> names;
    static const uint16_t match_flags = MATCHMOD_EXCEPTIONS;
    static const MatchTarget match_target = occGroup;

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

        if (kContains(ncs, "**"))
        {
            names = std::vector{ std::string("**") };
            match_all = true;
        }
        else
        {
            iextend(names, ncs);
        }
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

    uint32_t getKey()
    {
        return dwOccupantGroup;
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
        *           Nb. 5) However, files included by a file in one namespace will also use the same namespace. So, for example:
        *                   * <plugins>/alpha.namedef
        *                   * including "foo/bar" resolves to <plugins>/foo/bar.namedef
        *                   * if foo/bar then includes "bang/baz", that will resolve to <plugins>/bang/baz.namedef, not <plugins>/foo/bang/baz.namedef
        *   2. Initialize file-level tracking variables
        *	3. Open file for reading, and fgets() on it in a loop until EOF
        *		i.		Trim whitespace from both sides (including newlines)
        *		ii.		Check starting characters in if-elseif-else tree
        *		iii.	Change local section context and section key variables as needed (swapping out with the local map variables as needed)
        *		iv.		Use section context and key to add lines to appropriate list if no special char match
        */

        std::unordered_map<std::string, pathlib::path> includeFiles_map;      // maps requested include file name with found include file path
        std::vector<std::string> includeFiles_list;         // just lists requested include file names in load order for enumeration of the map
        bool skip_auto_includes = false;

        // check city first
        pathlib::path cityNames_path = currentRegionDirectory / (currentCityName + DOTNAMES);
        if (pathlib::is_regular_file(cityNames_path))
        {
            includeFiles_map[kCitySpecialFileName] = cityNames_path;
            includeFiles_list.push_back(kCitySpecialFileName);
            skip_auto_includes = checkIncludesRecursive(currentRegionDirectory, cityNames_path, includeFiles_map, includeFiles_list);
        }

        if (!skip_auto_includes)
        {
            // check region next, follow that same pattern^
            // plugins directory will take a bit more effort
            pathlib::path regionNames_path = currentRegionDirectory / (std::string("region") + DOTNAMES);
            if (pathlib::is_regular_file(regionNames_path))
            {
                includeFiles_map[kRegionSpecialFileName] = regionNames_path;
                includeFiles_list.push_back(kRegionSpecialFileName);
                skip_auto_includes = checkIncludesRecursive(currentRegionDirectory, regionNames_path, includeFiles_map, includeFiles_list);
            }
        }

        if (!skip_auto_includes)
        {
            // remaining files are not allowed to skip remaining auto-includes
            
            // okay, plugins directory next
            // for this one, we should just iterate through the main plugins directory (not subdirectories, so these files can *choose* which files to include)
            // if the file.path().extension() == DOTNAMES, then do checkIncludesRecursive()
            // we want to put the found files in a set first, though, so they'll be sorted alphabetically when we check their includes
            std::set<pathlib::path> found_plugins;
            for (auto const& f : pathlib::directory_iterator(userPluginDirectory))
            {
                if (f.is_regular_file())
                {
                    pathlib::path f_path = f.path();
                    std::string f_ext = f_path.extension().string();
                    if (f_ext == DOTNAMES && !found_plugins.contains(f_path))
                    {
                        // ding ding ding, we got a winner!
                        found_plugins.insert(f_path);
                    }
                }
            }

            // now they're sorted and we can check against our existing mapping
            for (auto const& f : found_plugins)
            {
                std::string inc_name = f.string().substr(userPluginDirectory.string().length());
                if (!includeFiles_map.contains(inc_name))
                {
                    includeFiles_list.push_back(inc_name);
                    includeFiles_map[inc_name] = f;
                    checkIncludesRecursive(userPluginDirectory, f, includeFiles_map, includeFiles_list);
                }
            }

            // last but not least, the default file and namespace
            pathlib::path defaultNames_path = userDataDirectory / (std::string("default") + DOTNAMES);
            if (pathlib::is_regular_file(defaultNames_path))
            {
                includeFiles_map[kDefaultSpecialFileName] = defaultNames_path;
                includeFiles_list.push_back(kDefaultSpecialFileName);
                checkIncludesRecursive(userDataDirectory, defaultNames_path, includeFiles_map, includeFiles_list);
            }
        }

        // okay, our list/mapping of includes has been built in the order we want them
        // now we get to actually build our name match lists

        std::map<int, kcLotNamerExclusions> exclusions_map;
        std::map<std::string, kcLotNamerMatch> namesByName_map;
        std::map<std::set<uint32_t>, std::vector<kcLotNamerMatch>> namesByGroup_map;

        uint16_t cur_priority = 1;
        unsigned int names_added = 0;
        using mTgt = kcLotNamerMatch::MatchTarget;
        for (std::string inc_f : includeFiles_list)
        {
            pathlib::path this_inc = includeFiles_map[inc_f];       // get the canonical path of this include name
            std::vector<kcLotNamerMatch> these_defs = parseNamefile(this_inc, cur_priority);
            for (auto& this_md : these_defs)
            {
                if (this_md.match_flags & MATCHMOD_EXCEPTIONS)
                {
                    // exceptions definition
                    // check for existing def with same occ group
                    // add this one's names to the other if existing, add this one to the map if not
                    kcLotNamerExclusions* this_md = dynamic_cast<kcLotNamerExclusions*>(this_md);
                    if (!this_md) continue;     // something went wrong

                    int this_og = this_md->getKey();
                    if (exclusions_map.contains(this_og))
                    {
                        exclusions_map[this_og].addMany(this_md->names);
                    }
                    exclusions_map[this_og] = *this_md;
                }
                else if (this_md.match_target == mTgt::occName)
                {
                    // goes to namesByName
                    kcStringLotNamerMatch* this_md = dynamic_cast<kcStringLotNamerMatch*>(this_md);
                    if (!this_md) continue;

                    std::string this_name = this_md->getKey();
                    if (namesByName_map.contains(this_name))
                    {
                        namesByName_map[this_name].addMany(*this_md);
                    }
                }
                else if (this_md.match_target == mTgt::occGroup)
                {
                    // shouldn't ever be anything else, right?
                    // goes to namesByGroup
                    std::set<uint32_t> these_ogs = this_md.getKey();
                    if (namesByGroup_map.contains(these_ogs))
                    {
                        auto& this_group = namesByGroup_map[these_ogs];
                        if (kContains(this_group, this_md))
                        {
                            kGetItem(this_group, this_md)->addMany(this_md);
                        }
                        else
                        {
                            this_group.push_back(this_md);
                        }
                    }
                    else
                    {
                        namesByGroup_map[these_ogs] = std::vector{ this_md };
                    }
                }
            }
        }
    }

    std::vector<kcLotNamerMatch> parseNamefile(pathlib::path this_filep, uint16_t &priority)
    {
        // read a file and a return a vector of name match definitions to be sorted by the caller based on their flags
        // priority gives the starting priority of the built name matches
        // priority should increment with each match def
        // if we end up making more than 65535 match definitions someone's got a problem and it's not me and my code
        if (!pathlib::is_regular_file(this_filep)) return;

        std::vector<kcLotNamerMatch> these_matchdefs;
        std::ifstream this_file(this_filep);
        for (std::string this_hdr; std::getline(this_file, this_hdr); )
        {
            // strip comments first
            if (this_hdr.contains(';'))
            {
                this_hdr.erase(this_hdr.find(';'));
            }

            itrim(this_hdr);    // then whitespace

            if (this_hdr.empty()) continue;     // it was all comment and/or whitespace, skip to next line

            // check header type

            char x = this_hdr.front();
            if (x == '~')
            {
                // check if integer, else string
                // begin parsing section lines, keep an eye out for extra headers
                // don't build LotNamerMatch object yet
                uint32_t this_og;
                std::set<uint32_t> these_ogs;
                std::string match_name;
                bool string_only = false;
                bool use_regex = false;
                bool partial_regex = false;
                bool most_specific = false;     // we've already got an occupant group and a regex or a string, treat any other non-int header definitions to the end of this section as name choices
                std::vector<kcNameChoice> these_names;

                if (!get_uint(this_hdr.substr(1), &this_og)) 
                {
                    match_name = this_hdr.substr(1);
                    string_only = true;
                    most_specific = true;
                }
                else
                {
                    these_ogs.insert(this_og);
                }

                // start getting names
                for (std::string this_line; std::getline(this_file, this_line); )
                {
                    if (this_line.front() == '\r' || this_line.front() == '\n') break;      // section break, finish processing this list

                    char x = this_line.front();
                    std::string str_rest = this_line.substr(1);
                    if (x == '~')
                    {
                        // got a few different choices
                        // try an integer first
                        uint32_t this_og;
                        if (get_uint(str_rest, &this_og))
                        {
                            these_ogs.insert(this_og);
                            continue;
                        }
                        else if (str_rest.front() == '^' && !most_specific)
                        {
                            use_regex = true;
                            match_name = str_rest;
                            most_specific = true;
                            continue;
                        }
                        else if (str_rest.front() == '%' && !most_specific)
                        {
                            partial_regex = true;
                            match_name = str_rest.substr(1);
                            most_specific = true;
                            continue;
                        }
                        else if (!most_specific)
                        {
                            match_name = str_rest;
                            most_specific = true;
                        }
                    }
                    else if (x == '>')
                    {
                        // URL name choice
                        kcURLNameChoice this_name(str_rest);
                        these_names.push_back(this_name);
                    }
                    else if (x == '*')
                    {
                        // get int and repeat previous name choice that many times
                        unsigned int n;
                        if (get_uint(str_rest, &n))
                        {
                            for (int i = 0; i < n; i++)
                            {
                                these_names.push_back(these_names.back());
                            }
                        }
                        else
                        {
                            // no int, treat like a regular name
                            kcNameChoice this_name(this_line);
                            these_names.push_back(this_name);
                        }
                    }
                    else if (this_line.starts_with("++"))
                    {
                        // build this one with &these_names.back() as a prefix, don't add it to these_names itself
                        str_rest = this_line.substr(2);
                        if (str_rest.front() == '>')
                        {
                            // URL name choice as a suffix
                            kcURLNameChoice this_name(str_rest.substr(1), &these_names.back());
                        }
                        else
                        {
                            kcNameChoice this_name(str_rest, &these_names.back());
                        }
                    }
                    else
                    {
                        // build regular NameChoice and add it to the list
                        kcNameChoice this_name(this_line);
                        these_names.push_back(this_name);
                    }
                }

                // build LotNamerMatch here using the flags and name list set above
                if (string_only)
                {
                    kcLotNamerMatch this_match_def(priority++, these_names);
                    these_matchdefs.push_back(this_match_def);
                }
                else if (partial_regex)
                {
                    kcPartialOccGroupRegexMatch this_match_def(priority++, these_ogs, std::regex(match_name, std::regex::optimize), these_names);
                    these_matchdefs.push_back(this_match_def);
                }
                else if (use_regex)
                {
                    kcOccGroupRegexMatch this_match_def(priority++, these_ogs, std::regex(match_name, std::regex::optimize), these_names);
                    these_matchdefs.push_back(this_match_def);
                }
                else if (match_name.length())
                {
                    kcOccGroupStringMatch this_match_def(priority++, these_ogs, match_name, these_names);
                    these_matchdefs.push_back(this_match_def);
                }
                else
                {
                    kcOccGroupLotNamerMatch this_match_def(priority++, these_ogs, these_names);
                    these_matchdefs.push_back(this_match_def);
                }
            }
            else if (x == '-')
            {
                // check for integer, then start exclusions list
                uint32_t this_og;
                if (!get_uint(this_hdr.substr(1), &this_og)) continue;

                std::vector<std::string> this_list;
                for (std::string this_line; std::getline(this_file, this_line); )
                {
                    if (this_line.front() == '\r' || this_line.front() == '\n') break;      // section break, finish processing this list

                    this_list.push_back(this_line);
                }

                kcLotNamerExclusions this_def(this_og, this_list);
                these_matchdefs.push_back(this_def);
            }
        }
        return these_matchdefs;
    }

    std::vector<std::string> checkIncludes(pathlib::path this_filep)
    {
        // read a file, but just check for includes for now, we'll parse the namedefs on second pass
        if (!pathlib::is_regular_file(this_filep)) return;      // somehow didn't get a regular file to read

        std::vector<std::string> this_includes;
        std::ifstream this_file(this_filep);
        for (std::string this_line; std::getline(this_file, this_line); )
        {
            itrim(this_line);       // trim whitespace
            if (this_line.front() == '<')
            {
                this_includes.push_back(this_line.substr(1));
            }
        }

        return this_includes;
    }

    bool checkIncludesRecursive(pathlib::path this_folder, pathlib::path this_filep, std::unordered_map<std::string, pathlib::path> &inc_map, std::vector<std::string> &inc_list)
    {
        bool skip_auto_includes = false;    // for passing up
        std::vector<std::string> this_includes = checkIncludes(this_filep);
        if (kContains(this_includes, kDisabledIncludes)) skip_auto_includes = true;     // found "<?!", skip remaining auto-include files in calling function
        if (this_includes.size())
        {
            // resolve included file names to canonical paths
            for (auto& inc : this_includes)
            {
                if (!inc_map.contains(inc))
                {
                    pathlib::path res_inc = resolveInclude(this_folder, inc);
                    if (!res_inc.empty())
                    {
                        inc_map[inc] = res_inc;
                        inc_list.push_back(inc);

                        // aaaaand recurse! Shouldn't be any risk of an infinite loop since we're always checking if we've already added a particular include
                        if (checkIncludesRecursive(this_folder, res_inc, inc_map, inc_list)) skip_auto_includes = true;
                    }
                    // if it doesn't resolve, no biggie, this is just a vidya game, we'll skip it silently
                    // until I figure out that I need to debug why something isn't working, lol
                }
            }
        }
        return skip_auto_includes;
    }

    pathlib::path resolveInclude(pathlib::path folder, std::string include_name)
    {
        // get canonical path for an include name in the given namespace/folder
        trim(include_name);                 // trim whitespace
        iltrim(include_name, "./\\");       // trim dots and slashes from the beginning -- should prevent parent directory includes
        irtrim(include_name, "/\\");        // just trim trailing slashes -- they're unnecessary and will conflict with the file extension
        trim(include_name);                 // trim whitespace again after trimming the others
        include_name += DOTNAMES;

        pathlib::path this_inc = pathlib::canonical(folder / include_name);
        if (pathlib::is_regular_file(this_inc))
        {
            // this should do
            return this_inc;
        }
        else
        {
            // nothing in current folder, let's check another...
            if (folder == currentRegionDirectory)
            {
                // check plugins next
                this_inc = pathlib::canonical(userPluginDirectory / include_name);
                if (pathlib::is_regular_file(this_inc))
                {
                    return this_inc;
                }
            }
            // if we're not in the region directory, we've already checked the plugin directory because that'd be next, so now the main one
            this_inc = pathlib::canonical(userDataDirectory / include_name);
            if (pathlib::is_regular_file(this_inc))
            {
                return this_inc;
            }
        }

        // no match
        return "";
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
