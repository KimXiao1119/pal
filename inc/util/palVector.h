/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2015-2017 Advanced Micro Devices, Inc. All Rights Reserved.
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
/**
***********************************************************************************************************************
* @file  palVector.h
* @brief PAL utility collection Vector and VectorIterator class declarations.
***********************************************************************************************************************
*/

#pragma once

#include "palUtil.h"
#include "palAssert.h"
#include "palSysMemory.h"
#include <type_traits>

namespace Util
{

// Forward declarations.
template<typename T, uint32 defaultCapacity, typename Allocator> class Vector;

/**
 ***********************************************************************************************************************
 * @brief  Iterator for traversal of elements in Vector.
 *
 * Supports forward traversal.
 ***********************************************************************************************************************
 */
template<typename T, uint32 defaultCapacity, typename Allocator>
class VectorIterator
{
public:
    ~VectorIterator() { }

    /// Checks if the current index is within bounds of the number of elements in the vector.
    ///
    /// @returns True if the current element this iterator is pointing to is within the permitted range.
    bool IsValid() const { return (m_curIndex < m_srcVector.m_numElements); }

    /// Returns the element the iterator is currently pointing to as a reference.
    ///
    /// @warning This may cause an access violation if the iterator is not valid.
    ///
    /// @returns The element the iterator is currently pointing to.
    T& Get() const
    {
        PAL_ASSERT(IsValid());
        return (*(m_srcVector.m_pData + m_curIndex));
    }

    /// Advances the iterator to point to the next element.
    ///
    /// @warning Does not do bounds checking.
    void Next() { ++m_curIndex; }

    /// Retrieves the current vector position of this iterator.
    ///
    /// @returns The location in the vector of the element the iterator is currently pointing to.
    uint32 Position() const { return m_curIndex; }

private:
    VectorIterator(uint32 index, const Vector<T, defaultCapacity, Allocator>& srcVec);

    uint32                                        m_curIndex;  // The current index of the vector iterator.
    const Vector<T, defaultCapacity, Allocator>&  m_srcVector; // The vector container this iterator is used for.

    PAL_DISALLOW_DEFAULT_CTOR(VectorIterator);

    // Although this is a transgression of coding standards, it means that Vector does not need to have a public
    // interface specifically to implement this class. The added encapsulation this provides is worthwhile.
    friend class Vector<T, defaultCapacity, Allocator>;
};

/**
 ***********************************************************************************************************************
 * @brief Vector container.
 *
 * Vector is a templated array based storage that starts with a default-size allocation in the stack. If more space is
 * needed it then resorts to dynamic allocation by doubling the size every time the capacity is exceeded.
 * Operations which this class supports are:
 *
 * - Insertion at the end of the array.
 * - Forward iteration.
 * - Random access.
 *
 * @warning This class is not thread-safe.
 ***********************************************************************************************************************
 */
template<typename T, uint32 defaultCapacity, typename Allocator>
class Vector
{
public:
    /// A convenient shorthand for VectorIterator.
    typedef VectorIterator<T, defaultCapacity, Allocator> Iter;

    /// Constructor.
    ///
    /// @param [in] pAllocator The allocator that will allocate memory if required.
    Vector(Allocator*const pAllocator);

    /// Destructor.
    ~Vector();

    /// Copy an element to end of the vector. If not enough space is available, new space will be allocated and the old
    /// data will be copied to the new space.
    ///
    /// @param [in] data The element to be pushed to the vector. The element will become the last element.
    ///
    /// @returns Result ErrorOutOfMemory if the operation failed.
    Result PushBack(const T& data);

    /// Returns the element at the end of the vector and destroys it.
    ///
    /// @param [out] pData The element at the end of the vector
    void PopBack(T* pData);

    /// Destroys all elements stored in the vector. All dynamically allocated memory will be saved for reuse.
    void Clear();

    /// Returns the element at the location specified.
    ///
    /// @warning Calling this function with an out-of-bounds index will cause an access violation!
    ///
    /// @param [in] index Integer location of the element needed.
    ///
    /// @returns The element at location specified by index.
    T& At(uint32 index)
    {
        PAL_ASSERT(index < m_numElements);
        return *(m_pData + index);
    }

    /// Returns the element at the location specified.
    ///
    /// @warning Calling this function with an out-of-bounds index will cause an access violation!
    ///
    /// @param [in] index Integer location of the element needed.
    ///
    /// @returns The element at location specified by index.
    const T& At(uint32 index) const
    {
        PAL_ASSERT(index < m_numElements);
        return *(m_pData + index);
    }

    /// Returns the data at the front of the vector.
    ///
    /// @warning Calling this function on an empty vector will cause an access violation!
    ///
    /// @returns The data at the front of the vector.
    T& Front() const
    {
        PAL_ASSERT(IsEmpty() == false);
        return *m_pData;
    }

    /// Returns the data at the back of the vector.
    ///
    /// @warning Calling this function on an empty vector will cause an access violation!
    ///
    /// @returns The data at the back of the vector.
    T& Back() const
    {
        PAL_ASSERT(IsEmpty() == false);
        return *(m_pData + (m_numElements - 1));
    }

    /// Returns an iterator to the first element of the vector.
    ///
    /// @warning Accessing an element using an iterator of an empty vector will cause an access violation!
    ///
    /// @returns An iterator to first element of the vector.
    Iter Begin() const { return Iter(0, *this); }

    /// Returns an iterator to the last element of the vector.
    ///
    /// @warning Accessing an element using an iterator of an empty vector will cause an access violation!
    ///
    /// @returns VectorIterator An iterator to last element of the vector.
    Iter End() const { return Iter((m_numElements - 1), *this); }

    /// Returns the size of the vector.
    ///
    /// @returns An unsigned integer equal to the number of elements currently present in the vector.
    uint32 NumElements() const { return m_numElements; }

    /// Returns true if the number of elements present in the vector is equal to zero.
    ///
    /// @returns True if the vector is empty.
    bool IsEmpty() const { return (m_numElements == 0); }

private:
    // This is a POD-type that exactly fits one T value.
    typedef typename std::aligned_storage<sizeof(T), alignof(T)>::type ValueStorage;

    ValueStorage     m_data[defaultCapacity];  // The initial data buffer stored within the vector object.
    T*               m_pData;                  // Pointer to the current data buffer.
    uint32           m_numElements;            // Number of elements present.
    uint32           m_maxCapacity;            // Maximum size it can hold.
    Allocator*const  m_pAllocator;             // Allocator for this Vector.

    PAL_DISALLOW_COPY_AND_ASSIGN(Vector);

    // Although this is a transgression of coding standards, it prevents VectorIterator requiring a public constructor;
    // constructing a 'bare' VectorIterator (i.e. without calling Vector::GetIterator) can never be a legal operation,
    // so this means that these two classes are much safer to use.
    friend class VectorIterator<T, defaultCapacity, Allocator>;
};

// =====================================================================================================================
template<typename T, uint32 defaultCapacity, typename Allocator>
VectorIterator<T, defaultCapacity, Allocator>::VectorIterator(
    uint32                                       index,
    const Vector<T, defaultCapacity, Allocator>& srcVec)
    :
    m_curIndex(index),
    m_srcVector(srcVec)
 {
 }

// =====================================================================================================================
template<typename T, uint32 defaultCapacity, typename Allocator>
Vector<T, defaultCapacity, Allocator>::Vector(
    Allocator*const pAllocator)
    :
    m_pData(reinterpret_cast<T*>(m_data)),
    m_numElements(0),
    m_maxCapacity(defaultCapacity),
    m_pAllocator(pAllocator)
 {
 }

// =====================================================================================================================
template<typename T, uint32 defaultCapacity, typename Allocator>
Vector<T, defaultCapacity, Allocator>::~Vector()
{
    // Explicitly destroy all non-trivial types.
    if (!std::is_pod<T>::value)
    {
        for (uint32 idx = 0; idx < m_numElements; ++idx)
        {
            m_pData[idx].~T();
        }
    }

    // Check if we have dynamically allocated memory.
    if (m_pData != reinterpret_cast<T*>(m_data))
    {
        // Free the memory that was allocated dynamically.
        PAL_FREE(m_pData, m_pAllocator);
    }
}

} // Util
