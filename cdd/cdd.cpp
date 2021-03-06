/*

Copyright 2010-2021 Michael Graz
http://www.plan10.com/cdd

This file is part of Cd Deluxe.

Cd Deluxe is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Cd Deluxe is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Cd Deluxe.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "stdafx.h"
#include "cdd_version.h"

// TODO consolidate header files more rationally
#include <regex>
#include <cassert>

#include "cxxopts.hpp"

#define countof(x) (sizeof(x)/sizeof(x[0]))

const string Cdd::env_options_name = "CDD_OPTIONS";

const char *param_debug_input = "debug-input";

//----------------------------------------------------------------------
// ref: https://stackoverflow.com/questions/236129/the-most-elegant-way-to-iterate-the-words-of-a-string

template<typename Out>
void split(const std::string &s, char delim, Out result) {
    std::stringstream ss(s);
    std::string item;
    int count = 0;
    while (std::getline(ss, item, delim)) {
        *(result++) = item;
    }
}

std::vector<std::string> split(const std::string &s, char delim=' ') {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
}

//----------------------------------------------------------------------

Cdd::Cdd(void)
{
    initialize();
}

Cdd::Cdd(vector<string>& vec_pushd, string current_path)
{
    initialize();
    assign(vec_pushd, current_path);
}

Cdd::Cdd(string arr_pushd[], int count, string current_path)
{
    initialize();
    assign(arr_pushd, count, current_path);
}

void Cdd::initialize(void)
{
    current_path = string();
    current_path_added = false;
    opt_help = false;
    opt_version = false;
    opt_path = string();
    opt_history = false;
    opt_gc = false;
    opt_delete = false;
    opt_reset = false;
    opt_limit_backwards = 10;
    opt_limit_forwards = 0;
    opt_limit_common = 10;
    opt_all = false;
#ifdef WIN32
    opt_separator = '\\';
#else
    opt_separator = '/';
#endif
}

void Cdd::assign(string arr_pushd[], int count, string current_path)
{
    vector<string> vec_pushd;
    vec_pushd.assign(arr_pushd, arr_pushd + count);
    assign(vec_pushd, current_path);
}

void Cdd::assign(vector<string>& vec_pushd, string current_path)
{
    assert( ! has_directory_stack );
    vec_dir_stack = vec_pushd;
    this->current_path = current_path;

#ifdef WIN32
    if (!current_path.empty())
    {
        vec_dir_stack.insert(vec_dir_stack.begin(), current_path);
        current_path_added = true;
    }
#endif

    string current_path_normalized = normalize_path(current_path);
    int current_path_inode = get_inode(current_path_normalized);

    // First, save the vector of pushed directories
    set<string> set_dir1;
    for (vector<string>::iterator it=vec_dir_stack.begin(); it!=vec_dir_stack.end(); ++it)
    {
        string dir = *it;
        string normalized = normalize_path(dir);

        // Check to see if this directory has already been seen
        if (set_dir1.find(normalized) == set_dir1.end())
        {
            // Filter out the current_path after the set inclusion test,
            // since checking inode is potentially expensive.
            if (paths_equal(current_path_normalized, current_path_inode, normalized))
                continue;
            // Directory has not been seen, add it to the vector
            vec_dir_last_to_first.push_back(dir);
            set_dir1.insert(normalized);
        }
    }

    // Second, build up a vector of all directories but with removing
    // duplicates.  This allows for assigning a unique number to each dir.
    set<string> set_dir2;
    vector<string>::reverse_iterator rit;
    for (rit=vec_dir_stack.rbegin(); rit!=vec_dir_stack.rend(); ++rit)
    {
        string dir = *rit;
        string normalized = normalize_path(dir);
        // Check to see if this directory has already been seen
        if (set_dir2.find(normalized) == set_dir2.end())
        {
            // Directory has not been seen, add it to the vector
            vec_dir_first_to_last.push_back(dir);
            set_dir2.insert(normalized);
        }
    }

    // Third, build up the vector of most common directories
    typedef map<string, Common> MapCommon;
    MapCommon map_common;
    for (vector<string>::iterator it=vec_dir_stack.begin(); it!=vec_dir_stack.end(); ++it)
    {
        string dir = *it;
        string normalized = normalize_path(dir);
        MapCommon::iterator mi;
        mi = map_common.find(normalized);
        if (mi == map_common.end())
            map_common.insert(MapCommon::value_type(normalized, Common(1, map_common.size(), dir)));
        else
            mi->second.count++;
    }
    for (MapCommon::iterator mi=map_common.begin(); mi!=map_common.end(); ++mi)
        vec_dir_most_to_least.push_back(mi->second);
    sort(vec_dir_most_to_least.begin(), vec_dir_most_to_least.end());
    has_directory_stack = true;
}

void Cdd::assign_debug_input(const string& input_path)
{
    assert( ! has_directory_stack );
    std::cerr << "Using file instead of stdin: \"" << input_path << "\"\n";
    vector<string> vec_pushd;
    string line;
    std::fstream fstrm(input_path);
    while (getline(fstrm, line))
        vec_pushd.push_back(line);
    assign(vec_pushd, get_working_path());
}

string Cdd::normalize_path(const string& path)
{
    string result;
    int count = 0;
    for (string::const_iterator it=path.begin(); it!=path.end(); ++it)
    {
        char c = *it;
#ifdef WIN32
        // Windows pathnames are case insensitive
        c = tolower(c);
#endif
        if (c == opt_separator)
            c = '/';
        if (++count == path.size())
        {
            // Don't add trailing slash
            if (c == '/')
                break;
        }
        result.push_back(c);
    }
    return result;
}

string Cdd::windowize_path(const string& path)
{
    string result;
    for (string::const_iterator it=path.begin(); it!=path.end(); ++it)
    {
        char c = *it;
        result.push_back(c == '/' ? opt_separator : c);
    }
    return result;
}

string Cdd::get_parent_path(const string& path)
{
    // TODO use opt_separator here
    std::size_t found = path.find_last_of("/\\");
    if ( found != string::npos )
    {
        if ( found == 0 )
        {
            // special case of file in root dir
            return path.substr(0, 1);
        }
        return path.substr(0, found);
    }
#ifdef WIN32
    if ( path.length() >= 2 && path[1] == ':' )
    {
        return path.substr(0, 2);
    }
#endif
    return path;    // just return original path
}

bool Cdd::paths_equal(const string& path1, int inode1, const string& path2)
{
    int inode2 = get_inode(path2);
    if (inode1 > 0 && inode2 > 0)
        return inode1 == inode2;
    return path1 == path2;
}

int Cdd::get_inode(const string& path)
{
#ifndef WIN32
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
        return st.st_ino;
#endif
    return 0;
}

string Cdd::expand_dots(string path)
{
    static std::regex re_dots("(?:^|[\\\\/])([.]{3,})(?:[\\\\/]|$)");
    std::smatch what;
    if (std::regex_search(path, what, re_dots))
    {
        string s_start(what[0].first, what[1].first);
        string s_end(what[1].second, what[0].second);
        string dots = "..";
        int dot_len = what[1].second - what[1].first;
        for (int i=2; i<dot_len; i++)
        {
            dots += opt_separator;
            dots += "..";
// #ifdef WIN32
//             dots += "\\..";
// #else
//             dots += "/..";
// #endif
        }
        return string(what.prefix()) + s_start + dots + expand_dots(s_end + string(what.suffix()));
    }
    return path;
}

int Cdd::pushd_count()
{
    int count = vec_dir_stack.size();
    if (current_path_added)
        count --;
    return count;
}

void Cdd::process(void)
{
    if (opt_help)
    {
        help();
        return;
    }
    if (opt_version)
    {
        version();
        return;
    }
    if (opt_gc)
    {
        garbage_collect();
        return;
    }
    else if (opt_delete)
    {
        process_delete();
        return;
    }
    else if (opt_reset)
    {
        process_reset();
        return;
    }
    else if (opt_path.size())
    {
        change_to_path_spec();
        return;
    }
    else if (opt_history)
    {
        show_history();
        return;
    }
    help();
}

bool Cdd::change_to_path_spec(void)
{
    bool rc = false;

    // Save the original specification before any changes applied
    opt_path_original = opt_path;

    string path_found;
    vector<string> path_extra;
    stringstream path_error;
    if (process_path_spec(path_found, path_extra, path_error))
    {
#ifdef WIN32
        strm_out << "pushd " << path_found << endl;
#else
        strm_out << "pushd '" << path_found << "'" << endl;
#endif
        if (path_found != opt_path_original || path_extra.size())
            strm_err << "cdd: " << path_found << endl;
        vector<string>::iterator it;
        for (it=path_extra.begin(); it!=path_extra.end(); ++it)
            strm_err << *it << endl;
        rc = true;
    }
    string error_msg = path_error.str();
    if (!error_msg.empty())
        strm_err << error_msg;
    return rc;
}

static std::regex re_num("(\\d+)");
static std::regex re_dashes("-+");
static std::regex re_two_or_more_dashes("--+");
static std::regex re_dash_num("-(\\d+)");
static std::regex re_pluses("[+]+");
static std::regex re_plus_num("[+](\\d+)");
static std::regex re_commas(",+");
static std::regex re_comma_num(",(\\d+)");

bool is_valid_path_spec(const char *s)
{
    if ( std::regex_match(s, re_two_or_more_dashes) )
        return true;
    if ( std::regex_match(s, re_dash_num) )
        return true;
    return false;
}

// Main matching engine
// path_found will have the result of the path match if it exists
// path_extra will have the strings to display as an alternative to path_found
// path_error is a stream which will detail any error message

bool Cdd::process_path_spec(string& path_found, vector<string>& path_extra, stringstream& path_error)
{
    // Rewrite things like "..." to "../.."
    opt_path = expand_dots(opt_path);

#ifdef WIN32
    // Recover gracefully if forward slashes passed instead
    opt_path = windowize_path(opt_path);
#endif

    // Normal cd operation: if it is a directory, change to it
    if (is_directory(opt_path))
    {
        path_found = opt_path;
        return true;
    }
    // If it is a file, change to the directory containing the file
    if (is_regular_file(opt_path))
    {
        // fs::path path(opt_path);
        // path_found = path.parent_path().string();
        path_found = get_parent_path(opt_path);
        return true;
    }
    if (vec_dir_stack.empty())
    {
        path_error << "No history of directories" << endl;
        return false;
    }
    std::smatch match;
    if (std::regex_match(opt_path, match, re_num))
    {
        int amount = std::stoi(match[1]);
        if (direction.is_backwards())
            return go_backwards(amount, path_found, path_error);
        if (direction.is_forwards())
            return go_forwards(amount, path_found, path_error);
        if (direction.is_common())
            return go_common(amount, path_found, path_error);
        return false;
    }
    if (std::regex_match(opt_path, match, re_dash_num))
    {
        int amount = std::stoi(match[1]);
        return go_backwards(amount, path_found, path_error);
    }
    if (std::regex_match(opt_path, match, re_dashes))
    {
        int amount = match[0].str().size();
        return go_backwards(amount, path_found, path_error);
    }
    if (std::regex_match(opt_path, match, re_plus_num))
    {
        int amount = std::stoi(match[1]);
        return go_forwards(amount, path_found, path_error);
    }
    if (std::regex_match(opt_path, match, re_pluses))
    {
        int amount = match[0].str().size();
        return go_forwards(amount-1, path_found, path_error);
    }
    if (std::regex_match(opt_path, match, re_comma_num))
    {
        int amount = std::stoi(match[1]);
        return go_common(amount, path_found, path_error);
    }
    if (std::regex_match(opt_path, match, re_commas))
    {
        int amount = match[0].str().size();
        return go_common(amount-1, path_found, path_error);
    }

    return process_match(path_found, path_extra, path_error);
}

bool Cdd::go_backwards(unsigned amount, string& path_found, stringstream& path_error)
{
    if (amount < 1 || amount > vec_dir_last_to_first.size())
    {
        path_error << "No directory at -" << amount << endl;
        return false;
    }
    path_found = vec_dir_last_to_first[amount-1];
    return true;
}

bool Cdd::go_forwards(unsigned amount, string& path_found, stringstream& path_error)
{
    if (amount < 0 || amount >= vec_dir_first_to_last.size())
    {
        path_error << "No directory at +" << amount << endl;
        return false;
    }
    path_found = vec_dir_first_to_last[amount];
    return true;
}

bool Cdd::go_common(unsigned amount, string& path_found, stringstream& path_error)
{
    if (amount < 0 || amount >= vec_dir_most_to_least.size())
    {
        path_error << "No directory at ," << amount << endl;
        return false;
    }
    path_found = vec_dir_most_to_least[amount].dir;
    return true;
}

void Cdd::show_history(void)
{
    if (direction.is_backwards())
        show_history_last_to_first();
    else if (direction.is_forwards())
        show_history_first_to_last();
    else if (direction.is_common())
        show_history_most_to_least();
}

void Cdd::show_history_first_to_last(void)
{
    vector<string>::iterator it;
    unsigned count = 0;
    int number = 0;
    for (it=vec_dir_first_to_last.begin(); it!=vec_dir_first_to_last.end(); ++it)
    {
        strm_err << setw(3) << number++ << ": " << *it << endl;
        if (++count >= opt_limit_forwards && opt_limit_forwards > 0 && !opt_all)
            break;
    }
    if (count < vec_dir_first_to_last.size())
        strm_err << " ... showing first " << count << " of " << vec_dir_first_to_last.size() << endl;
}

void Cdd::show_history_last_to_first(void)
{
    if (vec_dir_last_to_first.empty())
    {
        strm_err << "No history of other directories" << endl;
        return;
    }
    vector<string>::iterator it;
    unsigned count = 0;
    int number = -1;
    for (it=vec_dir_last_to_first.begin(); it!=vec_dir_last_to_first.end(); ++it)
    {
        strm_err << setw(3) << number-- << ": " << *it << endl;
        if (++count >= opt_limit_backwards && opt_limit_backwards > 0 && !opt_all)
            break;
    }
    if (count < vec_dir_last_to_first.size())
        strm_err << " ... showing last " << count << " of " << vec_dir_last_to_first.size() << endl;
}

void Cdd::show_history_most_to_least(void)
{
    vector<Common>::iterator it;
    unsigned count = 0;
    int number = 0;
    for (it=vec_dir_most_to_least.begin(); it!=vec_dir_most_to_least.end(); ++it)
    {
        if (number < 10)
            strm_err << " ";
        strm_err << "," << number++ << ": (" << setw(2) << it->count << ") " << it->dir << endl;
        if (++count >= opt_limit_common && opt_limit_common > 0 && !opt_all)
            break;
    }
    if (count < vec_dir_most_to_least.size())
        strm_err << " ... showing top " << count << " of " << vec_dir_most_to_least.size() << endl;
}

bool Cdd::is_directory(string path)
{
    // return fs::is_directory(path);
    struct stat info;
    return stat(path.c_str(), &info) == 0 && info.st_mode & S_IFDIR;
}

bool Cdd::is_regular_file(string path)
{
    // return fs::is_regular_file(path);
    struct stat info;
    return stat(path.c_str(), &info) == 0 && !(info.st_mode & S_IFDIR);
}

bool Cdd::process_match(string& path_found, vector<string>& path_extra, stringstream& path_error)
{
    // Ignore case by default
    std::regex re;
    try
    {
        re.assign(opt_path, std::regex_constants::icase);
    }
    catch (std::regex_error& e)
    {
        path_error << "Cannot process pattern: '" << opt_path << "'" << endl << e.what() << endl;
        return false;
    }

    std::smatch what;
    if (direction.is_backwards())
    {
        unsigned count = 0;
        bool truncated = false;
        int number = -1;
        vector<string>::iterator it;
        for (it=vec_dir_last_to_first.begin(); it!=vec_dir_last_to_first.end(); ++it)
        {
            string dir = *it;
            if (std::regex_search(dir, what, re))
            {
                count ++;
                if (path_found.empty())
                    path_found = dir;
                else if ( opt_all || opt_limit_backwards == 0 || count <= opt_limit_backwards )
                {
                    stringstream strm;
                    strm << setw(3) << number << ": " << dir;
                    path_extra.push_back(strm.str());
                }
                else
                    truncated = true;
            }
            number--;
        }
        if ( truncated )
        {
            stringstream strm;
            strm << " ... showing last " << opt_limit_backwards << " matching of " << count;
            path_extra.push_back(strm.str());
        }
    }

    else if (direction.is_forwards())
    {
        unsigned count = 0;
        bool truncated = false;
        int number = 0;
        vector<string>::iterator it;
        for (it=vec_dir_first_to_last.begin(); it!=vec_dir_first_to_last.end(); ++it)
        {
            string dir = *it;
            if (std::regex_search(dir, what, re))
            {
                count ++;
                if (path_found.empty())
                    path_found = dir;
                else if ( opt_all || opt_limit_forwards == 0 || count <= opt_limit_forwards )
                {
                    stringstream strm;
                    strm << setw(3) << number << ": " << dir;
                    path_extra.push_back(strm.str());
                }
                else
                    truncated = true;
            }
            number++;
        }
        if ( truncated )
        {
            stringstream strm;
            strm << " ... showing first " << opt_limit_forwards << " matching of " << count;
            path_extra.push_back(strm.str());
        }
    }

    else if (direction.is_common())
    {
        unsigned count = 0;
        bool truncated = false;
        int number = 0;
        vector<Common>::iterator it;
        for (it=vec_dir_most_to_least.begin(); it!=vec_dir_most_to_least.end(); ++it)
        {
            string dir = it->dir;
            if (std::regex_search(dir, what, re))
            {
                count ++;
                if (path_found.empty())
                    path_found = dir;
                else if ( opt_all || opt_limit_common == 0 || count <= opt_limit_common )
                {
                    stringstream strm;
                    if (number < 10)
                        strm << " ";
                    strm << "," << number << ": (" << setw(2) << it->count << ") " << it->dir;
                    path_extra.push_back(strm.str());
                }
                else
                    truncated = true;
            }
            number++;
        }
        if ( truncated )
        {
            stringstream strm;
            strm << " ... showing top " << opt_limit_common << " matching of " << count;
            path_extra.push_back(strm.str());
        }
    }

    if (path_found.empty())
    {
        path_error << "Cannot match pattern: '" << opt_path << "'" << endl;
        return false;
    }
    return true;
}

void Cdd::garbage_collect(void)
{
    command_generator(vec_dir_first_to_last);
    strm_err << "cdd gc" << endl;
}

bool has_string(vector<string>& vec, string str)
{
    vector<string>::iterator it;
    for (it=vec.begin(); it!=vec.end(); ++it)
    {
        if (*it == str)
            return true;
    }
    return false;
}

void Cdd::process_delete(void)
{
    string path_found;
    vector<string> path_extra;
    stringstream path_error;
    if (! process_path_spec(path_found, path_extra, path_error))
    {
        // Here: error
        string error_msg = path_error.str();
        if (!error_msg.empty())
            strm_err << error_msg;
        else
            strm_err << "** Could not resolve for delete: " << opt_path << endl;
        return;
    }

    if (! has_string(vec_dir_stack, path_found))
    {
        strm_err << "** Could not delete from history: " << path_found << endl;
        return;
    }

    vector<string> vec_dir;
    vec_dir.assign(vec_dir_stack.rbegin(), vec_dir_stack.rend());
    command_generator(vec_dir, path_found);
    strm_err << "cdd del: " << path_found << endl;
}

void Cdd::process_reset(void)
{
    vector<string> vec_dir;
    command_generator(vec_dir);
    strm_err << "cdd reset" << endl;
}

void Cdd::command_generator(vector<string>& vec_dir, const string& dir_delete)
{
#ifdef WIN32
    command_generator_win32(vec_dir, dir_delete);
#else
    command_generator_bash(vec_dir, dir_delete);
#endif
}

void Cdd::command_generator_win32(vector<string>& vec_dir, const string& dir_delete)
{
    strm_out << "for /l %%i in (1,1," << pushd_count() << ") do popd" << endl;
    int count = 0;
    vector<string>::iterator it;
    for (it=vec_dir.begin(); it!=vec_dir.end(); ++it)
    {
        string dir = *it;
        if (dir == dir_delete)
            continue;
        strm_out << (count++ ? "pushd " : "chdir/d ") << dir << " 2>nul" << endl;
    }
    if (!current_path.empty())
        strm_out << (count ? "pushd " : "chdir/d ") << windowize_path(current_path) << endl;
}

void Cdd::command_generator_bash(vector<string>& vec_dir, const string& dir_delete)
{
    strm_out << "dirs -c" << endl;
    int count = 0;
    vector<string>::iterator it;
    for (it=vec_dir.begin(); it!=vec_dir.end(); ++it)
    {
        string dir = *it;
        if (dir == dir_delete)
            continue;
        strm_out << (count++ ? "pushd" : "\\cd") << " '" << dir << "'" << endl;
    }
}

//----------------------------------------------------------------------

void Cdd::set_opt_path(const string& opt_path)
{
    // This handles the special case of a positive integer being assigned without a direction.
    // It should only be called once the direction options have been assigned.
    this->opt_path = opt_path;
    if (direction.is_assigned())
        return;
    // Here: direction has not been assigned.
    // Try to discern the direction if an integer value has been passed.
    int ivalue;
    try
    {
        ivalue = std::stoi(opt_path);
    }
    catch (std::logic_error &)
    {
        // Not an int, so give up
        return;
    }
    if (ivalue >= 0)
        direction.assign("+");
    else
        direction.assign("-");
}

bool Cdd::set_history_direction(const string& spec)
{
    if (spec == "?")
    {
        // Special case here.  If direction has not been specifically indicated,
        // then set it to be forwards.
        if (!direction.is_assigned())
            direction.assign("+");
        return true;
    }
    if (spec == "??")
    {
        direction.assign(",");
        return true;
    }
    if (spec.size() == 2)
    {
        string s0(spec.substr(0, 1));
        string s1(spec.substr(1, 1));
        if (Cdd::Direction::is_valid_direction(s0) && s1 == "?")
        {
            direction.assign(s0);
            return true;
        }
    }
    return false;
}

struct OptionDirection
{
    string direction;
    OptionDirection() : direction(string()) {}
    OptionDirection(string s) : direction(s) {}
};

std::istream& operator>>(std::istream& is, OptionDirection& obj)
{
    // read obj from stream
    is >> obj.direction;
//     if( /* T could not be constructed */ )
//         is.setstate(std::ios::failbit);
    return is;
}

template<typename T>
T get_value(const char *opt_name, const cxxopts::ParseResult& opts_cmd, const cxxopts::ParseResult& opts_env)
{
    if ( opts_cmd.count(opt_name) > 0 )
        return opts_cmd[opt_name].as<T>();
    if ( opts_env.count(opt_name) > 0 )
        return opts_env[opt_name].as<T>();
    return {};
}

bool Cdd::options(int ac, const char *av[], const string& env_options)
{
    if (ac > 0)
    {
        // handle special cases which conflict with tradition option parsing
        if ( is_valid_path_spec(av[ac-1]) )
            opt_path = av[--ac];
    }

//     std::cout << "xx opt_path: >" << opt_path.c_str() << "<\n";
//     std::cout << "xx direction._direction: >" << direction._direction << "<\n";
//     for(int i=0; i<ac; i++)
//         std::cout << "xx options ac[" << i << "]: >" << av[i] << "<\n";

    string opts_src;
    try
    {
        cxxopts::Options options("cdd", "cd Deluxe options");
        // First, parse the options string with a limited set of options
        options.add_options()
            ("action", "Specify freeform action", cxxopts::value<string>())
            // ("direction", "Direction of search and history", cxxopts::value<OptionDirection>()->default_value(&option_direction))
            ("direction", "Direction of search and history", cxxopts::value<string>()->default_value(direction._direction))
            ("limit-backwards", "Limit of history (last to first) to display", cxxopts::value(opt_limit_backwards))
            ("limit-forwards", "Limit of history (first to last) to display", cxxopts::value(opt_limit_forwards))
            ("limit-common", "Limit of history (most to least) to display", cxxopts::value(opt_limit_common))
            ("path-separator", "Custom path separator", cxxopts::value(opt_separator))
            ("all", "Show all, do not limit listing")
            ;

        auto vec_env_options = split(env_options);
        // The options package is expecting the program name to be in zeroth position.
        // Put in a placeholder that will be ignored.
        vec_env_options.insert(vec_env_options.begin(), "dummy");

        // need to turn this into an array of char pointers for cxxopts
        char *argv_env_options[32];  // choose a suitably large number
        int argc_env_options = vec_env_options.size();
        if ( argc_env_options > countof(argv_env_options) )
            argc_env_options = countof(argv_env_options);
        for(int i=0; i<argc_env_options; i++)
            argv_env_options[i] = const_cast<char *>(vec_env_options[i].c_str());

        opts_src = "in environment variable";
        char **argv_env_options_ptr = argv_env_options;
        auto opts_env = options.parse(argc_env_options, argv_env_options_ptr);

        std::vector<std::string> vec_action;

        options.add_options()
            ("help", "Program help")
            ("version", "Version information")
            ("path", "Change to path number|string", cxxopts::value<string>())
            ("history", "Show history")
            ("gc", "Garbage collect")
            ("del", "Delete from history")
            ("delete", "Delete from history")
            ("reset", "Reset (erase) all history")
            ("positional", "Positional parameters", cxxopts::value<std::vector<std::string>>(vec_action))
#if !defined(NDEBUG)
            (param_debug_input, "Directory stack to use, parsed from a file", cxxopts::value<string>())
#endif
            ;

        options.parse_positional({"positional"});

        // need to turn this into an array of char pointers for cxxopts
        char *argv_cmd_options[32];  // choose a suitably large number
        int argc_cmd_options = ac;
        if ( argc_cmd_options > countof(argv_cmd_options) )
            argc_cmd_options = countof(argv_cmd_options);
        for(int i=0; i<argc_cmd_options; i++)
            argv_cmd_options[i] = const_cast<char *>(av[i]);

        opts_src = "on command line";
        char **argv_cmd_options_ptr = argv_cmd_options;
        auto opts_cmd = options.parse(argc_cmd_options, argv_cmd_options_ptr);

        if (opts_cmd.count("help"))
        {
            opt_help = true;
            return true;
        }
        if (opts_cmd.count("version"))
        {
            opt_version = true;
            return true;
        }

#if !defined(NDEBUG)
        if ( opts_cmd.count(param_debug_input) )
            assign_debug_input(opts_cmd[param_debug_input].as<string>());
#endif

        if (opts_cmd.count("history"))
            opt_history = true;
        if (opts_cmd.count("gc"))
            opt_gc = true;
        if (opts_cmd.count("del") || opts_cmd.count("delete"))
            opt_delete = true;
        if (opts_cmd.count("reset"))
            opt_reset = true;
        string opt_direction;
        opt_direction = get_value<string>("direction", opts_cmd, opts_env);
        if ( ! opt_direction.empty() )
        {
            try
            {
                direction.assign(opt_direction);
            }
            catch(exception& e)
            {
                strm_err << "** Options error: " << e.what() << endl;
                help_tip();
                return false;
            }
        }

        opt_all = get_value<bool>("all", opts_cmd, opts_env);

        if (opts_cmd.count("path"))
            set_opt_path(opts_cmd["path"].as<string>());

        // opt_path may be assigned later from vec_action
        if ( opt_delete && opt_path.empty() && vec_action.empty() )
        {
            strm_err << "** No path indicated for delete" << endl;
            return false;
        }

        if (vec_action.empty())
        {
            // Need at least history or path or one of the commands
            if (opt_history || (! opt_path.empty()) || opt_gc || opt_delete || opt_reset)
                return true;
            // Here: no actions specified, look in the 'action' option parameter
            string action = get_value<string>("action", opts_cmd, opts_env);
            if ( ! action.empty() )
            {
                try
                {
                    std::istringstream iss(action);
                    for(std::string s; iss >> s; )
                        vec_action.push_back(s);
                }
                catch (exception& e)
                {
                    strm_err << "** Caught exception parsing option --action: " << e.what() << endl;
                    help_tip();
                    return false;
                }
            }
        }

        if (vec_action.empty())
        {
            // If no actions specified, default action is history
            opt_history = true;
            return true;
        }

        if (vec_action.size() == 1)
        {
            if (set_history_direction(vec_action[0]))
                opt_history = true;
            else
                set_opt_path(vec_action[0]);
            return true;
        }
        if (vec_action.size() == 2)
        {
            if (Direction::is_valid_direction(vec_action[0]))
            {
                direction.assign(vec_action[0]);
                set_opt_path(vec_action[1]);
                return true;
            }

            if (set_history_direction(vec_action[0]))
            {
                opt_history = true;
                unsigned amount;
                try
                {
                    amount = std::stoi(vec_action[1]);
                }
                catch (std::logic_error &)
                {
                    strm_err << "** Options error: expecting number for second option: "
                        << vec_action[0] << " " << vec_action[1] << endl;
                    help_tip();
                    return false;
                }
                if (direction.is_backwards())
                    opt_limit_backwards = amount;
                else if (direction.is_forwards())
                    opt_limit_forwards = amount;
                else if (direction.is_common())
                    opt_limit_common = amount;
                // Number specified here overrides --all
                opt_all = false;
                return true;
            }
        }
        if (vec_action.size() > 2)
        {
            strm_err << "** Options error: too many options specified" << endl;
            help_tip();
            return false;
        }
        strm_err << "** Options error: unable to interpret options" << endl;
        help_tip();
        return false;

    }
    catch(cxxopts::OptionException& e)
    {
        strm_err << "** Options error " << opts_src.c_str() << ": " << e.what() << endl;
        help_tip();
        return false;
    }
}

void Cdd::help_tip(void)
{
    strm_err << "Use --help to see possible options" << endl;
}

void Cdd::help(void)
{

  // clang-format off
cerr <<
"Usage:\n"
"\n"
"  cdd [NAMED_OPTIONS] [FREEFORM_OPTIONS]\n"
"\n"
"NAMED_OPTIONS consist of:\n"
"\n"
"  --history               Show directory history depending on the direction\n"
"  --path=PATH_SPEC        Change to path specification (number or regular expression pattern)\n"
"  --direction={-|+|,}     Specify direction (backwards, forwards, most common) for history or PATH_SPEC\n"
"  --limit-backwards=n     Show at most n directories for last to first history\n"
"  --limit-forwards=n      Show at most n directories for first to last history\n"
"  --limit-common=n        Show at most n directories for most to least visited directories\n"
"  --path-separator=n      Force path separator to be a specific character\n"
"  --all                   Show all directories (overriding any 'limit' options)\n"
"  --action                Default freeform option to use when nothing else specified\n"
"  --gc                    Do garbage collection by minimizing directory stack\n"
"  --del=PATH_SPEC         Remove from history the directory matching PATH_SPEC\n"
"  --reset                 Reset the directory stack which clears all history\n"
"  --help                  Show help (this information)\n"
"  --version               Show version number\n"
"\n"
"FREEFORM_OPTIONS:\n"
 "\n"
"  {-|+|,|?}?              Show directory history (backwards '-', forwards '+' or most common ',' or '?')\n"
"  {-|+|,|?}? n            Show history limited by n amount (n == 0 means show all history)\n"
"  PATH_SPEC               Change to PATH_SPEC using the default direction\n"
"  {-|+|,} PATH_SPEC       Change to PATH_SPEC using the specified direction\n"
"\n"
"PATH_SPEC can be a number, a repeated direction, or a direction and a pattern.\n"
"\n"
"See http://www.plan10.com/cdd for more information.\n"
"Copyright 2010-2021 Michael Graz\n"
;
  // clang-format on
}

void Cdd::version(void)
{
    cerr << "cdd " << CDD_VERSION << endl;
}

// vim:ff=unix
