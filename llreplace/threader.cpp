//-------------------------------------------------------------------------------------------------
//
// File: threader.cpp   Author: Dennis Lang  Desc: run job in thread pool with memory pool.
//
//-------------------------------------------------------------------------------------------------
//
// Author: Dennis Lang - 2026
// https://landenlabs.com
//
//  
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

#include "threader.hpp"

#include <assert.h>
#include <thread>
#include <shared_mutex>
#include <semaphore>
#include <atomic>
#include <list>
#include <vector>
#include <iostream>


std::atomic<ThreadCnt> threadJobCnt(0);    // active thread jobs
static std::counting_semaphore threadLimiter{MAX_THREADS};

typedef unsigned int Tid;

// Wrapper to run job in thread pool, see Threader.
class ThreadJob  {
public:
    Tid id;
    Job* jobPtr;
    std::atomic<bool> isDone;  // written by the worker thread, read by the main thread
    std::thread thread1;

    ThreadJob(Tid _id, Job* _jobPtr): id(_id), jobPtr(_jobPtr), isDone(false),
        thread1(&ThreadJob::doWork, this) // starts thread
    {   threadJobCnt++;  }
    
    ~ThreadJob() {
        delete jobPtr;
        jobPtr = nullptr;
        isDone = true;
        threadJobCnt--;
    }
    
    void join() {
        if (thread1.joinable())
            thread1.join();
    }
    
    void doWork(){
        try {
            jobPtr->run();
        } catch (...) {
        }
        isDone = true;
        threadLimiter.release();
    }
};

ThreadCnt Threader::maxThreads = MAX_THREADS;
typedef std::list<ThreadJob*> ThreadJobs;
static ThreadJobs threadJobs;               // List if active threads
static Tid ids = 0;                         // debug

// Forward declaration
void clearDoneJobs(Job* jobPtr);

void Threader::init() {
    for (int idx = maxThreads; idx < MAX_THREADS; idx++)
        threadLimiter.acquire();
}

void Threader::runIt(Job* jobPtr) {
    clearDoneJobs(jobPtr);
    threadLimiter.acquire();        // blocks if max threads active
    threadJobs.push_back(new ThreadJob(ids++, jobPtr));
}

void Threader::waitForAll(Job* jobPtr) {
    // Wait for every in-flight thread to finish - the caller relies on this to know all
    // file writes have completed before the program exits. This used to give up after
    // ~100s (10 iterations x up to 10s each) even if threads were still running, silently
    // returning to the caller (and letting the process exit) while a worker could still
    // be mid-write.
    while (threadJobCnt > 0) {
        clearDoneJobs(jobPtr);
        if (threadJobCnt != 0)
            (void)threadLimiter.try_acquire_for(std::chrono::seconds(10));    // threadLimiter.acquire();
#if 0
        std::cerr << "Waiting for threads " << threadJobCnt << " to complete\n";
        ThreadJobs::iterator jobIter = threadJobs.begin();
        while (jobIter != threadJobs.end()) {
            ThreadJob* jobPtr = *jobIter++;
            std::cerr << (jobPtr->isDone ? "Done " : "Running ");
            jobPtr->jobPtr->dump();
        }
        // Buffer::dump();
#endif
    }
}

void clearDoneJobs(Job* /* jobPtr - currently unused; kept for a uniform call signature */) {
    ThreadJobs::iterator jobIter = threadJobs.begin();
    while (jobIter != threadJobs.end()) {
        ThreadJob* curJobPtr = *jobIter;
        if (curJobPtr->isDone) {
            curJobPtr->join();
            delete curJobPtr;
            jobIter = threadJobs.erase(jobIter);
        } else {
            ++jobIter;
        }
    }
}

// -----------------------------------------------------------------------------------
// Memory buffer pool manager
// -----------------------------------------------------------------------------------
#ifdef CAN_THREAD
typedef unsigned int uint;
typedef std::vector<char> Block;
typedef std::vector<Block> Blocks;
static Blocks blocks(MAX_THREADS);
static std::vector<bool> blocksUsed(MAX_THREADS, false);
static std::shared_mutex lockGetBlock;
static std::counting_semaphore<MAX_THREADS> lockFreeBlock{0};

static size_t bufferTotalSize = 0;
const uint NO_IDX = -1;
static uint maxBufIdx = NO_IDX;
const size_t MAX_TOTAL_SIZE = 1024 * 1024 * 200 * MAX_THREADS;


// Must be called with lockGetBlock already held. Returns NO_IDX (rather than blocking)
// if every block is currently in use - the caller (findBuffer) waits for one to free up
// and retries, WITHOUT holding lockGetBlock while it waits. Blocking here while holding
// the lock would deadlock against releaseBuffer, which also needs lockGetBlock to safely
// update the same state this function reads/writes (blocksUsed/blocks/bufferTotalSize/
// maxBufIdx were previously mutated by releaseBuffer with no locking at all - a data race
// with this function, which mutates the same state under the lock).
static uint getBuffer(size_t capacity) {

    uint bestIdx = NO_IDX;
    
    // Requested capacity best fit inside block
    uint bestIdx1 = NO_IDX;
    size_t delta1 = -1;
    
    // Requested capacity closest but larger than block.
    uint bestIdx2 = NO_IDX;
    size_t delta2 = -1;
    
    for (uint idx = 0; idx < blocksUsed.size(); idx++) {
        if (!blocksUsed[idx]) {
            size_t blockCapacity = blocks[idx].capacity();
            if (capacity < blockCapacity) {
                size_t newDelta = blockCapacity - capacity;
                if (newDelta < delta1) {
                    bestIdx1 = idx;
                    delta1 = newDelta;
                }
            } else {
                size_t newDelta = capacity - blockCapacity;
                if (newDelta < delta2) {
                    bestIdx2 = idx;
                    delta2 = newDelta;
                }
            }
        }
    }
    if (bestIdx1 != NO_IDX) {
        bestIdx = bestIdx1;     // Requested capacity best fit inside block
    } else if (bestIdx2 != NO_IDX) {
        bestIdx = bestIdx2;     // Requested capacity closest but larger than block.
        bufferTotalSize += delta2;
        blocks[bestIdx].reserve(capacity);
        if (maxBufIdx == NO_IDX || capacity > blocks[maxBufIdx].capacity()) {
            maxBufIdx = bestIdx;
        }
    } else {
        return NO_IDX;
    }

    blocksUsed[bestIdx] = true;
    return bestIdx;
}

static uint findBuffer(size_t capacity) {
    for (;;) {
        {
            std::scoped_lock lock(lockGetBlock);
            uint bestIdx = getBuffer(capacity);
            if (bestIdx != NO_IDX) {
                return bestIdx;
            }
        }
        // No free block right now - wait for a release OUTSIDE the lock, then retry.
        lockFreeBlock.acquire();
    }
}

static void releaseBuffer(unsigned int bufIdx) {
    {
        std::scoped_lock lock(lockGetBlock);
        if (bufIdx == maxBufIdx && bufferTotalSize > MAX_TOTAL_SIZE) {
            bufferTotalSize -= blocks[bufIdx].capacity();
            blocks[bufIdx].clear();
            maxBufIdx = NO_IDX;
        }
        blocksUsed[bufIdx] = false;
    }
    lockFreeBlock.release();
}

Buffer::Buffer(size_t capacity) : bufSize(capacity) {
    bufIdx = findBuffer(capacity);
    bufPtr = blocks[bufIdx].data();
}
Buffer::~Buffer() {
    releaseBuffer(bufIdx);
}
void Buffer::dump() {
    std::cerr << "Buffers\n";
    for (uint idx = 0; idx < blocksUsed.size(); idx++) {
        std::cerr << "  " << idx << (blocksUsed[idx] ? ": Used ":": Free ") << " size=" << blocks[idx].capacity() << std::endl;
    }
}
#else

static std::vector<char> Buffer1;
Buffer::Buffer(size_t capacity) : bufSize(capacity), bufIdx(0) {
    if (capacity > Buffer1.capacity())
        Buffer1.reserve(capacity);
    bufPtr = Buffer1.data();
}
Buffer::~Buffer() { }
Buffer::dump() { }
#endif
