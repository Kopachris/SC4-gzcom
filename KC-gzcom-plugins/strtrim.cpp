/*
* Trim whitespace from strings either in-place or returning a copy.
* 
* Originally from https://stackoverflow.com/a/217605 licensed CC-BY-SA 4.0
* 
* Updated to use char8_t and with versions which specify their own trim characters
* Also made the "copy" version default and indicated in-place versions by prefixing the function names with 'i'
* And made a header file so this can be included in other programs
* 
*/

#include "strtrim.h"
#include <algorithm> 
#include <cctype>
#include <locale>

namespace strtrim {
    // trim from start (in place)
    static void iltrim(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char8_t ch) {
            return !std::isspace(ch);
            }));
    }

    // trim from end (in place)
    static void irtrim(std::string& s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), [](char8_t ch) {
            return !std::isspace(ch);
            }).base(), s.end());
    }

    // trim any of selected chars instead of whitespace
    static void iltrim(std::string& s, std::string trim_chars)
    {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [trim_chars](char8_t ch) {
            return !trim_chars.contains(ch);
            }));
    }

    static void irtrim(std::string& s, std::string trim_chars)
    {
        s.erase(std::find_if(s.rbegin(), s.rend(), [trim_chars](char8_t ch) {
            return !trim_chars.contains(ch);
            }).base(), s.end());
    }

    // trim from both ends (in place)
    static void itrim(std::string& s) {
        iltrim(s);
        irtrim(s);
    }

    static void itrim(std::string& s, std::string trim_chars)
    {
        iltrim(s, trim_chars);
        irtrim(s, trim_chars);
    }

    // trim from start (copying)
    static std::string ltrim(std::string s) {
        iltrim(s);
        return s;
    }

    static std::string ltrim(std::string s, std::string trim_chars)
    {
        iltrim(s, trim_chars);
        return s;
    }

    // trim from end (copying)
    static std::string rtrim(std::string s) {
        irtrim(s);
        return s;
    }

    static std::string rtrim(std::string s, std::string trim_chars)
    {
        irtrim(s, trim_chars);
        return s;
    }

    // trim from both ends (copying)
    static std::string trim(std::string s) {
        itrim(s);
        return s;
    }

    static std::string trim(std::string s, std::string trim_chars)
    {
        itrim(s, trim_chars);
        return s;
    }
}