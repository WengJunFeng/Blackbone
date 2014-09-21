#include "MemBlock.h"
#include "ProcessMemory.h"
#include "ProcessCore.h"
#include "../Subsystem/NativeSubsystem.h"

namespace blackbone
{

MemBlock::MemBlock()
    : _memory( nullptr )
{
}

/// <summary>
/// MemBlock ctor
/// </summary>
/// <param name="mem">Process memory routines</param>
/// <param name="ptr">Memory address</param>
/// <param name="size">Block size</param>
/// <param name="prot">Memory protection</param>
/// <param name="own">true if caller will be responsible for block deallocation</param>
MemBlock::MemBlock( ProcessMemory* mem, ptr_t ptr, 
                    size_t size, DWORD prot, bool own /*= true*/,
                    bool physical /*= false*/)
    : _ptr( ptr )
    , _memory( mem )
    , _size( size )
    , _protection( prot )
    , _own( own )
    , _physical( physical )
{
}

/// <summary>
/// MemBlock ctor
/// </summary>
/// <param name="mem">Process memory routines</param>
/// <param name="ptr">Memory address</param>
/// <param name="own">true if caller will be responsible for block deallocation</param>
MemBlock::MemBlock( ProcessMemory* mem, ptr_t ptr, bool own /*= true*/ )
    : _ptr( ptr )
    , _memory( mem )
    , _own( own )
{
    MEMORY_BASIC_INFORMATION64 mbi = { 0 };
    mem->Query( _ptr, &mbi );

    _protection = mbi.Protect;
    _size = (size_t)mbi.RegionSize;
}

MemBlock::~MemBlock()
{
    if (_own)
        Free();
}

/// <summary>
/// Allocate new memory block
/// </summary>
/// <param name="process">Process memory routines</param>
/// <param name="size">Block size</param>
/// <param name="desired">Desired base address of new block</param>
/// <param name="protection">Memory protection</param>
/// <returns>Memory block. If failed - returned block will be invalid</returns>
MemBlock MemBlock::Allocate( ProcessMemory& process, size_t size, ptr_t desired /*= 0*/, DWORD protection /*= PAGE_EXECUTE_READWRITE */ )
{
    ptr_t desired64 = desired;
    DWORD newProt = CastProtection( protection, process.core().DEP() );
    
    if (process.core().native()->VirualAllocExT( desired64, size, MEM_COMMIT, newProt ) != STATUS_SUCCESS)
    {
        desired64 = 0;
        if (process.core().native()->VirualAllocExT( desired64, size, MEM_COMMIT, newProt ) == STATUS_SUCCESS)
            LastNtStatus( STATUS_IMAGE_NOT_AT_BASE );
        else
            desired64 = 0;
    }

    return MemBlock( &process, desired64, size, protection );
}

/// <summary>
/// Reallocate existing block for new size
/// </summary>
/// <param name="size">New block size</param>
/// <param name="desired">Desired base address of new block</param>
/// <param name="protection">Memory protection</param>
/// <returns>New block address</returns>
ptr_t MemBlock::Realloc( size_t size, ptr_t desired /*= 0*/, DWORD protection /*= PAGE_EXECUTE_READWRITE*/ )
{
    ptr_t desired64 = desired;
    _memory->core().native()->VirualAllocExT( desired64, size, MEM_COMMIT, protection );

    if (!desired64)
    {
        desired64 = 0;
        _memory->core( ).native( )->VirualAllocExT( desired64, size, MEM_COMMIT, protection );

        if (desired64)
            LastNtStatus( STATUS_IMAGE_NOT_AT_BASE );
    }

    // Replace current instance
    if (desired64)
    {
        Free();

        _ptr = desired64;
        _size = size;
        _protection = protection;
    }

    return desired64;
}

/// <summary>
/// Change memory protection
/// </summary>
/// <param name="protection">New protection flags</param>
/// <param name="offset">Memory offset in block</param>
/// <param name="size">Block size</param>
/// <param name="pOld">Old protection flags</param>
/// <returns>Status</returns>
NTSTATUS MemBlock::Protect( DWORD protection, size_t offset /*= 0*/, size_t size /*= 0*/, DWORD* pOld /*= nullptr */ )
{
    auto prot = CastProtection( protection, _memory->core().DEP() );

    if (size == 0)
        size = _size;

    return _physical ? Driver().ProtectMem( _memory->core().pid(), _ptr + offset, size, prot ) : 
                       _memory->Protect( _ptr + offset, size, prot, pOld );
}

/// <summary>
/// Free memory
/// </summary>
/// <param name="size">Size of memory chunk to free. If 0 - whole block is freed</param>
NTSTATUS MemBlock::Free( size_t size /*= 0*/ )
{
    if (_ptr != 0)
    {
        size = Align( size, 0x1000 );

        NTSTATUS status = _physical ? Driver().FreeMem( _memory->core().pid(), _ptr, size, MEM_RELEASE ) : _memory->Free( _ptr, size, size == 0 ? MEM_RELEASE : MEM_DECOMMIT );

        if (!NT_SUCCESS( status ))
            return LastNtStatus();

        if(size == 0)
        {
            _ptr  = 0;
            _size = 0;
            _protection = 0;
        }
        else
        {
            _ptr  += size;
            _size -= size;
        }
    }

    return STATUS_SUCCESS;
}

/// <summary>
/// Read data
/// </summary>
/// <param name="offset">Data offset in block</param>
/// <param name="size">Size of data to read</param>
/// <param name="pResult">Output buffer</param>
/// <param name="handleHoles">
/// If true, function will try to read all committed pages in range ignoring uncommitted.
/// Otherwise function will fail if there is at least one non-committed page in region.
/// </param>
/// <returns>Status</returns>
NTSTATUS MemBlock::Read( size_t offset, size_t size, PVOID pResult, bool handleHoles /*= false*/ )
{
    return _memory->Read( _ptr + offset, size, pResult, handleHoles );
}


/// <summary>
/// Write data
/// </summary>
/// <param name="offset">Data offset in block</param>
/// <param name="size">Size of data to write</param>
/// <param name="pData">Buffer to write</param>
/// <returns>Status</returns>
NTSTATUS MemBlock::Write( size_t offset, size_t size, const void* pData )
{
    return _memory->Write( _ptr + offset, size, pData );
}

/// <summary>
/// Try to free memory and reset pointers
/// </summary>
void MemBlock::Reset()
{
    Free();

    _ptr = 0;
    _size = 0;
    _protection = 0;
    _own = false;
    _memory = nullptr;
}

}