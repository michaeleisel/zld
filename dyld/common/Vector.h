/*
* Copyright (c) 2019 Apple Inc. All rights reserved.
*
* @APPLE_LICENSE_HEADER_START@
*
* "Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
* Reserved.  This file contains Original Code and/or Modifications of
* Original Code as defined in and that are subject to the Apple Public
* Source License Version 1.0 (the 'License').  You may not use this file
* except in compliance with the License.  Please obtain a copy of the
* License at http://www.apple.com/publicsource and read it before using
* this file.
*
* The Original Code and all software distributed under the License are
* distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
* EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
* INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
* License for the specific language governing rights and limitations
* under the License."
*
* @APPLE_LICENSE_HEADER_END@
*/

// This is a mostly complete reimplementation of std::vector that can be safely used in dyld
// It does not support a methods we don't use like max_capacity

//FIXME: Implement back()
//FIXME: All the erase functions are broken

#ifndef DRL_Vector_h
#define DRL_Vector_h

#include <cassert>
#include <cstdlib>
#include <cstddef>
#include <algorithm>

#include "Defines.h"
#include "BitUtils.h"
#include "Allocator.h"


namespace dyld4
{

template<typename T>
struct TRIVIAL_ABI Vector {
#pragma mark -
#pragma mark Typedefs
    typedef T                   value_type;
    typedef std::size_t         size_type;
    typedef std::ptrdiff_t      difference_type;
    typedef value_type&         reference;
    typedef const value_type&   const_reference;
    typedef value_type*         pointer;
    typedef const value_type*   const_pointer;
    typedef value_type*         iterator;
    typedef const value_type*   const_iterator;
#pragma mark -
#pragma mark Constructors / Destructors / Assignment Operators / swap
    Vector() = delete;
    explicit Vector(Allocator* A) : _allocator(A) {}
    Vector(const Vector& O, Allocator* A) :
            _allocator(A),
            _size(O._size),
            _capacity(O._capacity) {
        assert(_allocator != nullptr);
        auto [newBuffer, newBufferSize] = _allocator->allocate_buffer(sizeof(T[_capacity]), alignof(T));
        _capacity = newBufferSize / sizeof(T);
        _buffer = (value_type *)newBuffer;
        std::move(O.begin(), O.end(), &_buffer[0]);
    }
    Vector(const Vector& O) : Vector(O, O._allocator) {}
    Vector(Vector&& O, Allocator* A) : _allocator(A) {
        std::swap(_size,        O._size);
        std::swap(_capacity,    O._capacity);
        if (_allocator == O._allocator) {
            std::swap(_buffer,      O._buffer);
        } else {
            auto [newBuffer, newBufferSize] = _allocator->allocate_buffer(sizeof(T[_capacity]), alignof(T));
            _capacity = newBufferSize / sizeof(T);
            _buffer = (value_type *)newBuffer;
            std::move(O.begin(), O.end(), &_buffer[0]);
        }
    }
    Vector(Vector&& O) : Vector(O, O._allocator) {}
    template< class InputIt >
    Vector(InputIt first, InputIt last, Allocator* A) : Vector(A) {
        _size = last-first;
        reserve(_size);
        std::move(first, last, begin());
    }
    static Vector<T>* make(Allocator& allocator) {
        void* storage = allocator.malloc(sizeof(Vector<T>));
        return new (storage) Vector<T>(&allocator);
    }
    Vector(std::initializer_list<T> I, Allocator* A) : Vector(I.begin(), I.end(), A) {}
    ~Vector() {
        if (_buffer) {
            assert(_allocator != nullptr);
            clear();
            _allocator->deallocate_bytes((void*)_buffer, sizeof(T)*_capacity, alignof(T));
        }
    }
    void swap(Vector& O) {
        std::swap(_allocator,   O._allocator);
        std::swap(_buffer,      O._buffer);
        std::swap(_size,        O._size);
        std::swap(_capacity,    O._capacity);
    }
    Vector& operator=(const Vector& O) {
        assert(_allocator != nullptr);
        if (_size) {
            clear();
        }
        if (_capacity) {
            _allocator->deallocate_bytes((void*)_buffer, sizeof(T)*_capacity, alignof(T));
        }
        _size       = O._size;
        _capacity   = O._capacity;
        auto [newBuffer, newBufferSize] = _allocator->allocate_buffer(sizeof(T[_capacity]), alignof(T));
        _capacity = newBufferSize / sizeof(T);
        _buffer = (value_type *)newBuffer;
        std::move(O.begin(), O.end(), &_buffer[0]);
        return *this;
    }
    Vector& operator=(Vector&& O) {
        swap(O);
        return *this;
    }
//    Vector& operator=(std::initializer_list<T> I) {
//        clear();
//        _size = I.size();
//        reserve(_size);
//        std::copy(I.begin(), I.end(), begin());
//        return *this;
//    }
#pragma mark -
#pragma mark Iterator support
    iterator begin()                                { return &_buffer[0]; }
    iterator end()                                  { return &_buffer[_size]; }
    const_iterator begin() const                    { return &_buffer[0]; }
    const_iterator end() const                      { return &_buffer[_size]; }
    const_iterator cbegin() const noexcept          { return &_buffer[0]; }
    const_iterator cend() const noexcept            { return &_buffer[_size]; }

    reference at(size_type pos)                     { return _buffer[pos]; }
    const_reference at(size_type pos) const         { return _buffer[pos]; }
    reference       operator[](size_type pos)       { return _buffer[pos]; }
    const_reference operator[](size_type pos) const { return _buffer[pos]; }
    
    reference       front()                         { return _buffer[0]; }
    const_reference front() const                   { return _buffer[0]; }
    reference       back()                          { return _buffer[_size-1]; }
    const_reference back() const                    { return _buffer[_size-1]; }
#pragma mark -
    constexpr pointer data()                        { return &_buffer[0]; }
    constexpr const_pointer data() const            { return &_buffer[0]; }
    
    [[nodiscard]] constexpr bool empty() const      { return (_size == 0); }
    size_type size() const                          { return _size; }
    size_type capacity() const                      { return _capacity; }
    void clear() {
        if constexpr(!std::is_trivially_destructible<value_type>::value) {
            for (auto i = begin(); i != end(); ++i) {
                i->~value_type();
            }
        }
        _size = 0;
    }
    void reserve(size_type new_cap) {
        assert(_allocator != nullptr);
        if (new_cap <= _capacity) { return; }
        auto oldCapacity = _capacity;
        _capacity = new_cap;
        if (_capacity < 16) {
            _capacity = 16;
        } else {
            _capacity = (size_t)bit_ceil(_capacity);
        }
        auto [newBuffer, newBufferSize] = _allocator->allocate_buffer(sizeof(T)*_capacity, std::max(16UL, alignof(T)));
        _capacity = newBufferSize / sizeof(T);
        if (_buffer) {
            std::move(begin(), end(), (value_type *)newBuffer);
            _allocator->deallocate_bytes((void*)_buffer, sizeof(T)*oldCapacity, alignof(T));
        }
        _buffer = (value_type *)newBuffer;
    }
    iterator insert( const_iterator pos, const T& value ) {
        auto offset = pos-begin();
        reserve(_size+1);
        std::move_backward(&_buffer[offset], &_buffer[_size], &_buffer[_size+1]);
        ++_size;
        _buffer[offset] = value;
        return &_buffer[offset];
    }
    iterator insert( const_iterator pos, T&& value ) {
        auto offset = pos-begin();
        reserve(_size+1);
        std::move_backward(&_buffer[offset], &_buffer[_size], &_buffer[_size+1]);
        ++_size;
        std::swap(_buffer[offset], value);
        return &_buffer[offset];
    }
    iterator insert( const_iterator pos, size_type count, const T& value ) {
        auto offset = pos-begin();
        reserve(_size+count);
        std::move_backward(&_buffer[offset], &_buffer[_size], &_buffer[_size+count]);
        for(auto i = 0; i < count; ++i) {
            _buffer[offset+i] = value;
        }
        return &_buffer[offset];
    }
    template< class InputIt >
    iterator insert( const_iterator pos, InputIt first, InputIt last ) {
        auto offset = pos-begin();
        auto count = last-first;
        reserve(_size+count);
        std::move_backward(&_buffer[offset], &_buffer[_size], &_buffer[_size+count]);
        std::move(first, last, &_buffer[offset]);
        _size += count;
        return &_buffer[offset];
    }
    iterator erase(iterator pos) {
        assert(_size > 0);
        std::move(pos+1, end(), pos);
        --_size;
        return &_buffer[pos-begin()];
    }
    iterator erase(const_iterator pos) {
        assert(_size > 0);
        std::move(pos+1, cend(), (iterator)pos);
        --_size;
        return &_buffer[pos-cbegin()];
    }
    iterator erase(iterator first, iterator last) {
        uint64_t count = (last-first);
        std::move(last, end(), first);
        _size -= count;
        return &_buffer[first-begin()];

    }
    iterator erase(const_iterator first, const_iterator last) {
        uint64_t count = (last-first);
        std::move(last, cend(), (iterator)first);
        _size -= count;
        return &_buffer[first-cbegin()];
        
    }
    void push_back(const T& value) {
        reserve(_size+1);
        _buffer[_size++] = value;
    }
    void push_back(T&& value) {
        reserve(_size+1);
        _buffer[_size++] = std::move(value);
    }
    template< class... Args >
    reference emplace_back( Args&&... args ) {
        reserve(_size+1);
        (void)new((void*)&_buffer[_size]) value_type(std::forward<Args>(args)...);
        return _buffer[_size++];
    }
    
    void pop_back() {
        if constexpr(!std::is_trivially_destructible<value_type>::value) {
            _buffer[_size].~value_type();
        }
        _size--;
    }
    
    Allocator* allocator() const {
        return _allocator;
    }
private:
    Allocator* _allocator           = nullptr;
    value_type* _buffer             = nullptr;
    size_t _size                    = 0;
    size_t _capacity                = 0;
};

} // namespace dyld4

#endif /* DRL_Vector_h */
