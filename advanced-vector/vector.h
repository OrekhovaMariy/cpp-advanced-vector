#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <stdexcept>
#include <algorithm>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;
    RawMemory(RawMemory&& other) noexcept 
        : buffer_(std::move(other.GetAddress()))
        , capacity_(std::move(other.Capacity())) {
    }
    RawMemory& operator=(RawMemory&& rhs) noexcept {
        buffer_ = std::move(rhs.GetAddress());
        capacity_ = std::move(rhs.Capacity());
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};


template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept {
        return data_.GetAddress();
    }
    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }
    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }
    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }

    Vector() = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size)  //
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector & other)
        : data_(other.size_)
        , size_(other.size_)
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    void MoveOrCopyRawMemory(RawMemory<T>& other) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
        {
            std::uninitialized_move_n(data_.GetAddress(), size_, other.GetAddress());
        }
        else
        {
            std::uninitialized_copy_n(data_.GetAddress(), size_, other.GetAddress());
        }
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        MoveOrCopyRawMemory(new_data);
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress()+ new_size, size_ - new_size);
            size_ = new_size;
        }
        else if (new_size > size_) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            size_ = new_size;
        }
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }
    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *(Emplace(cend(), std::forward<Args>(args)...));
    }

    void PopBack() noexcept {
        assert(size_ > 0);
        std::destroy_n(data_ + size_ - 1, 1);
        --size_;
    }

    template <typename... Args>
    void EmplaceWhithFullCapacity(size_t distance, Args&&... args) {
        size_t new_capacity = size_ == 0 ? 1 : Capacity() * 2;
        RawMemory<T> new_data(new_capacity);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
        {
            std::uninitialized_move_n(data_.GetAddress(), distance, new_data.GetAddress());
            new(new_data + distance) T(std::forward<Args>(args)...);
            std::uninitialized_move_n(data_.GetAddress() + distance, size_ - distance, new_data.GetAddress() + distance + 1);
        }
        else
        {
            try {
                std::uninitialized_copy_n(data_.GetAddress(), distance, new_data.GetAddress());
            }
            catch (...) {
                throw;
            }

            try
            {
                new(new_data + distance) T(std::forward<Args>(args)...);
            }
            catch (...) {
                throw;
            }

            try {
                std::uninitialized_copy_n(data_.GetAddress() + distance, size_ - distance, new_data.GetAddress() + distance + 1);
            }
            catch (...) {
                throw;
            }
        }
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    template <typename... Args>
    void EmplaceWithEmptyCapacity(size_t distance, Args&&... args) {
        new(end()) T(std::forward<T>(data_[size_ - 1]));
        T tmp(std::forward<Args>(args)...);
        std::move_backward(begin() + distance, end() - 1, end());
        *(data_ + distance) = std::move(tmp);
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args)
    {
        assert(pos <= cend() && pos >= cbegin());
        size_t dist = std::distance(cbegin(), pos);
        if (size_ == Capacity()) {
            EmplaceWhithFullCapacity(dist, std::forward<Args>(args)...);
        }
        else if (pos == cend())
        {
            new(data_ + size_) T(std::forward<Args>(args)...);
        }
        else {
            EmplaceWithEmptyCapacity(dist, std::forward<Args>(args)...);
        }
        ++size_;
        return data_ + dist;
    }

    iterator Erase(const_iterator pos) {
        assert(size_ > 0);
        assert(pos <= end() && pos >= begin());
        size_t dist = std::distance(cbegin(), pos);
        std::move(begin() + dist + 1, end(), begin() + dist);
        std::destroy_n(data_ + size_ - 1, 1);
        --size_;
        return begin() + dist;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }
    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    ~Vector() {
        std::destroy_n(data_ + 0, size_);
    }
    
    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            }
        else {
                if (Size() > rhs.Size()) {
                    for (size_t i = 0; i < rhs.size_; ++i)
                    {
                        data_[i] = std::move(rhs.data_[i]);
                    }
                    std::destroy_n(data_ + rhs.Size(), Size() - rhs.Size());
                }
                else {
                    for (size_t i = 0; i < size_; ++i)
                    {
                        data_[i] = std::move(rhs.data_[i]);
                    }
                    std::uninitialized_copy_n(rhs.data_ + Size(), rhs.Size() - Size(), data_.GetAddress() + size_);
                }
            }
        }
        size_ = rhs.Size();
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            if (rhs.Size() > data_.Capacity()) {
                Swap(rhs);
            }
            else {
                data_.Swap(rhs.data_);
                size_ = rhs.Size();
            }
        }
        return *this;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};
