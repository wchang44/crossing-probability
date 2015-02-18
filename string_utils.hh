#ifndef __string_utils_hh__
#define __string_utils_hh__

#include <string>
#include <vector>

long string_to_long(const std::string& s);
double string_to_double(const std::string& s);
std::vector<std::string> split(const std::string& s, char delimiter);
std::vector<double> read_comma_delimited_doubles(const std::string& line);

#endif
