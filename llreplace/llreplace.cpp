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

// Project files
#include "ll_stdhdr.hpp"
#include "directory.hpp"
#include "split.hpp"
#include "filters.hpp"
#include "colors.hpp"

#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdlib.h>     // stroul

#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <regex>
#include <exception>
#include <chrono>   // Timing program execution

using namespace std;

// Helper types
typedef std::vector<lstring> StringList;
typedef std::vector<std::regex> PatternList;
typedef unsigned int uint;

// Runtime options
std::regex fromPat;
enum FindMode { FROM, FROM_TILL, FROM_UNTIL };
FindMode findMode = FROM;
std::regex tillPat;
std::regex untilPat;
lstring toPat;

// TODO - should this be moved into Range Filter, also allow array of these
std::regex beginPat;
std::regex ignorePat;
std::regex endPat;

lstring backupDir;
PatternList includeFilePatList;
PatternList excludeFilePatList;
StringList fileDirList;
lstring outFile;
lstring printPat;

bool isVerbose = false;
bool doLineByLine = false;      // What is this for ?, should it be a switch
bool showPattern = false;
bool inverseMatch = false;
bool canForce = false;          // Can update read-only file.
uint optionErrCnt = 0;
uint patternErrCnt = 0;
std::set<std::string> parseArgSet;
const char EOL_CHR = '\n';
const char* EOL_STR = "\n";

lstring printPosFmt = "%r/%f(%o) %l\n";
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

#if 1
std::regex_constants::match_flag_type rxFlags =
    std::regex_constants::match_flag_type(
        std::regex_constants::match_default
        + std::regex_constants::extended
    );
#else
std::regex_constants::match_flag_type rxFlags =
    std::regex_constants::match_flag_type(
        std::regex_constants::match_default
        + std::regex_constants::match_not_eol
        + std::regex_constants::match_not_bol);
#endif


Filter nopFilter;
LineFilter lineFilter;
BufferFilter bufferFilter;
Filter* pFilter = &nopFilter;

unsigned long g_regSearchCnt = 0;
unsigned long g_fileCnt = 0;

// ---------------------------------------------------------------------------
// Dump String, showing non-printable has hex value.
static void DumpStr(const char* label, const std::string& str) {
    if (showPattern) {
        std::cerr << "Pattern " << label << " length=" << str.length() << std::endl;
        for (int idx = 0; idx < str.length(); idx++) {
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
static std::string& ConvertSpecialChar(std::string& inOut) {
    uint len = 0;
    int x, n, scnt;
    const char* inPtr = inOut.c_str();
    char* outPtr = (char*)inPtr;
    while (*inPtr) {
        if (*inPtr == '\\') {
            inPtr++;
            switch (*inPtr) {
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
                scnt = sscanf(inPtr, "%3o%n", &x, &n);  // "\010" octal sets x=8 and n=3 (characters)
                assert(scnt == 1);
                inPtr += n - 1;
                *outPtr++ = (char)x;
                break;
            case 'x':								// hexadecimal
                scnt = sscanf(inPtr + 1, "%2x%n", &x, &n);  // "\1c" hex sets x=28 and n=2 (characters)
                assert(scnt == 1);
                if (n > 0) {
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
        } else
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
lstring& getName(lstring& outName, const lstring& inPath) {
    size_t nameStart = inPath.rfind(SLASH_CHAR) + 1;
    if (nameStart == 0)
        outName = inPath;
    else
        outName = inPath.substr(nameStart);
    return outName;
}

// ---------------------------------------------------------------------------
// Return true if inPath (filename part) matches pattern in patternList
bool FileMatches(const lstring& inName, const PatternList& patternList, bool emptyResult) {
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
    const lstring& matchStr,
    const char* begLinePtr) {
    // TODO - handle custom printf syntax to get to path parts:
    //    %#.#s    s=fullpath,  p=path only, n=name only, e=extension only f=filename name+ext
    //    %0#d     o=offset,  l=length
    // printf(filepath, fileOffset, len, filepath);

    const int NONE = 12345;
    lstring itemFmt;
    size_t tmpLen;

    char* fmt = (char*)customFmt;
    while (*fmt) {
        char c = *fmt;
        if (c != '%') {
            putchar(c);
            fmt++;
        } else {
            const char* begFmt = fmt;
            int precision = NONE;
            int width = (int)strtol(fmt + 1, &fmt, 10);
            if (*fmt == '.') {
                precision = (int)strtol(fmt + 1, &fmt, 10);
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
            case 'z':
                itemFmt += "lu";    // unsigned long formatter
                printf(itemFmt, matchLen);
                break;
            case 'm':
                itemFmt += "s";
                printf(itemFmt, matchStr.c_str());
                break;
            case 'l':
                tmpLen = strcspn(begLinePtr, EOL_STR);
                itemFmt += "s";
                printf(itemFmt, lstring(begLinePtr, tmpLen).c_str());
                break;
            case 't':   // match convert using printPat
                itemFmt += "s";
                if (printPat.length() == 0) {
                    printf("Missing -printPat=pattern");
                } else {
                    printf(itemFmt, std::regex_replace(matchStr, fromPat, printPat, rxFlags).c_str());
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
const char* strchrRev(const char* sPtr, char want) {
    while (*sPtr != want) {
        sPtr--;
    }
    return sPtr;
}

// ---------------------------------------------------------------------------
// Find 'fromPat' in file
unsigned FindFileGrep(const char* filepath) {
    lstring         filename;
    ifstream        in;
    ofstream        out;
    struct stat     filestat;
    uint            matchCnt = 0;


    try {
        if (stat(filepath, &filestat) != 0)
            return 0;

        in.open(filepath);
        if (in.good() && filestat.st_size > 0) {
            buffer.resize(filestat.st_size);
            buffer[0] = EOL_CHR;
            streamsize inCnt = in.read(buffer.data() + 1, buffer.size()).gcount();
            in.close();

            std::match_results <const char*> match;
            const char* begPtr = (const char*)buffer.data() + 1;
            const char* endPtr = begPtr + inCnt;
            size_t off = 0;
            pFilter->init(buffer);

            g_fileCnt++;
            while (std::regex_search(begPtr, endPtr, match, fromPat, rxFlags)) {
                g_regSearchCnt++;
                // for (auto group : match)
                //    std::cout << group << endl;

                size_t pos = match.position();
                size_t len = match.length();
                // size_t numMatches = match.size();
                // std::string matchedStr = match.str();
                // std::string m0 = match[0];

                off += pos;

                if (pFilter->valid(off, len)) {
                    if (! inverseMatch) {
                        printParts(printPosFmt, filepath, off, len, lstring(begPtr + pos, len), strchrRev(begPtr + pos, EOL_CHR) +1);
                    }
                    matchCnt++;
                }

                begPtr += pos + len;
            }
            return inverseMatch ? (matchCnt > 0 ? 0 : 1) : matchCnt;
        } else {
            cerr << "Unable to open " << filepath << endl;
        }
    } catch (exception ex) {
        cerr << "Parsing error in file:" << filepath << endl;
        cerr << ex.what() << std::endl;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Find 'fromPat' in file
unsigned FindLineGrep(const char* filepath) {
    lstring         filename;
    ifstream        in;
    ofstream        out;
    struct stat     filestat;
    uint            matchCnt = 0;


    try {
        if (stat(filepath, &filestat) != 0)
            return 0;

        in.open(filepath);
        if (in.good()) {
            // std::match_results <std::string> match;
            std::smatch match;

            size_t off = 0;

            pFilter->init(buffer);
            std::string lineBuffer;

            g_fileCnt++;
            while (getline(in, lineBuffer)) {
                if (std::regex_search(lineBuffer.cbegin(), lineBuffer.cend(), match, fromPat, rxFlags))   {
                    g_regSearchCnt++;
                    // for (auto group : match)
                    //    std::cout << group << endl;

                    size_t pos = match.position();
                    size_t len = match.length();
                    // size_t numMatches = match.size();
                    // std::string matchedStr = match.str();
                    // std::string m0 = match[0];

                    off += pos;

                    if (pFilter->valid(off, len)) {
                        if (! inverseMatch) {
                            printParts(printPosFmt, filepath, off, len, lineBuffer.substr(pos, len), lineBuffer.c_str());
                        }
                        matchCnt++;
                    }
                }
            }
            return inverseMatch ? (matchCnt > 0 ? 0 : 1) : matchCnt;
        } else {
            cerr << "Unable to open " << filepath << endl;
        }
    } catch (exception ex) {
        cerr << "Parsing error in file:" << filepath << endl;
        cerr << ex.what() << std::endl;
    }

    return 0;
}


// ---------------------------------------------------------------------------
static bool isWriteableFile(const struct stat& info) {

#ifdef HAVE_WIN
    size_t mask = _S_IFREG + _S_IWRITE;
#else
    size_t mask = S_IFREG + S_IWRITE;
#endif
    return ((info.st_mode & mask) == mask);
}

// ---------------------------------------------------------------------------
static bool makeWriteableFile(const char* filePath, struct stat* info) {
    struct stat tmpStat;

    if (info == nullptr) {
        info = &tmpStat;
        if (stat(filePath, info) != 0)
            return false;
    }
#ifdef HAVE_WIN
    size_t mask = _S_IFREG + _S_IWRITE;
    return _chmod(filePath, info.st_mode | mask) == 0;
#else
    size_t mask = S_IFREG + S_IWRITE;
    return chmod(filePath, info->st_mode | mask) == 0;
#endif
}

// ---------------------------------------------------------------------------
// Find fromPat and Replace with toPat in filepath.
//  inFilepath = full file name path
//  outfilePath = full file name paht, can be same as inFilepath
//  backupToName = name only part
bool ReplaceFile(const lstring& inFilepath, const lstring& outFilepath, const lstring& backupToName) {
    ifstream        in;
    ofstream        out;

    struct stat     filestat;

    try {
        if (stat(inFilepath, &filestat) != 0)
            return false;

        in.open(inFilepath);
        if (in.good()) {
            g_fileCnt++;
            buffer.resize(filestat.st_size);
            streamsize inCnt = in.read(buffer.data(), buffer.size()).gcount();
            in.close();

            std::match_results <const char*> match;
            std::match_results <const char*> matchEnd;
            const char* begPtr = (const char*)buffer.data();
            const char* endPtr = begPtr + inCnt;
            pFilter->init(buffer);

            // WARNING - LineFilter only validates first match and not multiple replacements.
            if (std::regex_search(begPtr, endPtr, match, fromPat, rxFlags)
                && pFilter->valid(match.position(), match.length())) {
                g_regSearchCnt++;
                if (! backupDir.empty()) {
                    lstring backupFull;
                    rename(inFilepath, Directory_files::join(backupFull, backupDir, backupToName));
                }

                ostream* outPtr = &cout;
                if (outFilepath != "-") {
                    if (canForce)
                        makeWriteableFile(outFilepath, nullptr);
                    out.open(outFilepath);
                    if (out.is_open()) {
                        outPtr = &out;
                    } else {
                        cerr << strerror(errno) << ", Unable to write to " << outFilepath << endl;
                        return false;
                    }
                }

                // TODO - support tillPat and untilPat
                // Compute region of match and replace it.
                size_t offset;
                switch (findMode) {
                case FROM:
                    // TODO - support LineFilter validation.
                    std::regex_replace(std::ostreambuf_iterator<char>(*outPtr),
                        begPtr,
                        endPtr,
                        fromPat, toPat, rxFlags);
                    break;
                case FROM_TILL:
                    do {
                        outPtr->write(begPtr, match.position());
                        offset = match.position() +  match.length();
                        if (std::regex_search(begPtr + offset, endPtr, matchEnd, tillPat, rxFlags)) {
                            (*outPtr) << toPat;
                            offset += matchEnd.position() + matchEnd.length();
                        }
                        begPtr += offset;
                    } while (std::regex_search(begPtr, endPtr, match, fromPat, rxFlags));
                    outPtr->write(begPtr, endPtr - begPtr);
                    break;
                case FROM_UNTIL:
                    do {
                        outPtr->write(begPtr, match.position());
                        offset = match.position() +  match.length();
                        if (std::regex_search(begPtr + offset, endPtr, matchEnd, tillPat, rxFlags)) {
                            (*outPtr) << toPat;
                            offset += matchEnd.position();
                        }
                        begPtr += offset;
                    } while (std::regex_search(begPtr, endPtr, match, fromPat, rxFlags));
                    outPtr->write(begPtr, endPtr - begPtr);
                    break;
                }

                if (out.is_open()) {
                    out.close();
                }
                return true;
            }
        } else {
            cerr << strerror(errno) << ", Unable to open " << inFilepath << endl;
        }
    } catch (exception ex) {
        cerr << ex.what() << ", Error in file:" << inFilepath << endl;
    }
    return false;
}


// ---------------------------------------------------------------------------
static size_t ReplaceFile(const lstring& inFullname) {
    size_t fileCount = 0;
    lstring name;
    getName(name, inFullname);

    if (! name.empty()
        && ! FileMatches(name, excludeFilePatList, false)
        && FileMatches(name, includeFilePatList, true)) {
        if (doReplace) {
            string outFullname = (outFile.length() != 0) ? outFile : inFullname;
            if (ReplaceFile(inFullname, outFullname, name)) {
                fileCount++;
                // printParts(printPosFmt, fullname, 0, 0, "");
            }
        } else {
            unsigned matchCnt;
            if (doLineByLine) {
                matchCnt = FindLineGrep(inFullname);
            } else {
                matchCnt = FindFileGrep(inFullname);
            }
            if (isVerbose) {
                std::cerr << "Match Found=" << matchCnt  << " in " << inFullname << std::endl;
            }
            if (matchCnt != 0) {
                fileCount++;
                // printParts(printPosFmt, fullname, 0, 0, "");
            }
        }
    }

    return fileCount;
}

// ---------------------------------------------------------------------------
static size_t ReplaceFiles(const lstring& dirname) {
    Directory_files directory(dirname);
    lstring fullname;

    size_t fileCount = 0;

    struct stat filestat;
    try {
        if (stat(dirname, &filestat) == 0 && S_ISREG(filestat.st_mode)) {
            fileCount += ReplaceFile(dirname);
        }
    } catch (exception ex) {
        // Probably a pattern, let directory scan do its magic.
    }

    while (directory.more()) {
        directory.fullName(fullname);
        if (directory.is_directory()) {
            fileCount += ReplaceFiles(fullname);
        } else if (fullname.length() > 0) {
            fileCount += ReplaceFile(fullname);
        }
    }

    return fileCount;
}


// ---------------------------------------------------------------------------
// Return compiled regular expression from text.
std::regex getRegEx(const char* value) {
    try {
        std::string valueStr(value);
        ConvertSpecialChar(valueStr);
        DumpStr("From", valueStr);
        return std::regex(valueStr);
        // return std::regex(valueStr, regex_constants::icase);
    } catch (const std::regex_error& regEx) {
        std::cerr << regEx.what() << ", Pattern=" << value << std::endl;
    }

    patternErrCnt++;
    return std::regex("");
}

// ---------------------------------------------------------------------------
// Validate option matchs and optionally report problem to user.
bool ValidOption(const char* validCmd, const char* possibleCmd, bool reportErr = true) {
    // Starts with validCmd else mark error
    size_t validLen = strlen(validCmd);
    size_t possibleLen = strlen(possibleCmd);

    if ( strncasecmp(validCmd, possibleCmd, std::min(validLen, possibleLen)) == 0) {
        parseArgSet.insert(validCmd);
        return true;
    }

    if (reportErr) {
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
void showHelp(const char* argv0) {
    const char* helpMsg =
        "  Dennis Lang v1.6 (LandenLabs.com) " __DATE__  "\n"
        "\nDes: Replace text in files\n"
        "Use: llreplace [options] directories...\n"
        "\n"
        "_P_Main options:_X_\n"
        "   -_y_from=<regExpression>          ; Pattern to find\n"
        "   -_y_till<regExpression>           ;   Optional end pattern to find\n"
        "   -_y_until<regExpression>          ;   Optional end pattern to find\n"
        "   -_y_to=<regExpression or string>  ; Optional replacment \n"
        "   -_y_backupDir=<directory>         ; Optional Path to store backup copy before change\n"
        "   -_y_out= - | outfilepath          ; Optional alternate output, default is input file \n"
        "\n"
        "   -_y_includeFiles=<filePattern>    ; Optional files to include in file scan, default=*\n"
        "   -_y_excludeFiles=<filePattern>    ; Optional files to exclude in file scan, no default\n"
        "   -_y_range=beg,end                 ; Optional line range filter \n"
        "   -_y_force                         ; Allow updates on read-only files \n"
        "\n"
        "   directories...                 ; Directories to scan\n"
        "\n"
        "_P_Other options:_X_\n"
        "   -_y_inverse                       ; Invert Search, show files not matching \n"
        "   -_y_printFmt=' %r/%f(%o) %l\\n'       ; Printf format to present match \n"
        "  %s=fullpath, %p=path, %r=relative path, %f=filename, %n=name only %e=extension \n"
        "  %o=character offset, %z=match length %m=match string %t=match convert using toPattern \n"
        "  %l match line \n"
        "\n"
        "  ex: -_y_printFmt='%20.20f %08o\\n'  \n"
        "  Filename padded to 20 characters, max 20, and offset 8 digits leading zeros.\n"
        "\n"
        "_p_NOTES:\n"
        "   . (dot) does not match \\r \\n,  you need to use [\\r\\n] or  (.|\\r|\\n)* \n"
        "   Use lookahead for negative condition with dot, ex: \"(?!</section).\"  Full patter below\n"
        "   Use single quotes to wrap from and/or to patterns if they use special characters\n"
        "   like $ dollar sign to prevent shell from interception it.\n"
        "\n"
        "_p_Examples\n"
        " Search only, show patterns and defaults showing file and match:\n"
        "  llreplace -_y_from='Copyright' '-_y_include=*.java' -_y_print='%r/%f\\n' src1 src2\n"
        "  llreplace -_y_from='Copyright' '-_y_include=*.java' -_y_include='*.xml' -_y_print='%s' -_y_inverse src res\n"
        "  llreplace '-_y_from=if [(]MapConfigInfo.DEBUG[)] [{][\\r\\n ]*Log[.](d|e)([(][^)]*[)];)[\\r\\n ]*[}]'  '-_y_include=*.java' -_y_range=0,10 -_y_range=20,-1 -_y_printFmt='%f %03d: ' src1 src2\n"
        "  llreplace -_y_printFmt=\"%m\\n\" -_y_from=\"<section id='trail-stats'>((?!</section).|\\r|\\n)*</section>\" \n"
        "\n"
        " _P_Search and replace in-place:_X_\n"
        "  llreplace '-_y_from=if [(]MapConfigInfo.DEBUG[)] [{][\\r\\n ]*Log[.](d|e)([(][^)]*[)];)[\\r\\n ]*[}]' '-_y_to=MapConfigInfo.$1$2$3' '-_y_include=*.java' src\n"
        "  llreplace '-_y_from=<block>' -till='</block>' '-_y_to=' '-_y_include=*.xml' res\n"
        "\n";

    std::cerr << Colors::colorize(stringer("\n_W_", argv0, "_X_").c_str()) << Colors::colorize(helpMsg);
}

// ---------------------------------------------------------------------------
void showUnknown(const char* argStr) {
    std::cerr << Colors::colorize("Use -h for help.\n_Y_Unknown option _R_") << argStr << Colors::colorize("_X_\n");
    optionErrCnt++;
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc == 1) {
        showHelp(argv[0]);
    } else {
        bool doParseCmds = true;
        string endCmds = "--";
        for (int argn = 1; argn < argc; argn++) {
            if (*argv[argn] == '-' && doParseCmds) {
                lstring argStr(argv[argn]);
                Split cmdValue(argStr, "=", 2);
                if (argStr.find("=") != string::npos && cmdValue.size() >= 1) {
                    lstring cmd = cmdValue[0];
                    lstring value = cmdValue[1];
                    
                    if (cmd.length() > 1 && cmd[0] == '-')
                        cmd.erase(0);   // allow -- prefix on commands
                    
                    const char* cmdName = cmd + 1;
                    switch (cmd[1]) {
                    case 'b':   // backup path
                        if (ValidOption("backupdir", cmdName, false))
                            backupDir = value;
                        else if (ValidOption("begin", cmdName, false)) {
                            ConvertSpecialChar(value);
                            beginPat = getRegEx(value);
                            // doLineByLine = value.find("\n") == std::string::npos;
                        }
                        break;
                    case 'f':   // from=<pat>
                        if (ValidOption("from", cmdName)) {
                            ConvertSpecialChar(value);
                            fromPat = getRegEx(value);
                            findMode = FROM;
                            // doLineByLine = value.find("\n") == std::string::npos;
                        }
                        break;

                    case 'i':   // includeFile=<pat>
                        if (ValidOption("ignore", cmdName, false)) {
                            ConvertSpecialChar(value);
                            ignorePat = getRegEx(value);
                            // doLineByLine = value.find("\n") == std::string::npos;
                        } else if (ValidOption("includeFile", cmdName))  {
                            ReplaceAll(value, "*", ".*");
                            includeFilePatList.push_back(getRegEx(value));
                        }
                        break;
                    case 'e':   // excludeFile=<pat>
                        if (ValidOption("excludeFile", cmdName, false)) {
                            ReplaceAll(value, "*", ".*");
                            excludeFilePatList.push_back(getRegEx(value));
                        } else  if (ValidOption("end", cmdName)) {
                            ConvertSpecialChar(value);
                            endPat = getRegEx(value);
                            // doLineByLine = value.find("\n") == std::string::npos;
                        }
                        break;

                    case 'r':   // range=beg,end
                        if (ValidOption("range", cmdName))  {
                            char* nPtr;
                            size_t n1 = strtoul(value, &nPtr, 10);
                            size_t n2 = strtoul(nPtr + 1, &nPtr, 10);
                            if (n1 <= n2) {
                                // TODO - should BeginPat, IgnorePat, EndPat be part of range filter
                                pFilter = &bufferFilter;
                                lineFilter.zones.push_back(Zone(n1, n2));
                                bufferFilter.zones.push_back(Zone(n1, n2));
                            }
                        }
                        break;

                    case 'o':   // alternate output
                        if (ValidOption("out", cmdName))
                            outFile = value;
                        break;
                    case 'p':
                        if (ValidOption("printFmt", cmdName, false)) {
                            printPosFmt = value;
                            ConvertSpecialChar(printPosFmt);
                        } else if (ValidOption("printPat", cmdName)) {
                            printPat = value;
                            ConvertSpecialChar(printPat);
                        }
                        break;

                    case 't':   // to=<pat>
                        if (ValidOption("till", cmdName, false))  {
                            ConvertSpecialChar(value);
                            tillPat = getRegEx(value);
                            findMode = FROM_TILL;
                            doLineByLine = true;
                        } else if (ValidOption("to", cmdName))  {
                            toPat = value;
                            ConvertSpecialChar(toPat);
                            DumpStr("To", toPat);
                            doReplace = true;
                        }
                        break;

                    case 'u':   // until=<pat>
                        if (ValidOption("until", cmdName)) {
                            ConvertSpecialChar(value);
                            untilPat = getRegEx(value);
                            findMode = FROM_UNTIL;
                            doLineByLine = true;
                        }
                        break;

                    default:
                        showUnknown(argStr);
                        break;
                    }
                } else {
                    if (endCmds == argv[argn]) {
                        doParseCmds = false;
                    } else {
                        const char* cmdName = argStr + 1;
                        switch (argStr[1]) {
                        case 'f':
                            canForce = ValidOption("force", cmdName, true);
                            break;
                        case 'i':
                            inverseMatch = ValidOption("inverse", cmdName, true);
                            break;
                        case 'v':
                            isVerbose = ValidOption("verbose", cmdName, true);
                            break;
                        default:
                            showUnknown(argStr);
                            break;
                        }
                    }
                }
            } else {
                // Store file directories
                fileDirList.push_back(argv[argn]);
            }
        }

        if (parseArgSet.count("from") == 0) {
            std::cerr << "Missing -from='pattern' \n";
            return 0;
        }

        if (patternErrCnt == 0 && optionErrCnt == 0 && fileDirList.size() != 0) {
            // Get starting timepoint
            auto start = std::chrono::high_resolution_clock::now();

            char cwdTmp[256];
            cwd = getcwd(cwdTmp, sizeof(cwdTmp));
            cwd += Directory_files::SLASH;

            if (! toPat.empty() && pFilter != &nopFilter) {
                cerr << "\a\nRange filter does not work for replacement only searching\a\n" << std::endl;
            }
            if (pFilter != &nopFilter) {
                pFilter = doLineByLine ? ((Filter*)&lineFilter) : ((Filter*)&bufferFilter);
            }

            size_t fileMatchCnt = 0;
            if (fileDirList.size() == 1 && fileDirList[0] == "-") {
                string filePath;
                while (std::getline(std::cin, filePath)) {
                    fileMatchCnt += ReplaceFiles(filePath);
                }
            } else {
                for (auto const& filePath : fileDirList) {
                    fileMatchCnt += ReplaceFiles(filePath);
                }
            }

            // Get ending timepoint
            auto stop = std::chrono::high_resolution_clock::now();
            auto minutes = std::chrono::duration_cast<std::chrono::minutes>(stop - start);
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(stop - start);
            auto milli = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

            std::cerr << "\n";
            if (minutes.count() > 5) {
                std::cerr << "Elapsed " << minutes.count() << " minutes" << endl;
            } else if (seconds.count() > 5) {
                std::cerr << "Elapsed " << seconds.count() << " seconds" << endl;
            } else {
                std::cerr << "Elapsed " << milli.count() << " milliSeconds" << endl;
            }
            std::cerr << "FilesChecked=" << g_fileCnt << endl;
            std::cerr << "FilesMatched=" << fileMatchCnt << endl;
            if (toPat.empty() || doLineByLine) {
                std::cerr << "LineMatches=" <<  g_regSearchCnt << endl;
            }

        } else {
            showHelp(argv[0]);
        }

        std::cerr << std::endl;
    }

    return 0;
}
