//-------------------------------------------------------------------------------------------------
// File: threader.hpp
// Author: Dennis Lang
//
// Desc: Run job in thread pool and provide a memory pool
//
//-------------------------------------------------------------------------------------------------
//
// Author: Dennis Lang - 2024
// https://landenlabs.com
//
//  
//
// ----- License ----
//
// Copyright (c) 2024  Dennis Lang
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

#include "ll_stdhdr.hpp"
 
#define CAN_THREAD 1

typedef unsigned int ThreadCnt;
#ifdef CAN_THREAD
const ThreadCnt MAX_THREADS = 10;
#else
const ThreadCnt MAX_THREADS = 1;
#endif
 
class Job {
public:
    virtual ~Job() { }
    virtual void run() = 0;
    virtual void dump() {}
};

class Threader  {
public:
    static ThreadCnt maxThreads;
    static void init();
    static void runIt(Job* jobPtr);
    static void waitForAll(Job* jobPtr = nullptr);
};


class Buffer {
    unsigned int bufIdx;
    char* bufPtr;
    size_t bufSize;
    
public:
    Buffer(size_t capacity);
    ~Buffer();
    
    size_t size() const { return bufSize;  }
    char* data() { return bufPtr;  }
    char& operator[](size_t idx) { return bufPtr[idx]; }
    
    // Debug
    static void dump();
};
