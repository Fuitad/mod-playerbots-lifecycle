#ifndef PLAYERBOTS_LIFECYCLE_STANDALONE_UTIL_H
#define PLAYERBOTS_LIFECYCLE_STANDALONE_UTIL_H

#include <cctype>
#include <string>

inline bool Utf8ToUpperOnlyLatin(std::string& value)
{
    for (char& symbol : value)
        symbol = static_cast<char>(std::toupper(static_cast<unsigned char>(symbol)));
    return true;
}

#endif
