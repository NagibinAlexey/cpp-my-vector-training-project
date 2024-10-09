#pragma once

#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <type_traits>
#include <algorithm>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;
    RawMemory(RawMemory&& other) noexcept { Swap(other); }
    RawMemory& operator=(RawMemory&& rhs) noexcept { Swap(rhs); }

    ~RawMemory() {
        Deallocate(buffer_);
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

    explicit Vector(size_t size) : data_(size), size_(size)
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other) : data_(other.size_), size_(other.size_)
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            }
            else {
                size_t copy_size{};
                if (rhs.size_ <= size_) {
                    copy_size = rhs.size_;
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                }
                else {
                    copy_size = size_;
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress() + size_);
                }

                std::copy_n(rhs.data_.GetAddress(), copy_size, data_.GetAddress());
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        Swap(rhs);
        return *this;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);

        MoveOrCopyData(data_, new_data, size_);

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        }
        else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }
        size_ = new_size;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        T* value_ = nullptr;
        if (size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            value_ = new (new_data + size_) T(std::forward <Args>(args) ...);

            MoveOrCopyData(data_, new_data, size_);

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        }
        else {
            value_ = new (data_ + size_) T(std::forward <Args>(args) ...);
        }
        ++size_;
        return *value_;
    }

    void PopBack() noexcept {
        std::destroy_at(end() - 1);
        --size_;
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        if (size_ == Capacity()) {
            return EmplaceRealloc(pos, std::forward <Args>(args) ...);
        }
        return EmplaceMove(pos, std::forward <Args>(args) ...);
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        iterator pos_ = const_cast<iterator>(pos);
        std::move(pos_ + 1, end(), pos_);
        PopBack();
        return pos_;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    void MoveOrCopyData(RawMemory<T>& data, RawMemory<T>& new_data, size_t size) {
        // constexpr оператор if будет вычислен во время компиляции
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data.GetAddress(), size, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data.GetAddress(), size, new_data.GetAddress());
        }
    }

    template <typename... Args>
    iterator EmplaceRealloc(const_iterator pos, Args&&... args) {
        size_t index_ = pos - begin();
        iterator value_ptr = nullptr;

        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
        value_ptr = new (new_data + index_) T(std::forward <Args>(args) ...);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(begin(), index_, new_data.GetAddress());
        }
        else {
            try {
                std::uninitialized_copy_n(begin(), index_, new_data.GetAddress());
            }
            catch (...) {
                std::destroy_at(new_data.GetAddress() + index_);
                throw;
            }
        }
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(begin() + index_, size_ - index_, new_data.GetAddress() + index_ + 1);
        }
        else {
            try {
                std::uninitialized_copy_n(begin() + index_, size_ - index_, new_data.GetAddress() + index_ + 1);
            }
            catch (...) {
                std::destroy_n(new_data.GetAddress(), index_);
                throw;
            }
        }
        std::destroy_n(begin(), size_);
        data_.Swap(new_data);

        ++size_;
        return value_ptr;
    }

    template <typename... Args>
    iterator EmplaceMove(const_iterator pos, Args&&... args) {
        size_t index_ = pos - begin();
        iterator value_ptr = nullptr;

        if (size_ != 0) {
            new (data_ + size_) T(std::move(*(begin() + size_ - 1)));
            try {
                std::move_backward(begin() + index_, begin() + size_, begin() + size_ + 1);
            }
            catch (...) {
                std::destroy_at(begin() + size_);
                throw;
            }
            std::destroy_at(begin() + index_);
        }
        value_ptr = new (data_ + index_) T(std::forward <Args>(args) ...);

        ++size_;
        return value_ptr;
    }
};