#pragma once

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

namespace strtrim
{
	static void iltrim(std::string& s);
	static void iltrim(std::string& s, std::string trim_chars);
	static void irtrim(std::string& s);
	static void irtrim(std::string& s, std::string trim_chars);
	static void itrim(std::string& s);
	static void itrim(std::string& s, std::string trim_chars);
	static std::string ltrim(std::string s);
	static std::string ltrim(std::string s, std::string trim_chars);
	static std::string rtrim(std::string s);
	static std::string rtrim(std::string s, std::string trim_chars);
	static std::string trim(std::string s);
	static std::string trim(std::string s, std::string trim_chars);
}