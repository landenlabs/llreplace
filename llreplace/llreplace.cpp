//-------------------------------------------------------------------------------------------------
//
//  llreplace      9/16/2016       Dennis Lang
//
//  Regular expression file text replacement tool
//
//-------------------------------------------------------------------------------------------------
//
// Author: Dennis Lang - 2016
// http://landenlabs.com/
//
// This file is part of llreplace project.
//
// ----- License ----
//
// Copyright (c) 2016 Dennis Lang
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// 4291 - No matching operator delete found
#pragma warning( disable : 4291 )

#include <stdio.h>
#include <ctype.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdlib.h>     // stroul

#include "ll_stdhdr.h"
#include "directory.h"
#include "split.h"
#include "filters.h"
#include "colors.h"

#include <vector>
#include <map>
#include <algorithm>
#include <regex>
#include <exception>
using namespace std;


// Helper types
typedef std::vector<lstring> StringList;
typedef std::vector<std::regex> PatternList;
typedef unsigned int uint;

// Runtime options
std::regex fromPat;
lstring toPat;
lstring backupDir;
PatternList includeFilePatList;
PatternList excludeFilePatList;
StringList fileDirList;
lstring outFile;
lstring printPat;

bool showPattern = false;
bool inverseMatch = false;
uint optionErrCnt = 0;
uint patternErrCnt = 0;

lstring printPosFmt = "%f(%o)\n";
lstring cwd;    // current working directory

// Working values
bool doReplace = false;
std::vector<char> buffer;

#if 0
// Sample replace fragment.
    std::regex specialCharRe("[*?-]+");
    std::regex_constants::match_flag_type flags = std::regex_constants::match_default;
    title = std::regex_replace(title, specialCharRe, "_", flags);
    std::replace(title.begin(), title.end(), SLASH_CHR, '_');

#endif



Filter nopFilter;
LineFilter lineFilter;
Filter* pFilter = &nopFilter;


// ---------------------------------------------------------------------------
// Dump String, showing non-printable has hex value.
static void DumpStr(const char* label, const std::string& str) {
    if (showPattern)
    {
        std::cerr << "Pattern " << label << " length=" << str.length() << std::endl;
        for (int idx = 0; idx < str.length(); idx++)
        {
            std::cerr << "  [" << idx << "]";
            if (isprint(str[idx]))
                std::cerr << str[idx] << std::endl;
            else
                std::cerr << "(hex) " << std::hex << (unsigned)str[idx] << std::dec << std::endl;
        }
        std::cerr << "[end-of-pattern]\n";
    }
}

// ---------------------------------------------------------------------------
// Convert special characters from text to binary.
static std::string& ConvertSpecialChar(std::string& inOut)
{
    uint len = 0;
    int x, n;
    const char *inPtr = inOut.c_str();
    char* outPtr = (char*)inPtr;
    while (*inPtr)
    {
        if (*inPtr == '\\')
        {
            inPtr++;
            switch (*inPtr)
            {
                case 'n': *outPtr++ = '\n'; break;
                case 't': *outPtr++ = '\t'; break;
                case 'v': *outPtr++ = '\v'; break;
                case 'b': *outPtr++ = '\b'; break;
                case 'r': *outPtr++ = '\r'; break;
                case 'f': *outPtr++ = '\f'; break;
                case 'a': *outPtr++ = '\a'; break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                    sscanf(inPtr,"%3o%n",&x,&n);
                    inPtr += n-1;
                    *outPtr++ = (char)x;
                    break;
                case 'x':								// hexadecimal
                    sscanf(inPtr+1,"%2x%n",&x,&n);
                    if (n>0)
                    {
                        inPtr += n;
                        *outPtr++ = (char)x;
                        break;
                    }
                    // seep through
                default:  
                    throw( "Warning: unrecognized escape sequence" );
                case '\\':
                case '\?':
                case '\'':
                case '\"':
                    *outPtr++ = *inPtr;
                    break;
            }
            inPtr++;
        }
        else
            *outPtr++ = *inPtr++;
        len++;
    }
    
    inOut.resize(len);
    return inOut;;
}

#ifdef WIN32
const char SLASH_CHAR('\\');
#else
const char SLASH_CHAR('/');
#endif

// ---------------------------------------------------------------------------
// Extract name part from path.
lstring& getName(lstring& outName, const lstring& inPath)
{
    size_t nameStart = inPath.rfind(SLASH_CHAR) + 1;
    if (nameStart == 0)
        outName = inPath;
    else
        outName = inPath.substr(nameStart);
    return outName;
}

// ---------------------------------------------------------------------------
// Return true if inPath (filename part) matches pattern in patternList
bool FileMatches(const lstring& inName, const PatternList& patternList, bool emptyResult)
{
    if (patternList.empty() || inName.empty())
        return emptyResult;
    
    for (size_t idx = 0; idx != patternList.size(); idx++)
        if (std::regex_match(inName.begin(), inName.end(), patternList[idx]))
            return true;
    
    return false;
}

// ---------------------------------------------------------------------------
void printParts(
        const char* customFmt,
        const char* filepath,
        size_t fileOffset,
        size_t matchLen,
        const lstring& matchStr)
{
    // TODO - handle custom printf syntax to get to path parts:
    //    %#.#s    s=fullpath,  p=path only, n=name only, e=extension only f=filename name+ext
    //    %0#d     o=offset,  l=length
    // printf(filepath, fileOffset, len, filepath);
    
    const int NONE = 12345;
    lstring itemFmt;
    
    char* fmt = (char*)customFmt;
    while (*fmt) {
        char c = *fmt;
        if (c != '%') {
            putchar(c);
            fmt++;
        } else {
            const char* begFmt = fmt;
            int precision = NONE;
            int width = (int)strtol(fmt+1, &fmt, 10);
            if (*fmt == '.') {
                precision = (int)strtol(fmt+1, &fmt, 10);
            }
            c = *fmt;
        
            itemFmt = begFmt;
            itemFmt.resize(fmt - begFmt);
           
            switch (c) {
                case 's':
                    itemFmt += "s";
                    printf(itemFmt, filepath);
                    break;
                case 'p':
                   itemFmt += "s";
                   printf(itemFmt, Directory_files::parts(filepath, true, false, false).c_str());
                   break;
                case 'r':   // relative path
                    itemFmt += "s";
                    printf(itemFmt, Directory_files::parts(filepath, true, false, false).replaceStr(cwd, "").c_str());
                    break;
                case 'n':
                    itemFmt += "s";
                    printf(itemFmt, Directory_files::parts(filepath, false, true, false).c_str());
                    break;
                case 'e':
                    itemFmt += "s";
                    printf(itemFmt, Directory_files::parts(filepath, false, false, true).c_str());
                    break;
                case 'f':
                    itemFmt += "s";
                    printf(itemFmt, Directory_files::parts(filepath, false, true, true).c_str());
                    break;
                case 'o':
                    itemFmt += "lu";    // unsigned long formatter
                    printf(itemFmt, fileOffset);
                    break;
                case 'l':
                    itemFmt += "lu";    // unsigned long formatter
                    printf(itemFmt, matchLen);
                    break;
                case 'm':
                    itemFmt += "s";
                    printf(itemFmt, matchStr.c_str());
                    break;
                case 't':   // match convert using printPat
                    itemFmt += "s";
                    if (printPat.length() == 0) {
                        printf("Missing -printPat=pattern");
                    } else {
                        std::regex_constants::match_flag_type flags = std::regex_constants::match_default;
                        printf(itemFmt, std::regex_replace(matchStr, fromPat, printPat, flags).c_str());
                    }
                    break;
                default:
                    putchar(c);
                    break;
            }
            fmt++;
        }
    }
}


// ---------------------------------------------------------------------------
// Find 'fromPat' in file
unsigned FindGrep(const char* filepath)
{
    lstring         filename;
    ifstream        in;
    ofstream        out;
    struct stat     filestat;
    uint            matchCnt = 0;
    
#if 1
    std::regex_constants::match_flag_type flags = std::regex_constants::match_default;
#else
    std::regex_constants::match_flag_type flags =
            std::regex_constants::match_flag_type(
                  std::regex_constants::match_default
                  + std::regex_constants::match_not_eol
                  + std::regex_constants::match_not_bol);
#endif
    
    
    try {
        if (stat(filepath, &filestat) != 0)
            return 0;
        
        in.open(filepath);
        if (in.good())
        {
            buffer.resize(filestat.st_size);
            streamsize inCnt = in.read(buffer.data(), buffer.size()).gcount();
            in.close();
            
            std::match_results <const char*> match;
            const char* begPtr = (const char*)buffer.data();
            const char* endPtr = begPtr + inCnt;
            size_t off = 0;
            pFilter->init(buffer);
            
            while (std::regex_search(begPtr, endPtr, match, fromPat, flags))
            {
                // for (auto group : match)
                //    std::cout << group << endl;
                
                size_t pos = match.position();
                size_t len = match.length();
                // size_t numMatches = match.size();
                // std::string matchedStr = match.str();
                // std::string m0 = match[0];
                
                off += pos;
                
                if (pFilter->valid(off, len)) {
                    if (!inverseMatch) {
                        printParts(printPosFmt, filepath, off, len, lstring(begPtr+pos, len));
                    }
                    matchCnt++;
                }
                
                begPtr += pos + len;
            }
            return inverseMatch ? (matchCnt>0?0:1): matchCnt;
        }
        else
        {
            cerr << "Unable to open " << filepath << endl;
        }
    }
    catch (exception ex)
    {
        cerr << "Parsing error in file:" << filepath << endl;
        cerr << ex.what() << std::endl;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Find fromPat and Replace with toPat in filepath.
//  inFilepath = full file name path
//  outfilePath = full file name paht, can be same as inFilepath
//  backupToName = name only part
bool ReplaceFile(const lstring& inFilepath, const lstring& outFilepath, const lstring& backupToName)
{
    ifstream        in;
    ofstream        out;
   
    struct stat     filestat;
    std::regex_constants::match_flag_type flags = std::regex_constants::match_default;

    try {
        if (stat(inFilepath, &filestat) != 0)
            return false;
        
        in.open(inFilepath);
        if (in.good())
        {
            buffer.resize(filestat.st_size);
            streamsize inCnt = in.read(buffer.data(), buffer.size()).gcount();
            in.close();
            
            std::match_results <const char*> match;
            const char* begPtr = (const char*)buffer.data();
            const char* endPtr = begPtr + inCnt;
            pFilter->init(buffer);
            
            // WARNING - LineFilter only validates first match and not multiple replacements.
            if (std::regex_search(begPtr, endPtr, match, fromPat, flags)
                && pFilter->valid(match.position(), match.length()))
            {
                if (!backupDir.empty())
                {
                    lstring backupFull;
                    rename(inFilepath, Directory_files::join(backupFull, backupDir, backupToName));
                }
                
                ostream* outPtr = &cout;
                if (outFilepath != "-") {
                    out.open(outFilepath);
                    if (out.is_open())
                    {
                        outPtr = &out;
                    } else {
                        cerr << strerror(errno) << ", Unable to write to " << outFilepath << endl;
                        return false;
                    }
                }
 
                // TODO - support LineFilter validation.
                std::regex_replace(std::ostreambuf_iterator<char>(*outPtr),
                        begPtr,
                        endPtr,
                        fromPat, toPat, flags);
          
                if (out.is_open()) {
                    out.close();
                }
                return true;
            }
        }
        else
        {
            cerr << strerror(errno) << ", Unable to open " << inFilepath << endl;
        }
    }
    catch (exception ex)
    {
        cerr << ex.what() << ", Error in file:" << inFilepath << endl;
    }
    return false;
}


// ---------------------------------------------------------------------------
static size_t ReplaceFile(const lstring& inFullname)
{
    size_t fileCount = 0;
    lstring name;
    getName(name, inFullname);
    
    if (!name.empty()
        && !FileMatches(name, excludeFilePatList, false)
        && FileMatches(name, includeFilePatList, true))
    {
        if (doReplace)
        {
            string outFullname = (outFile.length() != 0) ? outFile : inFullname;
            if (ReplaceFile(inFullname, outFullname, name))
            {
                fileCount++;
                // printParts(printPosFmt, fullname, 0, 0, "");
            }
        }
        else
        {
            unsigned matchCnt = FindGrep(inFullname);
            if (matchCnt != 0)
            {
                fileCount++;
                // printParts(printPosFmt, fullname, 0, 0, "");
            }
        }
    }
    
    return fileCount;
}

// ---------------------------------------------------------------------------
static size_t ReplaceFiles(const lstring& dirname)
{
    Directory_files directory(dirname);
    lstring fullname;
    
    size_t fileCount = 0;
    
    struct stat filestat;
    try {
        if (stat(dirname, &filestat) == 0 && S_ISREG(filestat.st_mode))
        {
            fileCount += ReplaceFile(dirname);
        }
    }
    catch (exception ex)
    {
        // Probably a pattern, let directory scan do its magic.
    }

    while (directory.more())
    {
        directory.fullName(fullname);
        if (directory.is_directory())
        {
            fileCount += ReplaceFiles(fullname);
        }
        else if (fullname.length() > 0)
        {
            fileCount += ReplaceFile(fullname);
        }
    }
    
    return fileCount;
}


// ---------------------------------------------------------------------------
// Return compiled regular expression from text.
std::regex getRegEx(const char* value)
{
    try {
        std::string valueStr(value);
        ConvertSpecialChar(valueStr);
        DumpStr("From", valueStr);
        return std::regex(valueStr);
        // return std::regex(valueStr, regex_constants::icase);
    }
    catch (const std::regex_error& regEx)
    {
        std::cerr << regEx.what() << ", Pattern=" << value << std::endl;
    }
    
    patternErrCnt++;
    return std::regex("");
}

// ---------------------------------------------------------------------------
// Validate option matchs and optionally report problem to user.
bool ValidOption(const char* validCmd, const char* possibleCmd, bool reportErr = true)
{
    // Starts with validCmd else mark error
    size_t validLen = strlen(validCmd);
    size_t possibleLen = strlen(possibleCmd);
    
    if ( strncasecmp(validCmd, possibleCmd, std::min(validLen, possibleLen)) == 0)
        return true;
    
    if (reportErr)
    {
        std::cerr << "Unknown option:'" << possibleCmd << "', expect:'" << validCmd << "'\n";
        optionErrCnt++;
    }
    return false;
}

// ---------------------------------------------------------------------------
template<typename TT>
std::string stringer(const TT& value) {
    return string(value);
}
template<typename TT, typename ... Args >
std::string stringer(const TT& value, const Args& ... args) {
    return string(value) + stringer(args...);
}

// ---------------------------------------------------------------------------
void help(const char* argv0) {
    const char* helpMsg = "  Dennis Lang v1.4 (LandenLabs.com) " __DATE__  "\n"
        "\nDes: Replace text in files\n"
        "Use: llreplace [options] directories...\n"
        "\n"
        "_P_Main options:_X_\n"
        "   -_y_from=<regExpression>          ; Pattern to find\n"
        "   -_y_to=<regExpression or string>  ; Optional replacment \n"
        "   -_y_backupDir=<directory>         ; Optional Path to store backup copy before change\n"
        "   -_y_out= - | outfilepath          ; Optional alternate output, default is input file \n"
        "\n"
        "   -_y_includeFiles=<filePattern>    ; Optional files to include in file scan, default=*\n"
        "   -_y_excludeFiles=<filePattern>    ; Optional files to exclude in file scan, no default\n"
        "   -_y_range=beg,end                 ; Optional line range filter \n"
        "\n"
        "   directories...                 ; Directories to scan\n"
        "\n"
        "_P_Other options:_X_\n"
        "   -_y_inverse                       ; Invert Search, show files not matching \n"
        "   -_y_printFmt='%o,%l'              ; Printf format to present match \n"
        "                                  ; Def: %f(%o)\\n \n"
        "  %s=fullpath, %p=path, %f=relative path %f=filename, %n=name only %e=extension \n"
        "  %o=character offset, %l=match length %m=match string %t=match convert using toPattern \n"
        "\n"
        "  ex: -_y_printPos='%20.20f %08o'  \n"
        "  Filename padded to 20 characters, max 20, and offset 8 digits leading zeros.\n"
        "\n"
        "_p_NOTES:\n"
        "   . (dot) does not match \\r \\n,  you need to use [\\r\\n] or  (.|\\r|\\n)* \n"
        "   Use single quotes to wrap from and/or to patterns if they use special characters\n"
        "   like $ dollar signt to prevent shell from interception it.\n"
        "\n"
        "_p_Examples\n"
        " Search only, show patterns and defaults showing file and match:\n"
        "  llreplace -_y_from='Copyright' '-_y_include=*.java' -_y_print='%r/%f\\n' src1 src2\n"
        "  llreplace -_y_from='Copyright' '-_y_include=*.java' -_y_include='*.xml' -_y_print='%s' -_y_inverse src res\n"
        "  llreplace '-_y_from=if [(]MapConfigInfo.DEBUG[)] [{][\\r\\n ]*Log[.](d|e)([(][^)]*[)];)[\\r\\n ]*[}]'  '-_y_include=*.java' -_y_range=0,10 -_y_range=20,-1 -_y_printFmt='%f %03d: ' src1 src2\n"
        "\n"
        " _P_Search and replace in-place:_X_\n"
        "  llreplace '-_y_from=if [(]MapConfigInfo.DEBUG[)] [{][\\r\\n ]*Log[.](d|e)([(][^)]*[)];)[\\r\\n ]*[}]' '-_y_to=MapConfigInfo.$1$2$3' '-_y_include=*.java' src\n"
        "\n";
    
    std::cerr << Colors::colorize(stringer("\n_W_", argv0, "_X_").c_str()) << Colors::colorize(helpMsg);
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{  
    if (argc == 1)
    {
        help(argv[0]);
    }
    else
    {
        bool doParseCmds = true;
        string endCmds = "--";
        for (int argn = 1; argn < argc; argn++)
        {
            if (*argv[argn] == '-' && doParseCmds)
            {
                lstring argStr(argv[argn]);
                Split cmdValue(argStr, "=", 2);
                if (cmdValue.size() >= 1)
                {
                    lstring cmd = cmdValue[0];
                    lstring value = cmdValue[1];
                    
                    if (cmd.length() > 1 && cmd[0] == '-')
                        cmd.erase(0);   // allow -- prefix on commands
                    
                    switch (cmd[1])
                    {
                    case 'b':   // backup path
                        if (ValidOption("backupdir", cmd+1))
                            backupDir = value;
                        break;
                    case 'f':   // from=<pat>
                        if (ValidOption("from", cmd+1))
                            ConvertSpecialChar(value);
                            fromPat = getRegEx(value);
                        break;
                    case 't':   // to=<pat>
                        if (ValidOption("to", cmd+1))
                        {
                            toPat = value;
                            ConvertSpecialChar(toPat);
                            DumpStr("To", toPat);
                            doReplace = true;
                        }
                        break;
                    case 'i':   // includeFile=<pat>
                        if (ValidOption("inverse", argStr+1, false)) {
                            inverseMatch = true;
                        }
                            
                        if (ValidOption("includefile", cmd+1, false))
                        {
                            ReplaceAll(value, "*", ".*");
                            includeFilePatList.push_back(getRegEx(value));
                        }
                        break;
                    case 'e':   // excludeFile=<pat>
                        if (ValidOption("excludefile", cmd+1))
                        {
                            ReplaceAll(value, "*", ".*");
                            excludeFilePatList.push_back(getRegEx(value));
                        }
                        break;
                           
                    case 'r':   // range=beg,end
                        if (ValidOption("range", cmd+1))
                        {
                        char* nPtr;
                        size_t n1 = strtoul(value, &nPtr, 10);
                        size_t n2 = strtoul(nPtr+1, &nPtr, 10);
                        if (n1 <= n2) {
                            pFilter = &lineFilter;
                            lineFilter.zones.push_back(Zone(n1, n2));
                        }
                        }
                        break;
                            
                    case 'o':   // alternate output
                        if (ValidOption("out", cmd+1))
                            outFile = value;
                        break;
                    case 'p':
                        if (ValidOption("printFmt", cmd+1, false)) {
                            printPosFmt = value;
                            ConvertSpecialChar(printPosFmt);
                        } else if (ValidOption("printPat", cmd+1)) {
                            printPat = value;
                            ConvertSpecialChar(printPat);
                        }
                        break;
                          
                    default:
                        std::cerr << "Unknown command " << cmd << std::endl;
                        optionErrCnt++;
                        break;
                    }
                } else {
                    if (endCmds == argv[argn]) {
                        doParseCmds = false;
                    } else {
                        switch (argStr[1])
                        {
                        case 'i':
                            inverseMatch = ValidOption("inverse", argStr+1, false);
                            break;
                        default:
                            std::cerr << "Unknown command " << argStr << std::endl;
                            optionErrCnt++;
                            break;
                        }
                    }
                }
            }
            else
            {
                // Store file directories
                fileDirList.push_back(argv[argn]);
            }
        }            
    
        if (patternErrCnt == 0 && optionErrCnt == 0 && fileDirList.size() != 0)
        {
            char cwdTmp[256];
            cwd = getcwd(cwdTmp, sizeof(cwdTmp));
            cwd += Directory_files::SLASH;
            
            if (!toPat.empty() && pFilter == &lineFilter) {
                cerr << "\a\nRange filter does not work for replacement only searching\a\n" << std::endl;
            }
            
            if (fileDirList.size() == 1 && fileDirList[0] == "-") {
                string filePath;
                while (std::getline(std::cin, filePath)) {
                    std::cerr << ReplaceFiles(filePath) << std::endl;
                }
            } else {
                for (auto const& filePath : fileDirList)
                {
                    std::cerr << ReplaceFiles(filePath) << std::endl;
                }
            }
        } else {
            help(argv[0]);
        }
        
        std::cerr << std::endl;
    }

    return 0;
}
