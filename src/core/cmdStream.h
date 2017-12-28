/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2014-2017 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 **********************************************************************************************************************/

#pragma once

#include "core/cmdStreamAllocation.h"
#include "core/platform.h"
#include "palHashMap.h"
#include "palIntrusiveList.h"
#include "palVector.h"
#include "g_palSettings.h"

namespace Util { class File; }
namespace Util { class VirtualLinearAllocator; }

namespace Pal
{

// Forward declarations:
class CmdAllocator;
class Device;
class GpuMemoryPatchList;
class ICmdAllocator;
class IQueue;
class Platform;
enum  QueueType : uint32;

// Many queues & command buffers actually break down into multiple command streams and subqueues internally. For
// example, Universal queues/cmdbufs actually submit a universal command stream and a constant engine command
// stream.
enum class SubQueueType : uint32
{
    // Subqueue that is the queue itself, rather than an ancilliary queue.
    Primary = 0,

    // CP constant update engine that runs in parallel with draw engine.
    ConstantEngine = 1,

    // Preamble command buffer CP constant update engine that runs in parallel with the draw engine, but prior
    // to any of the other ConstantEngine command streams. This is meant for use only in QueueContext classes.
    ConstantEnginePreamble = 2,

    SubQueueTypeCount,
};

// Structure for passing arguments to FilterSetUserDataGfx() and WriteUserDataRegisters().  Reduces moving
// arguments on and off the stack when calling these functions.
struct UserDataArgs
{
    uint32        firstEntry;
    uint32        entryCount;
    const uint32* pEntryValues;
};

// Flags describing the state of a command stream object. Note that the "optMode" flags are special. Exactly one of them
// must be set when optimizeCommands is also set. If optimizeCommands is not set they must all be not set. They exist
// to accelerate all if-statements that examine the optimization mode.
union CmdStreamFlags
{
    struct
    {
        uint32 isConstantEngine   :  1; // Indicates that this is a Constant Engine command stream.
        uint32 isCePreamble       :  1; // Indicates that this is a Constant Engine Preamble command stream.
        uint32 dropIfSameContext  :  1; // The KMD can drop this stream if the previous scheduler context is the same.
        uint32 prefetchCommands   :  1; // The command stream should be prefetched into the GPU cache.
        uint32 optimizeCommands   :  1; // The command stream contents should be optimized.
        uint32 optModeImmediate   :  1; // Commands will be optimized when they are written to the reserve buffer.
        uint32 optModeFinalized   :  1; // Commands will be optimized when chunks are finalized.
        uint32 buildInSysMem      :  1; // Command data will be allocated using system memory chunks instead of the
                                        // usual GPU memory chunks.
        uint32 enablePreemption   :  1; // This command stream can be preempted.
        uint32 addressDependent   :  1; // One or more commands are dependent on the command chunk's GPU address. This
                                        // disables optimizations that copy commands and execute them without patching.
        uint32 reserved           : 22;
    };
    uint32     value;
};

// Flags passed to CmdSteam::Begin. Putting them in a union saves space and makes it easier to add new flags.
union CmdStreamBeginFlags
{
    struct
    {
        uint32 prefetchCommands :  1; // The command stream should be prefetched into the GPU cache.
        uint32 optimizeCommands :  1; // The command stream contents should be optimized.
        uint32 reserved         : 30;
    };
    uint32     value;
};

// Used for tracking the number of times a nested chunk is 'called' and to record the generation of the chunk
// at build time. This is used for checking whether the command allocator was reset before the chunks were submitted
struct NestedChunkData
{
    uint32 executeCount;        // Number of times the chunk was called.
#if PAL_ENABLE_PRINTS_ASSERTS
    uint32 recordedGeneration;  // The generation of the chunk as recorded during each call.
#endif

};

// =====================================================================================================================
// The CmdStream class manages a single stream of hardware commands on behalf of a command buffer.
//
// In the simplest case, the command stream will consist of a single command buffer chunk, but the CmdStream also
// supports growing a command stream with multiple chained chunks.
//
// The CmdStream does not understand actual hardware commands, it just hands out space.  It implements shared
// functionality between command buffers that use different hardware command formats (e.g., PM4 vs. SDMA).  Derived
// classes may understand hardware commands if necessary.
//
// A single command buffer could have multiple command streams (e.g., draw and compute engine streams for a universal
// command buffer).
//
// In addition to divvying out command space, the command stream can also provide space for temporary data that needs
// to be GPU-accessible during the execution of the command stream.  Command space is issued from the beginning of the
// chunk, embedded data space is issued from the end of the chunk, and a new chunk is needed when they meet.
class CmdStream
{
    // A useful shorthand for a vector list of chunks.
    typedef ChunkVector<CmdStreamChunk*, 16, Platform> ChunkRefList;

    // A useful shorthand for a hash map of nested command buffer chunk execute-counts.
    typedef Util::HashMap<CmdStreamChunk*, NestedChunkData, Platform> NestedChunkMap;

public:
    CmdStream(
        Device*        pDevice,
        ICmdAllocator* pCmdAllocator,
        EngineType     engineType,
        SubQueueType   subQueueType,
        uint32         postambleDwords,
        uint32         minPaddingDwords,
        bool           isNested,
        bool           disablePreemption);
    virtual ~CmdStream();

    virtual Result Init();

    // Begin should be called after Reset but before any commands are written to the CmdStream.
    virtual Result Begin(CmdStreamBeginFlags flags, Util::VirtualLinearAllocator* pMemAllocator);

    // Finalizes the command chunks.
    // if an error is returned, it means there is something wrong with the command stream, the content is undefined and invalid
    // and the command stream can't be executed.
    Result End();

    // Resets the contents of the command stream. If pNewAllocator is non-null it will be used for future allocations.
    //
    // WARNING: If returnGpuMemory is false, the command stream's chunks must be GPU idle and must not be referenced
    //          by any other command streams!
    virtual void Reset(CmdAllocator* pNewAllocator, bool returnGpuMemory);

    // Accessor for the command stream's GPU memory patch location list. Some types of command streams need to maintain
    // a list of command buffer locations where the KMD can patch physical memory addresses into the submitted command
    // buffer at submission-time. Most command streams won't have this, but those that do will override this method.
    virtual const GpuMemoryPatchList* GetPatchList() const { return nullptr; }

    // Public command interface:
    // Classes that wish to write commands to a command stream should call ReserveCommands, write their commands to the
    // provided buffer, and finish by passing a pointer to the next DWORD after their commands to CommitCommands. They
    // can reserve embedded command space by calling ReserveEmbeddedData. However, embedded command space must never be
    // reserved in between a call to ReserveCommands and CommitCommands!
    uint32* ReserveCommands();
    void CommitCommands(const uint32* pEndOfBuffer);

    // Similar to ReserveCommands, but this call guarantees the command buffer is allocated in a new chunk.
    uint32* ReserveCommandsInNewChunk();

    // In some rare cases we must break the "assume you have enough space" rule. Typically this means the caller is
    // building commands in a loop which may or may not write more commands than will fit in the reserve buffer. This
    // function will return the number of command DWORDs that are guaranteed to fit in the reserve buffer.
    uint32 ReserveLimit() const { return m_reserveLimit; }

    // Patches the final chunk of this command stream so that it chains to the first chunk of the given target stream.
    // If a null pointer is provided, a NOP is written to clear out any previous chaining commands. This must be called
    // after this command stream has been finalized via End(). An empty implementation is provided for streams that do
    // not support chaining.
    virtual void PatchTailChain(const CmdStream* pTargetStream) const { PAL_ASSERT_ALWAYS(); };

    // "Calls" a command stream belonging to a nested command buffer. The base implementation is meant for engines or
    // situations where the command stream is unable to jump to the callee command streeam and then jump back. It copies
    // the commands from the callee's command chunk(s) into this command stream.
    // Note: It is expected that the caller will also call TrackNestedCommands().
    virtual void Call(const CmdStream& targetStream, bool exclusiveSubmit, bool allowIb2Launch);

    void IncrementSubmitCount();

    void TrackNestedCommands(const CmdStream& targetStream);
    void TrackNestedEmbeddedData(const ChunkRefList& dataChunkList);

    // Returns the current GPU VA of this command stream
    gpusize GetCurrentGpuVa();

    uint32 GetSizeAlignDwords() const { return m_sizeAlignDwords; }

#if PAL_ENABLE_PRINTS_ASSERTS
    void DumpCommands(Util::File* pFile, const char* pHeader, CmdBufDumpMode mode) const;
#endif

    void EnableDropIfSameContext(bool enable) { m_flags.dropIfSameContext = enable; }

    bool IsConstantEngine() const { return m_flags.isConstantEngine == 1; }
    bool IsConstantEnginePreamble() const { return m_flags.isCePreamble == 1; }
    bool DropIfSameContext() const { return m_flags.dropIfSameContext == 1; }
    bool IsPreemptionEnabled() const { return m_flags.enablePreemption == 1; }

    // Whenever someone writes a command that depends on the command chunk's GPU virtual address they must notify
    // the command stream of this dependency to prevent PAL from assuming it's safe to copy commands without patching.
    void NotifyAddressDependent() { m_flags.addressDependent = 1; }
    bool IsAddressDependent() const { return m_flags.addressDependent; }

    // Returns true if this command stream has recorded no commands.
    bool IsEmpty() const { return (GetNumChunks() == 0) || (GetFirstChunk()->DwordsAllocated() == 0); }

    EngineType GetEngineType() const { return m_engineType; }

    uint32 GetNumChunks() const { return m_chunkList.NumElements(); }

    ChunkRefList::Iter GetFwdIterator() const { return m_chunkList.Begin(); }
    CmdStreamChunk*    GetFirstChunk()  const { return m_chunkList.Front(); }

    // An upper-bound on all allocated command chunk space. Can be called on a finalized command stream.
    gpusize TotalChunkDwords() const { return m_totalChunkDwords; }

    // Returns whether PM4 optimizer is enabled or not
    bool Pm4OptimizerEnabled() const { return m_flags.optimizeCommands; }

    bool Pm4ImmediateOptimizerEnabled() const { return m_flags.optModeImmediate; }

protected:
    // Internal chunk memory interface:
    // The command stream uses the alloc functions to get chunk space to store commands and embedded data. These
    // functions will automatically obtain new chunks as needed. The reclaim function must be called by the command
    // stream if it did not use all of the command space it asked for using AllocCommandSpace, otherwise gaps will
    // appear in the command stream which will cause undefined behavior.
    uint32* AllocCommandSpace(uint32 sizeInDwords);
    void ReclaimCommandSpace(uint32 sizeInDwords);

    // Verifies that the current chunk has enough free space for an allocation of the given size. It will obtain a new
    // chunk if more space is needed. Returns true if it didn't get a new chunk.
    bool ValidateCommandSpace(uint32 sizeInDwords);

    // A list of command chunk pointers that the command stream owns. The chunks will be executed from front to back
    // which means that the chunk at the back is currently being built.
    ChunkRefList   m_chunkList;
    // A list of chunks that are being retained between command stream resets to avoid calling the allocator
    ChunkRefList   m_retainedChunkList;

    CmdStreamFlags m_flags;             // Flags describing the state of this command stream object.

    const uint32   m_sizeAlignDwords;   // Required size alignment of each chunk.
    const uint32   m_startAlignBytes;   // Required start alignment of each chunk.

    CmdAllocator*  m_pCmdAllocator;

    // An optional memory allocator that the command stream can use to make temporary allocations. It must be managed
    // by a parent command buffer and it is expected to be null in some situations (e.g., internal command streams).
    // It can only be valid during command building and its allocations must follow special life-time rules. Read the
    // CmdBufferBuildInfo documentation for more information.
    Util::VirtualLinearAllocator* m_pMemAllocator;

private:
    CmdStreamChunk* GetNextChunk(uint32 numDwords);

    // These adjust the size of our chunk list.
    CmdStreamChunk* GetChunk(uint32 numDwords);

    // Called before m_pMemAllocator is released in End(). Must destroy all temporary objects allocated using it.
    virtual void CleanupTempObjects() { }

    // Subclasses may need to update internal state when we switch between command chunks. BeginCurrentChunk will be
    // called once the tail chunk is ready to accept commands via AllocCommandSpace so that the implementer can add
    // a chunk preamble. EndCurrentChunk will be called once for each chunk so that the implementer can add a chunk
    // postamble which was pre-reserved using m_cmdSpaceDwordPadding.
    virtual void BeginCurrentChunk() { }
    virtual void EndCurrentChunk(bool atEndOfStream) { }

    // Gives subclasses the ability to apply command optimizations at commit time. If true is returned it means that an
    // optimized version of pSrcBuffer has been stored in pDstBuffer. If false is returned it means the implementer
    // declined to do any optimization in which case the caller must do what is necessary to commit the commands.
    // Note that pSrcBuffer and pDstBuffer may point to the same buffer and that pNumDwords contains the ammount of
    // space used in pSrcBuffer. If true is returned then pNumDwords returns the ammount of space used in pDstBuffer.
    virtual bool OptimizedCommit(const uint32* pSrcBuffer, uint32* pDstBuffer, uint32* pNumDwords) { return false; }

    void TrackNestedChunks(const ChunkRefList& chunkList);
    void ResetNestedChunks();

    Device*const     m_pDevice;
    const EngineType m_engineType;
    const uint32     m_cmdSpaceDwordPadding; // End-of-chunk padding needed for a postamble and/or NOP padding.
    const uint32     m_reserveLimit;         // DWORDs that are reserved by each call to ReserveCommands.

    uint32           m_chunkDwordsAvailable; // Unused DWORDs available in the tail of m_chunkList.

    // If the dedicated reserve buffer is in use its address will be stored here at initialization. If it is not in use
    // then this pointer will be set to a bit of allocated chunk space each time ReserveCommands is called.
    uint32*          m_pReserveBuffer;

    // Hash map of all nested command buffer chunks which were executed by this command stream via calls to Call().
    NestedChunkMap   m_nestedChunks;

    Result           m_status;           // To identify whether any error occurs when command stream setup.
    gpusize          m_totalChunkDwords; // The sum of all allocated chunk space, computed at End() time.

#if PAL_ENABLE_PRINTS_ASSERTS
    uint32           m_streamGeneration; // Counter used for tracking stream reset before submit.
    bool             m_isReserved;       // Used to validate that reserve and commit are always called in pairs.
#endif

    PAL_DISALLOW_DEFAULT_CTOR(CmdStream);
    PAL_DISALLOW_COPY_AND_ASSIGN(CmdStream);
};

} // Pal
