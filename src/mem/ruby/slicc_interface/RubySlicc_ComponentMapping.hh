/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
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
 */

#ifndef __MEM_RUBY_SLICC_INTERFACE_RUBYSLICC_COMPONENTMAPPINGS_HH__
#define __MEM_RUBY_SLICC_INTERFACE_RUBYSLICC_COMPONENTMAPPINGS_HH__

#include "mem/protocol/MachineType.hh"
#include "mem/ruby/common/Address.hh"
#include "mem/ruby/common/MachineID.hh"
#include "mem/ruby/common/NetDest.hh"
#include "mem/ruby/structures/DirectoryMemory.hh"

// used to determine the home directory
// returns a value between 0 and total_directories_within_the_system
inline NodeID
map_Address_to_DirectoryNode(const Address& addr)
{
    return DirectoryMemory::mapAddressToDirectoryVersion(addr);
}

// used to determine the home directory
// returns a value between 0 and total_directories_within_the_system
inline MachineID
map_Address_to_Directory(const Address &addr)
{
    MachineID mach =
        {MachineType_Directory, map_Address_to_DirectoryNode(addr)};
    return mach;
}

inline NetDest
broadcast(MachineType type)
{
    NetDest dest;
    for (NodeID i = 0; i < MachineType_base_count(type); i++) {
        MachineID mach = {type, i};
        dest.add(mach);
    }
    return dest;
}

inline MachineID
mapAddressToRange(const Address & addr, MachineType type, int low_bit,
                  int num_bits, int cluster_id = 0)
{
    MachineID mach = {type, 0};
    if (num_bits == 0)
        mach.num = cluster_id;
    else
        mach.num = addr.bitSelect(low_bit, low_bit + num_bits - 1)
            + (1 << num_bits) * cluster_id;
    //printf("Addr:%llu, low_bit:%d, num_bits:%d, cluster_id:%d, mach.num:%d.\n", addr.getAddress(),low_bit,num_bits,cluster_id,mach.num);
    return mach;
}

// A multicast search function.
inline NetDest
multicast(MachineID machID, MachineType typeL1, MachineType typeL2)
{
    NetDest dest;
    for(NodeID i = 0; i < MachineType_base_count(typeL2); i++) {
        MachineID mach = {typeL2, i};
        if (machID.num == mach.num % MachineType_base_count(typeL1)) {
            dest.add(mach);
			printf("num of L2:%d, num of L1:%d, machID.num:%d, mach.num:%d.\n", MachineType_base_count(typeL2), MachineType_base_count(typeL1), machID.num, mach.num);
		}
    }
    return dest;
}

// Count the number of banks in a bankset.
inline int
countSetbanks(MachineType typeL1, MachineType typeL2)
{
    return MachineType_base_count(typeL2) / MachineType_base_count(typeL1);
}

// Map to nearest L2 Cache. Input L1 machID and L2 type.
inline MachineID
maptonearestbank(MachineID machID, MachineType type)
{
    MachineID mach;
    mach = {type, machID.num};
    return mach;
}

inline NodeID
machineIDToNodeID(MachineID machID)
{
    return machID.num;
}

inline MachineType
machineIDToMachineType(MachineID machID)
{
    return machID.type;
}

inline int
machineCount(MachineType machType)
{
    return MachineType_base_count(machType);
}

inline MachineID
createMachineID(MachineType type, NodeID id)
{
    MachineID mach = {type, id};
    return mach;
}

#endif  // __MEM_RUBY_SLICC_INTERFACE_COMPONENTMAPPINGS_HH__
