//-------------------------------------------------------------------------------------------------
//
//  llreplace      May-2026       Dennis Lang
//
//  Regular expression file text replacement tool
//
//-------------------------------------------------------------------------------------------------
//
// Author: Dennis Lang - 2026
// https://landenlabs.com/
//
// ----- License ----
//
// Copyright (c) 2026 Dennis Lang
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

#define VERSION "v6.05.25"

// Project files
#include "ll_stdhdr.hpp"
#include "signals.hpp"
#include "parseutil.hpp"
#include "directory.hpp"
#include "filters.hpp"
#include "threader.hpp" // Defines CAN_THREAD
#include "swapstream.hpp"

#include <assert.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <semaphore>
#include <regex>
#include <exception>
#include <chrono>   // Timing program execution
#include <algorithm>   // std::replace

#ifdef HAVE_WIN
#define getcwd _getcwd

#include <sys/stat.h>
#include <direct.h> // _getcwd
#endif


// Helper types
typedef std::vector<lstring> StringList;
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
PatternList includePathPatList;
PatternList excludePathPatList;
StringList fileDirList;
lstring printPat;
lstring outFile;
ofstream out;   // If "from" only, outFile is optionally used for output report. 

bool isVerbose = false;
bool doLineByLine = false;      // What is this for ?, should it be a switch
bool showPattern = false;
bool inverseMatch = false;
bool dryRun = false;
bool canForce = false;          // Can update read-only file.
bool progress = true;           // Show progress
bool doReplace = false;
bool runWithThreads = false;
bool binaryOkay = false;
bool quiet = false;


// bool recurse = true;
// static std::binary_semaphore lockOutput{0}; // Single thread can output to console.
static std::counting_semaphore lockOutput{1};

struct OutputLockGuard {
    bool held = false;
    void lock() { if (!held) { lockOutput.acquire(); held = true; } }
    ~OutputLockGuard() { if (held) lockOutput.release(); }
};

#ifdef HAVE_WIN
lstring printPosFmt = "%r\\%f(%o) %l\n";
#else
lstring printPosFmt = "%r/%f(%o) %l\n";
#endif

lstring cwd;    // current working directory

const char EXTN_CHAR = '.';
const char EOL_CHR = '\n';
const char* EOL_STR = "\n";
const size_t KB = 1024;
const size_t MB = KB * KB;
const size_t GB = MB * KB;
const size_t MAX_FILE_SIZE_DEF = MB * 200;
size_t maxFileSize = MAX_FILE_SIZE_DEF;
const size_t MAX_LINE_LEN_DEF = 80*4;
size_t maxLineSize = MAX_LINE_LEN_DEF;

#if 0
    // Sample replace fragment.
    std::regex specialCharRe("[*?-]+");
    std::regex_constants::match_flag_type flags = std::regex_constants::match_default;
    title = std::regex_replace(title, specialCharRe, "_", flags);
    std::replace(title.begin(), title.end(), SLASH_CHR, '_');

#endif

std::regex_constants::match_flag_type rxFlags = std::regex_constants::match_default;


Filter nopFilter;
LineFilter lineFilter;
BufferFilter bufferFilter;
Filter* pFilter = &nopFilter;

unsigned long g_regSearchCnt = 0;
unsigned long g_fileCnt = 0;
unsigned long g_binaryCnt = 0;
unsigned long g_utf16Cnt = 0;



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
lstring getPartDir(const char* filepath) {
    lstring result = filepath;
    size_t endDir = result.find_last_of(Directory_files::SLASH);
    if (endDir != string::npos)
        result = result.substr(0, endDir);
    return result;
}
// ---------------------------------------------------------------------------
lstring getPartName(const char* filepath) {
    lstring result = filepath;
    size_t endDir = result.find_last_of(Directory_files::SLASH);
    if (endDir != string::npos)
        result = result.substr(endDir + 1);
    size_t endName = result.find_last_of(EXTN_CHAR);
    if (endName != string::npos)
        result.resize(endName);
    return result;
}
// ---------------------------------------------------------------------------
static lstring EMPTY_LSTR;
lstring getPartExt(const char* filepath) {
    lstring result = filepath;
    size_t pos = result.find_last_of(EXTN_CHAR);
    return (pos == std::string::npos) ? EMPTY_LSTR : result.substr(pos);
}
// ---------------------------------------------------------------------------
lstring parts(const char* filepath, bool dir, bool name, bool ext) {
    // #include <filesystem>
    // std::filesystem::path pathParts = filepath;

    lstring result;
    if (dir) {
        result += getPartDir(filepath);
    }
    if (name) {
        result += getPartName(filepath);
    }
    if (ext) {
        result += getPartExt(filepath);
    }
    return result;
}

// ---------------------------------------------------------------------------
void printParts(
    const char* customFmt,
    const char* filepath,
    size_t fileOffset,
    size_t matchLen,
    const char* matchPtr,
    const char* begLinePtr) {

    const int NONE = 12345;
    lstring itemFmt;
    size_t lineLen;
    // char c;

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

            // Default:  "%r/%f(%o) %l\n";
            // The %X  can be %<width>.<maxWidth>X  as in %20.20s 
            //  %s=entire file path
            //  %p=just directory path
            //  %r=relative directory path
            //  %n=filename only (no extension)
            //  %e=extension
            //  %f=filename with extension
            //  %o=offset into file where match found
            //  %z=match length
            //  %m=matched string
            //  %l=matched line
            //  %t=TO_REPLACEMENT  

            switch (c) {
            case 's':
                itemFmt += "s";
                printf(itemFmt, filepath);
                break;
            case 'p':
                itemFmt += "s";
                printf(itemFmt, parts(filepath, true, false, false).c_str());
                break;
            case 'r':   // relative path
                itemFmt += "s";
                printf(itemFmt, parts(filepath, true, false, false).replaceStr(cwd, "").c_str());
                break;
            case 'n':
                itemFmt += "s";
                printf(itemFmt, parts(filepath, false, true, false).c_str());
                break;
            case 'e':
                itemFmt += "s";
                printf(itemFmt, parts(filepath, false, false, true).c_str());
                break;
            case 'f':
                itemFmt += "s";
                printf(itemFmt, parts(filepath, false, true, true).c_str());
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
                c = matchPtr[matchLen];
                ((char*)matchPtr)[matchLen] = '\0';
                printf(itemFmt, matchPtr);
                ( (char*)matchPtr )[matchLen] = c;
                break;
            case 'l':
                lineLen = strcspn(begLinePtr, EOL_STR);
                if (lineLen < maxLineSize) {
                    int prefixLen = (int)(matchPtr - begLinePtr);
                    if (prefixLen > 0)
                        printf("%.*s", prefixLen, begLinePtr);
                    std::cerr << Colors::colorize("_G_");
                    printf("%.*s", (int)matchLen, matchPtr);
                    std::cerr << Colors::colorize("_X_");
                    int postLen = int(lineLen - matchLen - prefixLen);
                    if (postLen > 0)
                        printf("%.*s", postLen, matchPtr+matchLen);
                } else {
                    itemFmt += "s";
                    printf(itemFmt, lstring(begLinePtr, maxLineSize).c_str());
                }
                break;
                /* 
            case 't':   // match convert using printPat
                itemFmt += "s";
                if (printPat.length() == 0) {
                    printf("Missing -printPat=pattern");
                } else {
                    printf(itemFmt, std::regex_replace(matchPtr, fromPat, printPat, rxFlags).c_str());
                }
                break;
                */
            default:
                putchar(c);
                break;
            }
            fmt++;
        }
    }
}

// ---------------------------------------------------------------------------
const char* strchrRev(const char* sPtr, char want, size_t maxRev) {
    while (*sPtr != want && --maxRev != 0) {
        sPtr--;
    }
    return sPtr;
}

// ---------------------------------------------------------------------------
void fileProgress(const char* filePath) {
     g_fileCnt++;
    if ((g_fileCnt % 100) == 0)
        std::cerr << "\r" << "Files:" << g_fileCnt << " ";
}

// ---------------------------------------------------------------------------
// Text encoding detected from leading BOM bytes.
enum FileEncoding {
    ENC_UTF8,         // ASCII / UTF-8 without BOM (original path)
    ENC_UTF8_BOM,     // EF BB BF
    ENC_UTF16_LE,     // FF FE
    ENC_UTF16_BE      // FE FF
};

// Return encoding from leading BOM. bomLen receives bytes to skip past.
static FileEncoding detectEncoding(const char* data, size_t size, size_t& bomLen) {
    bomLen = 0;
    const unsigned char* p = (const unsigned char*)data;
    if (size >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        bomLen = 3;
        return ENC_UTF8_BOM;
    }
    if (size >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        bomLen = 2;
        return ENC_UTF16_LE;
    }
    if (size >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        bomLen = 2;
        return ENC_UTF16_BE;
    }
    return ENC_UTF8;
}

static inline void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

// Transcode UTF-16 byte stream to UTF-8 (appended to out).
static void utf16ToUtf8(const char* src, size_t srcByteLen, bool isLE, std::string& out) {
    out.reserve(out.size() + srcByteLen);
    size_t end = srcByteLen & ~(size_t)1;
    size_t i = 0;
    while (i + 1 < end) {
        uint32_t cp = isLE
            ? (uint32_t)((unsigned char)src[i] | ((unsigned char)src[i+1] << 8))
            : (uint32_t)(((unsigned char)src[i] << 8) | (unsigned char)src[i+1]);
        i += 2;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < end) {
            uint32_t low = isLE
                ? (uint32_t)((unsigned char)src[i] | ((unsigned char)src[i+1] << 8))
                : (uint32_t)(((unsigned char)src[i] << 8) | (unsigned char)src[i+1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        appendUtf8(out, cp);
    }
}

// Transcode UTF-8 to UTF-16 byte stream (appended as raw bytes to out).
static void utf8ToUtf16(const char* src, size_t srcLen, bool isLE, std::string& out) {
    out.reserve(out.size() + srcLen * 2);
    auto write16 = [&](uint16_t v) {
        if (isLE) {
            out.push_back((char)(v & 0xFF));
            out.push_back((char)((v >> 8) & 0xFF));
        } else {
            out.push_back((char)((v >> 8) & 0xFF));
            out.push_back((char)(v & 0xFF));
        }
    };
    size_t i = 0;
    while (i < srcLen) {
        uint32_t cp;
        unsigned char b0 = (unsigned char)src[i];
        if (b0 < 0x80) {
            cp = b0; i++;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < srcLen) {
            cp = ((b0 & 0x1F) << 6) | ((unsigned char)src[i+1] & 0x3F);
            i += 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < srcLen) {
            cp = ((b0 & 0x0F) << 12)
               | (((unsigned char)src[i+1] & 0x3F) << 6)
               | ((unsigned char)src[i+2] & 0x3F);
            i += 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < srcLen) {
            cp = ((b0 & 0x07) << 18)
               | (((unsigned char)src[i+1] & 0x3F) << 12)
               | (((unsigned char)src[i+2] & 0x3F) << 6)
               | ((unsigned char)src[i+3] & 0x3F);
            i += 4;
        } else {
            cp = 0xFFFD; i++;
        }
        if (cp < 0x10000) {
            write16((uint16_t)cp);
        } else {
            cp -= 0x10000;
            write16((uint16_t)(0xD800 | (cp >> 10)));
            write16((uint16_t)(0xDC00 | (cp & 0x3FF)));
        }
    }
}

// ---------------------------------------------------------------------------
bool isBinary(Buffer& buffer, struct stat& filestat, const char* fullname) {
    if (binaryOkay)
        return false;
    
    bool isBinary = false;  // filestat.st_mode & S_IXUSR
    bool isUtf16 = false;
    if (buffer.size() > KB) {
        size_t goodCnt = 0;
        size_t nullCnt = 0;
        unsigned char* cPtr = (unsigned char*)buffer.data();
        unsigned char* endPtr = cPtr + KB;
        while (cPtr < endPtr) {
            unsigned char c = *cPtr++;
            if (c >= 32 && c < 128)
                goodCnt++;
            else if (c == '\0')
                nullCnt++;
        }
        isBinary = goodCnt < (KB/2);
        isUtf16 = (nullCnt > goodCnt && nullCnt - goodCnt < KB/50); // utf16 is often [null, ascii]... so null cnt is similar to good cnt
    }
    if (isBinary) {
        g_binaryCnt++;
        if (isVerbose)
            std::cerr << "Skipping " << (isUtf16 ? "UTF16 ": "BINARY ") << fullname << std::endl;
    }
    if (isUtf16) {
        g_utf16Cnt++;
    }
    return isBinary;
}

// Forward declaration
unsigned FindLineGrep(const char* filepath);

// ---------------------------------------------------------------------------
// Find 'fromPat' in file
unsigned FindFileGrep(const char* filepath) {
    lstring         filename;
    ifstream        in;
    ofstream        out;
    struct stat     filestat;
    uint            matchCnt = 0;
    OutputLockGuard outputLock;


    try {
        if (stat(filepath, &filestat) != 0)
            return 0;
        
        if (filestat.st_size > maxFileSize) {
            Colors::showError("File too large ", filepath, " ", filestat.st_size);
            return 0;
            // return FindLineGrep(filepath);
        }
        
        in.open(filepath);
        if (in.good() && filestat.st_size > 0 && S_ISREG(filestat.st_mode)) {
            
            try {
                Buffer buffer(filestat.st_size+1);
                buffer[0] = EOL_CHR;
                streamsize inCnt = in.read(buffer.data() + 1, buffer.size()-1).gcount();
                in.close();

                size_t bomLen = 0;
                FileEncoding enc = detectEncoding(buffer.data() + 1, (size_t)inCnt, bomLen);

                std::string utf8Holder;     // populated only when transcoding UTF-16
                const char* begPtr;
                const char* endPtr;
                const char* lineRefPtr;     // base for pFilter line counting

                if (enc == ENC_UTF16_LE || enc == ENC_UTF16_BE) {
                    g_utf16Cnt++;
                    utf8Holder.push_back(EOL_CHR);  // preserve leading-EOL sentinel for strchrRev
                    utf16ToUtf8(buffer.data() + 1 + bomLen,
                                (size_t)inCnt - bomLen,
                                enc == ENC_UTF16_LE,
                                utf8Holder);
                    begPtr = utf8Holder.data() + 1;
                    endPtr = utf8Holder.data() + utf8Holder.size();
                    lineRefPtr = utf8Holder.data();
                } else {
                    if (isBinary(buffer, filestat, filepath))
                        return 0;
                    begPtr = buffer.data() + 1 + bomLen;
                    endPtr = buffer.data() + 1 + (size_t)inCnt;
                    lineRefPtr = buffer.data();
                }

                std::match_results <const char*> match;
                size_t off = 0;
                pFilter->init(lineRefPtr);

                fileProgress(filepath);   // g_fileCnt++;
                while (begPtr < endPtr && std::regex_search(begPtr, endPtr, match, fromPat, rxFlags)) {
                    g_regSearchCnt++;
                    
                    size_t pos = match.position();
                    size_t len = match.length();
                    // size_t numMatches = match.size();
                    // std::string matchedStr = match.str();
                    // std::string m0 = match[0];
                    
                    off += pos;

                    if (pFilter->valid(off, len)) {
                        if (! inverseMatch) {
                            outputLock.lock();
                            // printParts(printPosFmt, filepath, off, len, lstring(begPtr + pos, len), strchrRev(begPtr + pos, EOL_CHR) +1);
                            printParts(printPosFmt, filepath, off, len, begPtr + pos, strchrRev(begPtr + pos, EOL_CHR, min(pos, maxLineSize)) +1);
                        }
                        matchCnt++;
                    }

                    // off tracked only the match's position relative to the current begPtr;
                    // begPtr itself advances by pos+len each iteration, so off must too, or
                    // every match after the first understates its true absolute offset by
                    // the sum of all prior matches' lengths.
                    off += len;
                    begPtr += pos + len;
                }
            } catch (exception ex) {
                int err = errno;
                Colors::showError("File read exception on ", filepath, " ", strerror(err));
            }

            if (inverseMatch && matchCnt == 0) {
                char dummy[] = "\n";
                printParts(printPosFmt, filepath, 0, filestat.st_size, dummy, dummy);
            }
        } else if (in.bad()) {
            Colors::showError("Unable to open ", filepath);
        }
    } catch (exception ex) {
        Colors::showError(ex.what(), " Parsing error in file:",  filepath);
    }

    // outputLock's destructor releases lockOutput here, exactly once, iff it was
    // ever acquired above - regardless of inverseMatch or an exception in between.
    return inverseMatch ? (matchCnt > 0 ? 0 : 1) : matchCnt;
}

// ---------------------------------------------------------------------------
// Find and optionally replace 'fromPat' from stream line-by-line
unsigned FindLineGrep(const char* filepath, istream& in, ostream& out, const char* outPrefix) { // "TO="
    uint matchCnt = 0;
    std::smatch match;
    size_t off = 0;
    OutputLockGuard outputLock;

    Buffer buffer(512);
    pFilter->init(buffer.data());
    std::string lineBuffer;

    fileProgress(filepath);   // g_fileCnt++;
    while (getline(in, lineBuffer)) {
        pFilter->nextLine();   // advance the real line number regardless of match, for LineFilter's -range

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
                    outputLock.lock();

                    printParts(printPosFmt, filepath, off, len, lineBuffer.c_str()+pos, lineBuffer.c_str());
                    if (doReplace) {
                        string result;
                        std::regex_replace(std::back_inserter(result), lineBuffer.begin(), lineBuffer.end(), fromPat, toPat, rxFlags);
                        out << outPrefix << result << std::endl;
                    } else {
                        std::cerr << lineBuffer << std::endl;
                    }
                }
                matchCnt++;
            }
        }
    }
    
    return matchCnt;
}

// ---------------------------------------------------------------------------
// Find 'fromPat' in file and optionally replace line-by-line
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
        if (in.good() && filestat.st_size > 0 && S_ISREG(filestat.st_mode)) {
            
            matchCnt = FindLineGrep(filepath, in, out, "TO=");
            
            if (inverseMatch && matchCnt == 0) {
                char dummy[] = "\n";
                printParts(printPosFmt, filepath, 0, filestat.st_size, dummy, dummy);
            }
            
        } else if (!in.good()) {
            Colors::showError("Unable to open ", filepath);
        }
    } catch (exception ex) {
        Colors::showError(ex.what(), " Parsing error in file:",  filepath);
    }

    // The 4-arg FindLineGrep above now owns its own OutputLockGuard and releases
    // lockOutput itself before returning - no matching release needed here.
    return inverseMatch ? (matchCnt > 0 ? 0 : 1) : matchCnt;
}

// ---------------------------------------------------------------------------
// Find fromPat and Replace with toPat in filepath.
//  inFilepath = full file name path
//  outfilePath = full file name paht, can be same as inFilepath
//  backupToName = full path of inFilepath (NOT just the basename - two files with the
//                 same basename from different subdirectories need distinct backup
//                 names, or the second one silently overwrites the first's backup)
bool ReplaceFile(const lstring& inFilepath, const lstring& outFilepath, const lstring& backupToName) {
    ifstream        in;
    ofstream        out;
    struct stat     filestat;
    lstring         tempFilepath;   // set only once we start writing to a real (non-stdout) output file

    try {
        if (stat(inFilepath, &filestat) != 0)
            return false;
        
        if (filestat.st_size > maxFileSize) {
            Colors::showError("File too large ", inFilepath, " ", filestat.st_size);
            return 0;
        }
        
        in.open(inFilepath);
        if (in.good() && filestat.st_size > 0 && S_ISREG(filestat.st_mode)) {
            fileProgress(inFilepath);   // g_fileCnt++;
            Buffer buffer(filestat.st_size+1);
            // buffer.resize(filestat.st_size);
            streamsize inCnt = in.read(buffer.data(), buffer.size()).gcount();
            in.close();

            size_t bomLen = 0;
            FileEncoding enc = detectEncoding(buffer.data(), (size_t)inCnt, bomLen);
            bool isUtf16 = (enc == ENC_UTF16_LE || enc == ENC_UTF16_BE);

            std::string utf8Holder;     // populated only when transcoding UTF-16
            const char* begPtr;
            const char* endPtr;

            if (isUtf16) {
                g_utf16Cnt++;
                utf16ToUtf8(buffer.data() + bomLen, (size_t)inCnt - bomLen,
                            enc == ENC_UTF16_LE, utf8Holder);
                begPtr = utf8Holder.data();
                endPtr = utf8Holder.data() + utf8Holder.size();
            } else {
                if (isBinary(buffer, filestat, inFilepath))
                    return 0;
                begPtr = buffer.data() + bomLen;
                endPtr = buffer.data() + (size_t)inCnt;
            }

            std::match_results <const char*> match;
            std::match_results <const char*> matchEnd;
            pFilter->init(begPtr);

            // WARNING - LineFilter only validates first match and not multiple replacements.
            if (std::regex_search(begPtr, endPtr, match, fromPat, rxFlags)
                && pFilter->valid(match.position(), match.length())) {
                g_regSearchCnt++;
                if (! backupDir.empty()) {
                    lstring backupFull;
                    // Flatten the full path into a single unique filename (no subdirectories
                    // to create under backupDir) - using just the basename let two files with
                    // the same name from different subdirectories overwrite each other's backup.
                    lstring flatName = backupToName;
                    std::replace(flatName.begin(), flatName.end(), '/', '_');
                    std::replace(flatName.begin(), flatName.end(), '\\', '_');
                    rename(inFilepath, DirUtil::join(backupFull, backupDir, flatName));
                }

                ostream* outPtr = &cout;
                if (outFilepath != "-") {
                    if (canForce)
                        DirUtil::makeWriteableFile(outFilepath, nullptr);
                    // Write to a temp file in the same directory, then atomically rename it
                    // over outFilepath only once every byte has been written successfully.
                    // Previously outFilepath itself was opened (and truncated) directly, so a
                    // crash, kill, or exception mid-write left it corrupted with no way back -
                    // the only recovery was an explicit -backupDir, and even that only helped
                    // if the backup rename above had already completed.
                    tempFilepath = outFilepath + ".llreplace.tmp";
                    // UTF-16 round-trip needs binary mode so the OS does not translate \n -> \r\n
                    std::ios_base::openmode omode = std::ios::out
                        | (isUtf16 ? std::ios::binary : std::ios_base::openmode(0));
                    out.open(tempFilepath, omode);
                    if (out.is_open()) {
                        outPtr = &out;
                    } else {
                        Colors::showError(strerror(errno), ", Unable to write to ", tempFilepath);
                        tempFilepath.clear();
                        return false;
                    }
                }

                // Preserve a UTF-8 BOM if the input had one.
                if (enc == ENC_UTF8_BOM) {
                    outPtr->put((char)0xEF);
                    outPtr->put((char)0xBB);
                    outPtr->put((char)0xBF);
                }

                // For UTF-16 round-trip, capture replacement bytes into replacedUtf8,
                // then transcode + write with BOM. Otherwise stream directly to outPtr.
                std::string replacedUtf8;
                std::back_insert_iterator<std::string> outBack(replacedUtf8);
                std::ostreambuf_iterator<char> outStream(*outPtr);

                // Compute region of match and replace it.
                size_t offset;
                switch (findMode) {
                case FROM:
                    // TODO - support LineFilter validation.
                    if (isUtf16) {
                        std::regex_replace(outBack, begPtr, endPtr, fromPat, toPat, rxFlags);
                    } else {
                        std::regex_replace(outStream, begPtr, endPtr, fromPat, toPat, rxFlags);
                    }
                    break;
                case FROM_TILL:
                    do {
                        if (isUtf16) {
                            replacedUtf8.append(begPtr, match.position());
                        } else {
                            outPtr->write(begPtr, match.position());
                        }
                        offset = match.position() +  match.length();
                        if (std::regex_search(begPtr + offset, endPtr, matchEnd, tillPat, rxFlags)) {
                            if (isUtf16) {
                                replacedUtf8.append(toPat);
                            } else {
                                (*outPtr) << toPat;
                            }
                            offset += matchEnd.position() + matchEnd.length();
                        }
                        begPtr += offset;
                    } while (std::regex_search(begPtr, endPtr, match, fromPat, rxFlags));
                    if (isUtf16) {
                        replacedUtf8.append(begPtr, endPtr - begPtr);
                    } else {
                        outPtr->write(begPtr, endPtr - begPtr);
                    }
                    break;
                case FROM_UNTIL:
                    do {
                        if (isUtf16) {
                            replacedUtf8.append(begPtr, match.position());
                        } else {
                            outPtr->write(begPtr, match.position());
                        }
                        offset = match.position() +  match.length();
                        if (std::regex_search(begPtr + offset, endPtr, matchEnd, tillPat, rxFlags)) {
                            if (isUtf16) {
                                replacedUtf8.append(toPat);
                            } else {
                                (*outPtr) << toPat;
                            }
                            offset += matchEnd.position();
                        }
                        begPtr += offset;
                    } while (std::regex_search(begPtr, endPtr, match, fromPat, rxFlags));
                    if (isUtf16) {
                        replacedUtf8.append(begPtr, endPtr - begPtr);
                    } else {
                        outPtr->write(begPtr, endPtr - begPtr);
                    }
                    break;
                }

                if (isUtf16) {
                    bool isLE = (enc == ENC_UTF16_LE);
                    if (isLE) {
                        outPtr->put((char)0xFF); outPtr->put((char)0xFE);
                    } else {
                        outPtr->put((char)0xFE); outPtr->put((char)0xFF);
                    }
                    std::string utf16Bytes;
                    utf8ToUtf16(replacedUtf8.data(), replacedUtf8.size(), isLE, utf16Bytes);
                    outPtr->write(utf16Bytes.data(), utf16Bytes.size());
                }

                if (out.is_open()) {
                    out.close();
                }
                if (! tempFilepath.empty()) {
                    if (rename(tempFilepath, outFilepath) != 0) {
                        Colors::showError(strerror(errno), ", Unable to replace ", outFilepath, " with ", tempFilepath);
                        remove(tempFilepath);
                        return false;
                    }
                }
                return true;
            }
        } else if (!in.good()) {
            Colors::showError(strerror(errno), ", Unable to open ", inFilepath);
        }
    } catch (exception ex) {
        Colors::showError(ex.what(), ", Error in file:", inFilepath);
    }
    if (out.is_open()) {
        out.close();
    }
    if (! tempFilepath.empty()) {
        remove(tempFilepath);  // leave outFilepath untouched - only the temp copy is discarded
    }
    return false;
}

// ---------------------------------------------------------------------------
static size_t ReplaceFile(const lstring& inFullname) {
    size_t fileCount = 0;
    lstring name;
    DirUtil::getName(name, inFullname);

    if (! name.empty()
            && ! ParseUtil::FileMatches(name, excludeFilePatList, false)
            && ParseUtil::FileMatches(name, includeFilePatList, true)
            && ! ParseUtil::FileMatches(inFullname, excludePathPatList, false)
            && ParseUtil::FileMatches(inFullname, includePathPatList, true)
        ) {
        if (doReplace && !dryRun) {
            string outFullname = (outFile.length() != 0) ? outFile : inFullname;
            if (ReplaceFile(inFullname, outFullname, inFullname)) {
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
                if (matchCnt > 0)
                    std::cerr << "Match found=" << matchCnt  << " in " << inFullname << std::endl;
                else
                    std::cerr << "No match in " << inFullname << std::endl;
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
// Optional thread support

#ifdef CAN_THREAD
class ReplaceJob : public Job {
public:
    lstring fullname;
    static std::atomic<size_t> result;
    
    ReplaceJob(const lstring& _fullname) : fullname(_fullname) {
    }
    ~ReplaceJob() { }
    virtual void run() {
        result += ReplaceFile(fullname);
    }
    virtual void dump() {
        std::cerr << fullname << std::endl;
    }
};
std::atomic<size_t> ReplaceJob::result = 0;
#endif



static size_t ThreadReplaceFile(const lstring& inFullname) {
#ifdef CAN_THREAD
    if (runWithThreads) {
        Threader::runIt(new ReplaceJob(inFullname));
        return 0;
    }
#endif
    return ReplaceFile(inFullname);
}
static void ReplaceFilesInit() {
#ifdef CAN_THREAD
    if (runWithThreads) {
        std::cerr << "Running with " << Threader::maxThreads << " threads\n";
        Threader::init();
    }
#endif
}
static size_t ReplaceFilesDone() {
#ifdef CAN_THREAD
    if (runWithThreads) {
        Threader::waitForAll();
        return ReplaceJob::result;
    }
#endif
    return 0;
}

// ---------------------------------------------------------------------------
static size_t ReplaceFiles(const lstring& dirname) {
    Directory_files directory(dirname);
    lstring fullname;

    size_t fileCount = 0;

    struct stat filestat;
    try {
        int err = stat(dirname, &filestat);
        if ( err == 0) {
            if (S_ISREG(filestat.st_mode)) {
                // TODO - check if incPat and excPat are valid before starting thread.
                fileCount += ThreadReplaceFile(dirname);
            } else if (!S_ISDIR(filestat.st_mode)) {
                return 0;  // not a file or a directory
#ifndef HAVE_WIN
            } else if (access(dirname, X_OK) != 0) {
                Colors::showError("Unable to access ", dirname, " ", strerror(errno));
                return 0;
#endif
            }
        }
    } catch (exception ex) {
        // Probably a pattern, let directory scan do its magic.
        Colors::showError(ex.what());
    }

    while (!Signals::aborted && directory.more()) {
        directory.fullName(fullname);
        lstring name(directory.name());
        if (! ParseUtil::FileMatches(fullname, excludePathPatList, false)
            && ParseUtil::FileMatches(fullname, includePathPatList, true)
            && !ParseUtil::FileMatches(name, excludeFilePatList, false) ) {
            if (directory.is_directory()) {
                fileCount += ReplaceFiles(fullname);
            } else if (fullname.length() > 0 && ParseUtil::FileMatches(name, includeFilePatList, true)) {
                fileCount += ThreadReplaceFile(fullname);
            }
        }
    }

    return fileCount;
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
const char* removeQuotes(const char* value) {
#ifdef HAVE_WIN
	if (value[0] == '\'' && value[strlen(value) - 1] == '\'') {
		((char*)value)[strlen(value) - 1] = '\0';
		return value + 1;
	}
#endif
    return value;
}

// ---------------------------------------------------------------------------
void showHelp(const char* argv0) {
    const char* helpMsg =
        "  Dennis Lang " VERSION " (LandenLabs.com) " __DATE__  "\n"
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
        "   -_y_includeItem=<filePattern>     ; Include files or dir by regex match \n"
        "   -_y_excludeItem=<filePattern>     ; Exclude files or dir by regex match \n"
        "   -_y_IncludePath=<pathPattern>     ; Include path by regex match \n"
        "   -_y_ExcludePath=<pathPattern>     ; Exclude path by regex match \n"
        "   -_y_range=beg,end                 ; Optional line range filter \n"
        "\n"
        "   -_y_regex                         ; Use regex pattern not DOS pattern \n"
        "        The include/exclude patterns do an internal conversion to full regex \n"
        "          *  -> .*     and ? -> .   \n"
        "        Ex: \"*.png\"   internally becomes  \".*.png\" \n"
        "        Use -_y_regex before -inc/-exc options to force native regex pattern\n"
        "     Example to ignore all dot directories and files: \n"
        "          -_y_regex -_y_exclude=\"[.].*\" \n"
        "\n"
        "   directories...                 ; Directories or files  to scan\n"
        "\n"
        "_P_Other options:_X_\n"
        "   -_y_ignoreCase                    ; Next pattern set to ignore case \n"
        "   -_y_force                         ; Allow updates on read-only files \n"
        "   -_y_no                            ; Dry run, show changes if replacing \n"
        "   -_y_inverse                       ; Invert Search, show files not matching \n"
        "   -_y_maxFileSize=<#MB>             ; Max file size MB, def= 200 \n"
        "   -_y_maxLineSize=320               ; Max line shown using -from only \n"
        "   -_y_binary                        ; Include binary files \n"
        "   -_y_quiet                         ; Do not show matches \n"
        "   -_y_line                          ; Force line-by-line compare, def: entire file \n"
#ifdef CAN_THREAD
        "   -_y_threads                       ; Search/Replace using multiple threads \n"
        "   -_y_threads=<#threads>            ; Search/Replace using threads, capped at 10 \n"
#endif
        "\n"
        "_P_PrintFmt:_X_ \n"
#ifdef HAVE_WIN
        "   -_y_printFmt=' %r\\%f(%o) %l\\n'    ; Printf format to present match \n"
#else
        "   -_y_printFmt=' %r/%f(%o) %l\\n'    ; Printf format to present match \n"
#endif
        "    Each special character can include minWidth.maxWidth\n"
        "       %10s     = pad out to 10 wide minimum\n"
        "       %10.10s  = pad out to 10 wide min and clip to max of 10 \n"
        "       Without min or max, use entire value\n"
        "\n"
        "       %s = entire file path \n"
        "       %p = just directory path \n"
        "       %r = relative directory path \n"
        "       %n = file name only (no extension) \n"
        "       %e = extension \n"
        "       %f = filename with extension \n"
        "       %o = offset into file where match found \n"
        "       %z = match length \n"
        "       %m = matched string \n"
        "       %l = matched line  (colorized output)  \n"
        "  ex: -%_y_printFmt='%20.20f %08o\\n'  \n"
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
#ifdef HAVE_WIN
        "  llreplace -_y_from=\"Copyright\" -_y_include=*.java -_y_print='%r/%f\\n' src1 src2\n"
        "  llreplace -_y_from=\"Copyright\" -_y_include=*.java -_y_include=*.xml -_y_print='%s' -_y_inverse src res\n"
        "  llreplace -_y_from=\"d:[\\\\\\\\]\"  -to=\"c:\\\\\\\\\" ; Escape backslash and place in closure group\n"
        "  llreplace -_y_from=\"if [(]MapConfigInfo.DEBUG[)] [{][\\r\\n ]*Log[.](d|e)([(][^)]*[)];)[\\r\\n ]*[}]\"  -_y_include=*.java -_y_range=0,10 -_y_range=20,-1 -_y_printFmt=\"%f %03d: \" src1 src2\n"
#else
        "  llreplace -_y_from='Copyright' '-_y_include=*.java' -_y_print='%r/%f\\n' src1 src2\n"
        "  llreplace -_y_from='Copyright' '-_y_include=*.java' -_y_include='*.xml' -_y_print='%s' -_y_inverse src res\n"
        "  llreplace -_y_from='if [(]MapConfigInfo.DEBUG[)] [{][\\r\\n ]*Log[.](d|e)([(][^)]*[)];)[\\r\\n ]*[}]'  '-_y_include=*.java' -_y_range=0,10 -_y_range=20,-1 -_y_printFmt='%f %03d: ' src1 src2\n"
#endif
        "  llreplace -_y_printFmt=\"%m\\n\" -_y_from=\"<section id='trail-stats'>((?!</section).|\\r|\\n)*</section>\" \n"
        "\n"
        "  _y_output option can be used with search to save matches. Default is to console\n"
        "  llreplace -_y_out=matches.txt  -from=Copyright dir1 dir2 file1 file2 \n"
        "\n"
        " _P_Search and replace in-place:_X_\n"

#ifdef HAVE_WIN
        "  llreplace -_y_from=\"if [(]MapConfigInfo.DEBUG[)] [{][\\r\\n ]*Log[.](d|e)([(][^)]*[)];)[\\r\\n ]*[}]\" -_y_to=\"MapConfigInfo.$1$2$3\" -_y_include=*.java src\n"
        "  llreplace -_y_from=\"<block>\" -_y_till='</block>' '-_y_to=' '-_y_include=*.xml' res\n"
        "  llreplace -_y_from=\"http:\" -_y_to=\"https:\" -_y_Exc=*\\\\.git  . \n"
        "  llreplace -_y_from=\"http:\" -_y_to=\"https:\" -_y_Exc=*\\\\.(git||vs) . \n"
        "  llreplace -_y_from=\"http:\" -_y_to=\"https:\" -_y_regex -_y_Exc=.*\\\\[.](git||vs) . \n"
#else
        "  llreplace '-_y_from=if [(]MapConfigInfo.DEBUG[)] [{][\\r\\n ]*Log[.](d|e)([(][^)]*[)];)[\\r\\n ]*[}]' '-_y_to=MapConfigInfo.$1$2$3' '-_y_include=*.java' src\n"
        "  llreplace '-_y_from=<block>' -_y_till='</block>' '-_y_to=' '-_y_include=*.xml' res\n"
        "  llreplace -_y_from=\"http:\" -_y_to=\"https:\" -_y_Exc='*/.git'  . \n"
        "  llreplace -_y_from=\"http:\" -_y_to=\"https:\" -_y_Exc='*/.(git||vs)' . \n"
        "  llreplace -_y_from=\"http:\" -_y_to=\"https:\" -_y_regex -_y_Exc='.*/[.](git||vs)' . \n"
#endif
        "  llreplace -_y_from=\"http:\" -_y_to=\"https:\" -_y_regex -_y_exc=\"[.](git||vs)\" . \n"
        "\n"
        "  If input is dash, read from stdin, if @ read list of filepaths from stdin\n"
        "    type foo.txt | llreplace -_y_from=\"http:\" -_y_to=\"https:\" -- - >out.txt \n"
#ifdef HAVE_WIN
        "    find . -name *.txt | llreplace -_y_from=\"http:\" -_y_to=\"https:\" @ >out.txt \n"
#else
        "    find . -name \*.txt | llreplace -_y_from=\"http:\" -_y_to=\"https:\" @ >out.txt \n"
#endif
        "\n";

    std::cerr << Colors::colorize(stringer("\n_W_", argv0, "_X_").c_str()) << Colors::colorize(helpMsg);
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    Signals::init();
    ParseUtil parser;

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

                    const char* cmdName = cmd + 1;
                    if (cmd.length() > 2 && *cmdName == '-')
                        cmdName++;  // allow -- prefix on commands

                    switch (*cmdName) {
                    case 'b':   // backup path
                        if (parser.validOption("backupDir", cmdName, false))
                            backupDir = value;
                        else if (parser.validOption("begin", cmdName, false)) {
                            beginPat = parser.getRegEx(value, isVerbose);
                            // doLineByLine = value.find("\n") == std::string::npos;
                        }
                        break;
                    case 'f':   // from=<pat>
                        if (parser.validOption("from", cmdName)) {
                            fromPat = parser.getRegEx(removeQuotes(value), isVerbose);
                            findMode = FROM;
                            // doLineByLine = value.find("\n") == std::string::npos;
                        }
                        break;

                    case 'i': // -ignore=<pattern>
                        if (parser.validOption("ignore", cmdName, false)) {
                            ignorePat = parser.getRegEx(value, isVerbose);
                            // doLineByLine = value.find("\n") == std::string::npos;
                        } else {  // includeItem=<pat>
                            parser.validPattern(includeFilePatList, value, "includeItem", cmdName);
                        }
                        break;
                    case 'e':   // excludeItem=<pat>
                        if (parser.validPattern(excludeFilePatList, value, "excludeItem", cmdName, false)) {
                        } else  if (parser.validOption("end", cmdName)) {
                            endPat = parser.getRegEx(value, isVerbose);
                            // doLineByLine = value.find("\n") == std::string::npos;
                        }
                        break;
                    case 'E':   // ExcludePath=<pat>
                        parser.validPattern(excludePathPatList, value, "ExcludePath", cmdName);
                        break;
                    case 'I':   // IncludePath=<pat>
                        parser.validPattern(includePathPatList, value, "IncludePath", cmdName);
                        break;
                    case 'm':
                        if (parser.validOption("maxFileSizeMB", cmdName, false))  {
                            maxFileSize = (size_t)(atof(value) * MB);
                            maxFileSize = std::max((size_t)512, std::min(maxFileSize, GB*32));
                            std::cerr << "MaxFileSizeMB=" << maxFileSize/MB << std::endl;
                        } else if (parser.validOption("maxLineSize", cmdName))  {
                            maxLineSize = atoi(value);
                            maxLineSize = std::max((size_t)1, std::min(maxLineSize, KB));
                            std::cerr << "MaxLineSize=" << maxLineSize << std::endl;
                        }
                        break;
                    case 'r':   // range=beg,end
                        if (parser.validOption("range", cmdName))  {
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
                        if (parser.validOption("out", cmdName))
                            outFile = value;
                        break;
                    case 'p':
                        if (parser.validOption("printFmt", cmdName, false)) {
                            printPosFmt = value;
                            ParseUtil::convertSpecialChar(printPosFmt);
                        } else if (parser.validOption("printPat", cmdName)) {
                            printPat = value;
                            ParseUtil::convertSpecialChar(printPat);
                        }
                        break;

                    case 't':   // to=<pat>
                        if (parser.validOption("till", cmdName, false))  {
                            tillPat = parser.getRegEx(value, isVerbose);
                            findMode = FROM_TILL;
                            doLineByLine = true;
                        } else if (parser.validOption("to", cmdName, false))  {
                            toPat = value;
                            ParseUtil::convertSpecialChar(toPat);
                            doReplace = true;
#ifdef CAN_THREAD
                        } else if (parser.validOption("threads", cmdName))  {
                            runWithThreads = true;
                            Threader::maxThreads = (ThreadCnt)atoi(value);
#endif
                        }
                        break;

                    case 'u':   // until=<pat>
                        if (parser.validOption("until", cmdName)) {
                            untilPat = parser.getRegEx(value, isVerbose);
                            findMode = FROM_UNTIL;
                            doLineByLine = true;
                        }
                        break;

                    default:
                        parser.showUnknown(argStr);
                        break;
                    }
                } else {
                    if (endCmds == argv[argn]) {
                        doParseCmds = false;
                    } else {
                        const char* cmdName = argStr + 1;
                        if (argStr.length() > 2 && *cmdName == '-')
                            cmdName++;  // allow -- prefix on commands
                        switch (*cmdName) {
                        case 'b':
                            binaryOkay = parser.validOption("binary", cmdName);
                            break;
                        case 'f':
                            canForce = parser.validOption("force", cmdName);
                            break;
                        case 'i':
                            if (parser.validOption("ignoreCase", cmdName, false)) {
                                parser.ignoreCase = true;
                            } else 
                                inverseMatch = parser.validOption("inverse", cmdName);
                            break;
                        case 'l':
                            doLineByLine = parser.validOption("lineByLine", cmdName);
                            break;
                        case 'n':
                            doLineByLine = dryRun = parser.validOption("no", cmdName);
                            break;
                        case 'q':   
                            quiet = parser.validOption("quiet", cmdName);
                            break;

                        case 'r':   // -regex
                            parser.unixRegEx = parser.validOption("regex", cmdName);
                            break;
#ifdef CAN_THREAD
                        case 't':
                            runWithThreads = parser.validOption("threads", cmdName);
                            break;
#endif
                        case 'v':
                            isVerbose = parser.validOption("verbose", cmdName);
                            break;
                        case '-':
                            break;
                        case '?':
                            showHelp(argv[0]);
                            return 0;
                        default:
                            parser.showUnknown(argStr);
                            break;
                        }
                    }
                }
            } else {
                // Store file directories
                fileDirList.push_back(argv[argn]);
            }
        }

        if (parser.parseArgSet.count("from") == 0) {
            Colors::showError("Missing -from='pattern'");
            return 0;
        }

        if (parser.patternErrCnt == 0 && parser.optionErrCnt == 0 && fileDirList.size() != 0) {
            SwapStream swapStream(cout);
            if (quiet) {
                // Open the actual OS null device, not a file literally named "null" in the
                // current directory (which is what this created and then left behind).
#ifdef HAVE_WIN
                ofstream null("NUL");
#else
                ofstream null("/dev/null");
#endif
                swapStream.swap(null);
                fclose(stdout);
            } else if (outFile.length() > 0 && !doReplace) {
                out.open(outFile);
                if (out.good()) {
                    swapStream.swap(out);
                    freopen(outFile, "w", stdout);
                }
            }

            // Get starting timepoint
            auto start = std::chrono::high_resolution_clock::now();
            ReplaceFilesInit();
            
            char cwdTmp[256];
            if (getcwd(cwdTmp, sizeof(cwdTmp)) == nullptr) {
                // getcwd() fails if cwd's path is >= sizeof(cwdTmp) (or on other errno
                // conditions) - assigning the resulting NULL char* into an lstring would be UB.
                Colors::showError(strerror(errno), ", Unable to get current directory");
                cwdTmp[0] = '\0';
            }
            cwd = cwdTmp;
            cwd += Directory_files::SLASH;

            if (! toPat.empty() && pFilter != &nopFilter) {
                Colors::showError("\a\nRange filter does not work for replacement only searching\a");
            }
            if (pFilter != &nopFilter) {
                pFilter = doLineByLine ? ((Filter*)&lineFilter) : ((Filter*)&bufferFilter);
            }

            size_t fileMatchCnt = 0;
            if (fileDirList.size() == 1 && fileDirList[0] == "@") {
                string filePath;
                while (std::getline(std::cin, filePath)) {  // Read list of files
                    fileMatchCnt += ReplaceFiles(filePath);
                }
            } else if (fileDirList.size() == 1 && fileDirList[0] == "-") {
                // std::ifstream in = ifstream("data.txt");
                std::cerr << "Reading from stdin\n";
                fileMatchCnt = FindLineGrep("stdin", std::cin, std::cout, "");
            } else {
                for (auto const& filePath : fileDirList) {
                    fileMatchCnt += ReplaceFiles(filePath);
                }
            }

            fileMatchCnt += ReplaceFilesDone();
            
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
            std::cerr << "Files Checked=  " << g_fileCnt << endl;
            std::cerr << "Binary Skipped= " << g_binaryCnt << endl;
            if (g_utf16Cnt != 0)
                std::cerr << "UTF16 Processed=" << g_utf16Cnt << endl;
            std::cerr << "Files Matched= " << fileMatchCnt << endl;
            if (toPat.empty() || doLineByLine) {
                std::cerr << (doLineByLine ? "Lines " : "Patterns ") << "Matched= " << g_regSearchCnt << endl;
            }

        } else {
            showHelp(argv[0]);
        }

        if (out.tellp() > 0)
            out.close();

        std::cerr << std::endl;
    }

    return 0;
}
