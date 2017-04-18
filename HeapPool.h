#pragma once

#include <stdint.h>

#include <unordered_map>
#include <deque>
#include <vector>
#include <mutex>

#ifndef HEAP_POOL_H_
#define HEAP_POOL_H_

#pragma warning(push)
#pragma warning(disable:4290)
#pragma warning(disable:4267)

#ifdef __GNUC__
#define FORCEINLINE __attribute__((always_inline))
#else
#define FORCEINLINE __forceinline
#endif

#ifdef _WIN32
#   include <Windows.h>
#elif __linux__ 
#       
#endif

void* PlatformDepencyHeapAlloc(const unsigned szHeap);
void  PlatformDepencyHeapFree(void* pHeap, unsigned szHeap);

using namespace std;

typedef enum class __HeapType
{
    HEAP_RESERVED       = 0,
    HEAP_MANAGED        = 1,
    HEAP_COLLECTED      = 2
} HeapType;

// 24 Byte or 32 Byte of Header.
// It means under 32 Bytes of allcating is wasting memory.
typedef struct __HeapInfo
{
    unsigned __int32    m_szHeap;
    unsigned __int32    m_Flag;
    unsigned __int32*   m_HeapKey;

    HeapType            m_Type;
    void*               m_Object;

} HeapInfo;

typedef struct __HeapHeader
{
    HeapInfo*       m_HeapInfo;
} HeapHeader;

FORCEINLINE void* OffsetAddress(void* pTarget, int Offset)
{
    return (void*)((size_t)pTarget + Offset);
}

template<class Type>
class HeapPoolManaged
{
    vector<void*>           m_HeapUsable;
    vector<void*>           m_HeapReserved;

    const unsigned          m_szBlocks          = 512;
    const unsigned          m_cSizeAllocate     = sizeof(Type) * m_szBlocks;

    void Reserve()
    {
        void* pObject = PlatformDepencyHeapAlloc(m_cSizeAllocate);

        for (unsigned i = 0; i < 512; ++i)
        {
            m_HeapUsable.push_back(OffsetAddress(pObject, sizeof(Type) * i));
        }
        m_HeapReserved.push_back(pObject);
    }
public:
    HeapPoolManaged()
    {
        Reserve();
    }
    ~HeapPoolManaged()
    {
        for (void* pObject : m_HeapReserved)
        {
            PlatformDepencyHeapFree(pObject, m_cSizeAllocate);
        }
    }

    void* Alloc()
    {
        if (m_HeapUsable.empty())
        {
            Reserve();
        }

        void* pObject  = 
            m_HeapUsable.back();
            m_HeapUsable.pop_back();

        return pObject;
    }
    void Free(void* pObject)
    {
        m_HeapUsable.push_back(pObject);
    }
};

class HeapPoolNoneManaged
{
    const unsigned szHeapInfo = 8;

    typedef struct __InfoReserved
    {
        unsigned __int32            m_blockSize;
        unsigned __int32            m_blockNumber;

        unsigned __int32            m_HeapKey;
        HeapInfo                    m_HeapInfo;

        vector<void*>               m_HeapUsable;
        unsigned __int32            m_HeapUsableIndex;
    } InfoReserved;

    typedef vector<InfoReserved*>   ListReserved;

    ListReserved                    m_heapReserved;
    InfoReserved*                   m_heapCurrent;

    unsigned __int32                m_blockRecent;
    unsigned __int32                m_blockSize;

    HeapPoolManaged<HeapInfo>       m_infoPool;

    void Release(InfoReserved* Info)
    {
        PlatformDepencyHeapFree(
            Info->m_HeapInfo.m_Object,
            Info->m_HeapInfo.m_szHeap
        );

        delete Info;
    }

public:
    HeapPoolNoneManaged(size_t szBlock)
    {
        m_blockSize = szBlock + szHeapInfo;
    }

    void Reserve(unsigned nBlock)
    {
        InfoReserved* Info = new InfoReserved();

        Info->m_blockNumber                     = nBlock;
        Info->m_blockSize                       = m_blockSize;

        Info->m_HeapUsable.resize(nBlock);
        Info->m_HeapUsableIndex                 = 0;
        Info->m_HeapKey                         = m_heapReserved.size() + 1;

        Info->m_HeapInfo.m_Flag                 = 0;
        Info->m_HeapInfo.m_HeapKey              = &(Info->m_HeapKey);
        Info->m_HeapInfo.m_Object               = PlatformDepencyHeapAlloc(m_blockSize * nBlock);
        Info->m_HeapInfo.m_Type                 = HeapType::HEAP_RESERVED;
        Info->m_HeapInfo.m_szHeap               = m_blockSize * nBlock;

        for (unsigned i = 0; i < nBlock; ++i)
        {
            Info->m_HeapUsable[i] 
                = OffsetAddress(Info->m_HeapInfo.m_Object, i * m_blockSize);
        }

        m_heapReserved.push_back(Info);

        // Adding sizeof(void*) becouse we need header.
        m_blockRecent  = nBlock;
        m_heapCurrent  = Info;
    }
    
    void Alloc(HeapHeader*& Header) throw(std::bad_alloc())
    {
        auto FindAndSetAvailableHeap = [this]() -> bool
        {
            for (InfoReserved* Heap : m_heapReserved)
            {
                if (Heap->m_HeapUsable.empty())
                {
                    continue;
                }
                m_heapCurrent = Heap;
                return true;
            }
            return false;
        };

        if (m_heapCurrent->m_HeapUsable.empty())
        {
            if (FindAndSetAvailableHeap() == false)
            {
                Reserve(m_blockRecent);
            }
        }

        try
        {
            Header = (HeapHeader*)m_heapCurrent->m_HeapUsable[m_heapCurrent->m_HeapUsableIndex];

            Header->m_HeapInfo = (HeapInfo*)m_infoPool.Alloc();
            Header->m_HeapInfo->m_Type    = HeapType::HEAP_MANAGED;
            Header->m_HeapInfo->m_Object  = Header + sizeof(void*);
            Header->m_HeapInfo->m_HeapKey = m_heapCurrent->m_HeapInfo.m_HeapKey;

            m_heapCurrent->m_HeapUsableIndex++;
        }
        catch (...)
        {
            throw std::bad_alloc();
        }
    }

    void Free(HeapHeader* Header)
    {
        InfoReserved* Info = m_heapReserved[ *(Header->m_HeapInfo->m_HeapKey) ];

        Info->m_HeapUsable[Info->m_HeapUsableIndex] = Header->m_HeapInfo->m_Object;
        Info->m_HeapUsableIndex--;
    }

    void Clean()
    {
        ListReserved    MarkedList;

        unsigned countNewKey = 0;
        for (InfoReserved* Heap : m_heapReserved)
        {
            if (Heap->m_HeapUsableIndex == 0)
            {
                Release(Heap);
            }
            Heap->m_HeapKey = countNewKey;
            MarkedList.push_back(Heap);

            countNewKey++;
        }

        swap(m_heapReserved, MarkedList);
    }
};

template <bool isThreadSafe>
class HeapPoolImpl  
{
    const int szHeader = 8;

    typedef size_t                          HeapSize;
    typedef unordered_map
        <HeapSize, HeapPoolNoneManaged*>    HeapPool;

    HeapPool                                m_PoolHeap;
    
    constexpr bool isHeapExist(HeapSize szHeap)
    {
        return m_PoolHeap.find(szHeap) != m_PoolHeap.end();
    }

    inline unsigned getAlignedSize(unsigned szBlock, unsigned Align)
    {
        return (szBlock + Align - (szBlock % Align));
    }

public:
    void Release()
    {
        for (auto Object : m_PoolHeap)
        {
            delete (Object.second);
        }
    }

    void* Alloc(size_t szHeap)
    {
        HeapHeader* Header = nullptr;
        const unsigned szAligned = getAlignedSize(szHeap, 8);

        if (isHeapExist(szAligned) == false)
        {
            m_PoolHeap.insert(make_pair(
                szAligned,
                new HeapPoolNoneManaged(szAligned))
            );
            m_PoolHeap[szAligned]->Reserve(128);
        }

        while (true)
        {
            try
            {
                m_PoolHeap[szAligned]->Alloc(Header);
                Header->m_HeapInfo->m_szHeap = szAligned;
                return OffsetAddress(Header, szHeader);
            }
            catch (std::bad_alloc exception)
            {

            }
        }
    }
    void  Free(void* pHeap)
    {
        HeapHeader* Header = (HeapHeader*)OffsetAddress(pHeap, -szHeader);

        m_PoolHeap[Header->m_HeapInfo->m_szHeap]->Free(Header);
    }
};

template <>
class HeapPoolImpl<true> : public HeapPoolImpl<false>
{
    mutex HeapMutex;

    typedef HeapPoolImpl<false> Original;

public:
    void Release()
    {
        lock_guard<mutex> lock(HeapMutex);
        Original::Release();
    }
    void* Alloc(size_t szHeap)
    {
        lock_guard<mutex> lock(HeapMutex);
        return Original::Alloc(szHeap);
    }
    void Free(void* pHeap, size_t szHeap)
    {
        lock_guard<mutex> lock(HeapMutex);
        Original::Free(pHeap);
    }
};

#pragma warning(pop)
#endif