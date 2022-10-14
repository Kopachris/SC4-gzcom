/*
* Trim whitespace from strings either in-place or returning a copy.
* 
* Originally from https://stackoverflow.com/a/217605 licensed CC-BY-SA 4.0
*/

#include "strtrim.h"
#include <algorithm> 
#include <cctype>
#include <locale>

// trim from start (in place)
static inline void strtrim::ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
        }));
}

// trim from end (in place)
static inline void strtrim::rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
        }).base(), s.end());
}

// trim from both ends (in place)
static inline void strtrim::trim(std::string& s) {
    ltrim(s);
    rtrim(s);
}

// trim from start (copying)
static inline std::string strtrim::ltrim_copy(std::string s) {
    ltrim(s);
    return s;
}

// trim from end (copying)
static inline std::string strtrim::rtrim_copy(std::string s) {
    rtrim(s);
    return s;
}

// trim from both ends (copying)
static inline std::string strtrim::trim_copy(std::string s) {
    trim(s);
    return s;
}