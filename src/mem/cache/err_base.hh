/*
 * Copyright (c) 2012-2013, 2015 ARM Limited
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Erik Hallnor
 *          Steve Reinhardt
 *          Ron Dreslinski
 */

/**
 * @file
 * Declares a basic cache interface BaseCache.
 */

#ifndef __BASE_CACHE_HH__
#define __BASE_CACHE_HH__

#include <algorithm>
#include <list>
#include <string>
#include <vector>

#include "base/misc.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "debug/Cache.hh"
#include "debug/CachePort.hh"
#include "debug/CacheBank.hh"
#include "mem/cache/mshr_queue.hh"
#include "mem/mem_object.hh"
#include "mem/packet.hh"
#include "mem/qport.hh"
#include "mem/request.hh"
#include "params/BaseCache.hh"
#include "sim/eventq.hh"
#include "sim/full_system.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"

class MSHR;
/**
 * A basic cache interface. Implements some common functions for speed.
 */
class BaseCache : public MemObject
{
    /**
     * Indexes to enumerate the MSHR queues.
     */
    enum MSHRQueueIndex {
        MSHRQueue_MSHRs,
        MSHRQueue_WriteBuffer
    };

  public:
    /**
     * Reasons for caches to be blocked.
     */
    enum BlockedCause {
        Blocked_NoMSHRs = MSHRQueue_MSHRs,
        Blocked_NoWBBuffers = MSHRQueue_WriteBuffer,
        Blocked_NoTargets,
        NUM_BLOCKED_CAUSES
    };

    /**
     * Reasons for cache to request a bus.
     */
    enum RequestCause {
        Request_MSHR = MSHRQueue_MSHRs,
        Request_WB = MSHRQueue_WriteBuffer,
        Request_PF,
        NUM_REQUEST_CAUSES
    };

  protected:

    /**
     * A cache master port is used for the memory-side port of the
     * cache, and in addition to the basic timing port that only sends
     * response packets through a transmit list, it also offers the
     * ability to schedule and send request packets (requests &
     * writebacks). The send event is scheduled through requestBus,
     * and the sendDeferredPacket of the timing port is modified to
     * consider both the transmit list and the requests from the MSHR.
     */
    class CacheMasterPort : public QueuedMasterPort
    {

      public:

        /**
         * Schedule a send of a request packet (from the MSHR). Note
         * that we could already have a retry outstanding.
         */
        void requestBus(RequestCause cause, Tick time)
        {
            DPRINTF(CachePort, "Scheduling request at %llu due to %d\n",
                    time, cause);
            reqQueue.schedSendEvent(time);
        }

      protected:

        CacheMasterPort(const std::string &_name, BaseCache *_cache,
                        ReqPacketQueue &_reqQueue,
                        SnoopRespPacketQueue &_snoopRespQueue) :
            QueuedMasterPort(_name, _cache, _reqQueue, _snoopRespQueue)
        { }

        /**
         * Memory-side port always snoops.
         *
         * @return always true
         */
        virtual bool isSnooping() const { return true; }
    };

    /**
     * A cache slave port is used for the CPU-side port of the cache,
     * and it is basically a simple timing port that uses a transmit
     * list for responses to the CPU (or connected master). In
     * addition, it has the functionality to block the port for
     * incoming requests. If blocked, the port will issue a retry once
     * unblocked.
     */
    class CacheSlavePort : public QueuedSlavePort
    {

      public:

        /** Do not accept any new requests. */
        void setBlocked();

        /** Return to normal operation and accept new requests. */
        void clearBlocked();

        bool isBlocked() const { return blocked; }

      protected:

        CacheSlavePort(const std::string &_name, BaseCache *_cache,
                       const std::string &_label);

        /** A normal packet queue used to store responses. */
        RespPacketQueue queue;

        bool blocked;

        bool mustSendRetry;

//      private:

        void processSendRetry();

        EventWrapper<CacheSlavePort,
                     &CacheSlavePort::processSendRetry> sendRetryEvent;

    };

    /**
     * Cache data array bank.
     * Only models bank access contention, does not hold actual data
     */
    class CacheBank
    {

       private:
        /** Descriptive name (for DPRINTF output) */
        std::string bankName;
 
        bool inService;

        Tick nextIdleTick;

      public:

        /** Mark this cache bank in-service until finishTick */
        void markInService(Tick finishTick);

        /** Check and unmark this cache bank in-service if necessary */
        void checkAndUnmarkInService();

        /** Extend this cache bank's in-service time by extraTick */
        void extendService(Tick extraTick);

        CacheBank(const std::string &_name) :
            bankName(_name),
            inService(false),
            nextIdleTick(0)
        {}

        bool isBusy() const { return inService; }

        Tick finishTick() const { return nextIdleTick; }

        /** Return bank name (for DPRINTF). */
        const std::string name() const { return bankName; }
 
     };

    CacheSlavePort *cpuSidePort;
    CacheMasterPort *memSidePort;

  protected:

    /** Data array banks */
    std::vector<CacheBank *> bank;

    /** Miss status registers */
    MSHRQueue mshrQueue;

    /** Write/writeback buffer */
    MSHRQueue writeBuffer;

    /**
     * Allocate a buffer, passing the time indicating when schedule an
     * event to the queued port to go and ask the MSHR and write queue
     * if they have packets to send.
     *
     * allocateBufferInternal() function is called in:
     * - MSHR allocateWriteBuffer (unchached write forwarded to WriteBuffer);
     * - MSHR allocateMissBuffer (miss in MSHR queue);
     */
    MSHR *allocateBufferInternal(MSHRQueue *mq, Addr addr, int size,
                                 PacketPtr pkt, Tick time, bool requestBus)
    {
        // check that the address is block aligned since we rely on
        // this in a number of places when checking for matches and
        // overlap
        assert(addr == blockAlign(addr));

        MSHR *mshr = mq->allocate(addr, size, pkt, time, order++);

        if (mq->isFull()) {
            setBlocked((BlockedCause)mq->index);
        }

        if (requestBus) {
            requestMemSideBus((RequestCause)mq->index, time);
        }

        return mshr;
    }

    void markInServiceInternal(MSHR *mshr, bool pending_dirty_resp)
    {
        MSHRQueue *mq = mshr->queue;
        bool wasFull = mq->isFull();
        mq->markInService(mshr, pending_dirty_resp);
        if (wasFull && !mq->isFull()) {
            clearBlocked((BlockedCause)mq->index);
        }
    }

    /**
     * Write back dirty blocks in the cache using functional accesses.
     */
    virtual void memWriteback() = 0;
    /**
     * Invalidates all blocks in the cache.
     *
     * @warn Dirty cache lines will not be written back to
     * memory. Make sure to call functionalWriteback() first if you
     * want the to write them to memory.
     */
    virtual void memInvalidate() = 0;
    /**
     * Determine if there are any dirty blocks in the cache.
     *
     * \return true if at least one block is dirty, false otherwise.
     */
    virtual bool isDirty() const = 0;

    /**
     * Determine if an address is in the ranges covered by this
     * cache. This is useful to filter snoops.
     *
     * @param addr Address to check against
     *
     * @return If the address in question is in range
     */
    bool inRange(Addr addr) const;

    /** Block size of this cache */
    const unsigned blkSize;

    /**
     * The latency of tag lookup of a cache. It occurs when there is
     * an access to the cache.
     */
    const Cycles lookupLatency;

    /**
     * This is the forward latency of the cache. It occurs when there
     * is a cache miss and a request is forwarded downstream, in
     * particular an outbound miss.
     */
    const Cycles forwardLatency;

    /** The latency to fill a cache block */
    const Cycles fillLatency;

    // New added
     /**
     * The latency of a read in this device.
      */
    const Cycles readLatency;
    /**
     * The latency of a write in this device.
     */
    const Cycles writeLatency;

    /**
     * The latency of sending reponse to its upper level cache/core on
     * a linefill. The responseLatency parameter captures this
     * latency.
     */
    const Cycles responseLatency;

    //New added
    /**
     * The knob to turn on/off cache data array bank model
     */
    const bool enableBankModel;

    /**
     * The number of cache data array banks.
     */
    const unsigned numBanks;

    /**
     * The number of cache data array bank interleave bits
     */
    const unsigned bankIntlvBits;

    /**
     * Cache data array bank interleave high bit
     */
    const unsigned bankIntlvHighBit;

    /**
     * Cache data array bank interleave low bit
     */
    const unsigned bankIntlvLowBit;

    /**
     * Cache data array bank interleve mask
     */
    const Addr bankIntlvMask;
    //end of added

    /** The number of targets for each MSHR. */
    const int numTarget;

    /** Do we forward snoops from mem side port through to cpu side port? */
    const bool forwardSnoops;

    /** Is this cache a toplevel cache (e.g. L1, I/O cache). If so we should
     * never try to forward ownership and similar optimizations to the cpu
     * side */
    const bool isTopLevel;

    /**
     * Bit vector of the blocking reasons for the access path.
     * @sa #BlockedCause
     */
    uint8_t blocked;

    /** Increasing order number assigned to each incoming request. */
    uint64_t order;

    /** Stores time the cache blocked for statistics. */
    Cycles blockedCycle;

    /** Pointer to the MSHR that has no targets. */
    MSHR *noTargetMSHR;

    /** The number of misses to trigger an exit event. */
    Counter missCount;

    /**
     * The address range to which the cache responds on the CPU side.
     * Normally this is all possible memory addresses. */
    const AddrRangeList addrRanges;

  public:
    /** System we are currently operating in. */
    System *system;

    // Statistics
    /**
     * @addtogroup CacheStatistics
     * @{
     */

    /** Number of hits per thread for each type of command. @sa Packet::Command */
    Stats::Vector hits[MemCmd::NUM_MEM_CMDS];
    /** Number of hits for demand accesses. */
    Stats::Formula demandHits;
    /**
     * The latency of sending reponse to its upper level cache/core on
     * a linefill. The responseLatency parameter captures this
     * latency.
     */
    const Cycles responseLatency;

    //New added
    /**
     * The knob to turn on/off cache data array bank model
     */
    const bool enableBankModel;

    /**
     * The number of cache data array banks.
     */
    const unsigned numBanks;

    /**
     * The number of cache data array bank interleave bits
     */
    const unsigned bankIntlvBits;

    /**
     * Cache data array bank interleave high bit
     */
    const unsigned bankIntlvHighBit;

    /**
     * Cache data array bank interleave low bit
     */
    const unsigned bankIntlvLowBit;

    /**
     * Cache data array bank interleve mask
     */
    const Addr bankIntlvMask;
    //end of added

    /** The number of targets for each MSHR. */
    const int numTarget;

    /** Do we forward snoops from mem side port through to cpu side port? */
    const bool forwardSnoops;

    /** Is this cache a toplevel cache (e.g. L1, I/O cache). If so we should
     * never try to forward ownership and similar optimizations to the cpu
     * side */
    const bool isTopLevel;

    /**
     * Bit vector of the blocking reasons for the access path.
     * @sa #BlockedCause
     */
    uint8_t blocked;

    /** Increasing order number assigned to each incoming request. */
    uint64_t order;

    /** Stores time the cache blocked for statistics. */
    Cycles blockedCycle;

    /** Pointer to the MSHR that has no targets. */
    MSHR *noTargetMSHR;

    /** The number of misses to trigger an exit event. */
    Counter missCount;

    /**
     * The address range to which the cache responds on the CPU side.
     * Normally this is all possible memory addresses. */
    const AddrRangeList addrRanges;

  public:
    /** System we are currently operating in. */
    System *system;

    // Statistics
    /**
     * @addtogroup CacheStatistics
     * @{
     */

    /** Number of hits per thread for each type of command. @sa Packet::Command */
    Stats::Vector hits[MemCmd::NUM_MEM_CMDS];
    /** Number of hits for demand accesses. */
    Stats::Formula demandHits;
    /**
     * The latency of sending reponse to its upper level cache/core on
     * a linefill. The responseLatency parameter captures this
     * latency.
     */
    Stats::Formula mshrAccesses[MemCmd::NUM_MEM_CMDS];
    /** The total number of demand MSHR accesses. */
    Stats::Formula demandMshrAccesses;
    /** The total number of MSHR accesses. */
    Stats::Formula overallMshrAccesses;
#endif

    /** The miss rate in the MSHRs pre command and thread. */
    Stats::Formula mshrMissRate[MemCmd::NUM_MEM_CMDS];
    /** The demand miss rate in the MSHRs. */
    Stats::Formula demandMshrMissRate;
    /** The overall miss rate in the MSHRs. */
    Stats::Formula overallMshrMissRate;

    /** The average latency of an MSHR miss, per command and thread. */
    Stats::Formula avgMshrMissLatency[MemCmd::NUM_MEM_CMDS];
    /** The average latency of a demand MSHR miss. */
    Stats::Formula demandAvgMshrMissLatency;
    /** The average overall latency of an MSHR miss. */
    Stats::Formula overallAvgMshrMissLatency;

    /** The average latency of an MSHR miss, per command and thread. */
    Stats::Formula avgMshrUncacheableLatency[MemCmd::NUM_MEM_CMDS];
    /** The average overall latency of an MSHR miss. */
    Stats::Formula overallAvgMshrUncacheableLatency;

    /** The number of times a thread hit its MSHR cap. */
    Stats::Vector mshr_cap_events;
    /** The number of times software prefetches caused the MSHR to block. */
    Stats::Vector soft_prefetch_mshr_full;

    Stats::Scalar mshr_no_allocate_misses;

    /**
     * @}
     */

    /**
     * Register stats for this object.
     */
    virtual void regStats();

  public:
    typedef BaseCacheParams Params;
    BaseCache(const Params *p);

    /** Non-default destructor is needed to deallocate memory. */
    virtual ~BaseCache();

    virtual void init();

    virtual BaseMasterPort &getMasterPort(const std::string &if_name,
                                          PortID idx = InvalidPortID);
    virtual BaseSlavePort &getSlavePort(const std::string &if_name,
                                        PortID idx = InvalidPortID);

    /**
     * Query block size of a cache.
     * @return  The block size
     */
    unsigned
    getBlockSize() const
    {
        return blkSize;
    }

    /**
     * Return bank ID according to interleave bits
     */
    unsigned
    getBankId(Addr addr) const
    {
        return (addr & bankIntlvMask) >> bankIntlvLowBit;
    }

    Addr blockAlign(Addr addr) const { return (addr & ~(Addr(blkSize - 1))); }


    const AddrRangeList &getAddrRanges() const { return addrRanges; }

    MSHR *allocateMissBuffer(PacketPtr pkt, Tick time, bool requestBus)
    {
        return allocateBufferInternal(&mshrQueue,
                                      blockAlign(pkt->getAddr()), blkSize,
                                      pkt, time, requestBus);
    }

    MSHR *allocateWriteBuffer(PacketPtr pkt, Tick time, bool requestBus)
    {
        assert(pkt->isWrite() && !pkt->isRead());
        return allocateBufferInternal(&writeBuffer,
                                      blockAlign(pkt->getAddr()), blkSize,
                                      pkt, time, requestBus);
    }

    /**
     * Returns true if the cache is blocked for accesses.
     */
    bool isBlocked() const
    {
        return blocked != 0;
    }

    /**
     * Marks the access path of the cache as blocked for the given cause. This
     * also sets the blocked flag in the slave interface.
     * @param cause The reason for the cache blocking.
     */
    void setBlocked(BlockedCause cause)
    {
        uint8_t flag = 1 << cause;
        if (blocked == 0) {
            blocked_causes[cause]++;
            blockedCycle = curCycle();
            cpuSidePort->setBlocked();
        }
        blocked |= flag;
        DPRINTF(Cache,"Blocking for cause %d, mask=%d\n", cause, blocked);
    }

    /**
     * Marks the cache as unblocked for the given cause. This also clears the

    /** The number of times a thread hit its MSHR cap. */
    Stats::Vector mshr_cap_events;
    /** The number of times software prefetches caused the MSHR to block. */
    Stats::Vector soft_prefetch_mshr_full;

    Stats::Scalar mshr_no_allocate_misses;

    /**
     * @}
     */

    /**
     * Register stats for this object.
     */
    virtual void regStats();

  public:
    typedef BaseCacheParams Params;
    BaseCache(const Params *p);

    /** Non-default destructor is needed to deallocate memory. */
    virtual ~BaseCache();

    virtual void init();

    virtual BaseMasterPort &getMasterPort(const std::string &if_name,
                                          PortID idx = InvalidPortID);
    virtual BaseSlavePort &getSlavePort(const std::string &if_name,
                                        PortID idx = InvalidPortID);

    /**
     * Query block size of a cache.
     * @return  The block size
     */
    unsigned
    getBlockSize() const
    {
        return blkSize;
    }

    /**
     * Return bank ID according to interleave bits
     */
    unsigned
    getBankId(Addr addr) const
    {
        return (addr & bankIntlvMask) >> bankIntlvLowBit;
    }

    Addr blockAlign(Addr addr) const { return (addr & ~(Addr(blkSize - 1))); }


    const AddrRangeList &getAddrRanges() const { return addrRanges; }

    MSHR *allocateMissBuffer(PacketPtr pkt, Tick time, bool requestBus)
    {
        return allocateBufferInternal(&mshrQueue,
                                      blockAlign(pkt->getAddr()), blkSize,
                                      pkt, time, requestBus);
    }

    MSHR *allocateWriteBuffer(PacketPtr pkt, Tick time, bool requestBus)
    {
        assert(pkt->isWrite() && !pkt->isRead());
        return allocateBufferInternal(&writeBuffer,
