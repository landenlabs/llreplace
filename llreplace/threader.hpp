//  File: threader.hpp
//  Desc: Run grep/replace in thread
//
//  Created by dennis.lang on 12/3/24.
//  Copyright © 2024 Dennis Lang. All rights reserved.
//

#include "ll_stdhdr.hpp"
#include <atomic>

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
