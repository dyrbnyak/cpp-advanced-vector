#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>

// Класс оберкта для буффера.
// Упростит жизнь тем, что сам будет очищать буффер
template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    //std::exchange — вспомогательная функция из заголовка <utility> в C++,
    //которая заменяет значение объекта новым значением и возвращает его старое значение.
    RawMemory(RawMemory&& other) noexcept
        : buffer_(std::exchange(other.buffer_, nullptr))
        , capacity_(std::exchange(other.capacity_, 0)){}

    ~RawMemory() {
        Deallocate(buffer_);
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;


    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if(this != &rhs){
            Swap(rhs);
        }

        return *this;
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

    void SetCapacity(size_t new_capacity){
        capacity_ = new_capacity;
    }

    T* Reset() noexcept{
        T* return_value = GetAddress();
        buffer_ = nullptr;

        return return_value;
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
private:
    RawMemory<T> data_;
    size_t size_ = 0;

public:
    // Конструктор по умолчанию.
    // Инициализирует вектор нулевого размера и вместимости. Не выбрасывает исключений.
    // Алгоритмическая сложность: O(1)
    Vector() = default;

    // Конструктор, который создаёт вектор заданного размера.
    // Вместимость созданного вектора равна его размеру,
    // а элементы проинициализированы значением по умолчанию для типа T.
    // Алгоритмическая сложность: O(размер вектора).
    explicit Vector(size_t size)
        : data_(size)
        , size_(size)
    {
        std::uninitialized_default_construct_n(data_.GetAddress(), size);
    }

    // Копирующий конструктор.
    // Создаёт копию элементов исходного вектора.
    // Имеет вместимость, равную размеру исходного вектора, то есть выделяет память без запаса.
    // Алгоритмическая сложность: O(размер исходного вектора).
    explicit Vector(const Vector& other)
        : data_(other.Size())
        , size_(other.Size())
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_,data_.GetAddress());
    }

    // Перемещающий конструктор.
    // Алгоритмическая сложность: O(1).
    Vector(Vector&& other) noexcept
        : data_(std::move(other.data_))
        , size_(other.size_) {
        other.size_ = 0;
    }

    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept{return data_.GetAddress();}
    iterator end() noexcept{return data_.GetAddress() + size_;}

    const_iterator begin() const noexcept{return data_.GetAddress();}
    const_iterator end() const noexcept{return data_.GetAddress() + size_;}

    const_iterator cbegin() const noexcept{return data_.GetAddress();}
    const_iterator cend() const noexcept{return data_.GetAddress() + size_;}

    void Resize(size_t new_size){
        if(size_ < new_size){
            Reserve(new_size);

            size_t count_element_for_construct = new_size - size_;
            std::uninitialized_value_construct_n(data_ + size_,count_element_for_construct);

            size_ = new_size;

        } else if(size_ > new_size){
            size_t count_element_for_destroy = size_ - new_size;
            std::destroy_n(data_ + new_size, count_element_for_destroy);

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
    T& EmplaceBack(Args&&... args){
        if (size_ == data_.Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);

            // Размещаем новый элемент последним (всегда перемещаем для rvalue)
            new (new_data + size_) T(std::forward<Args>(args)...);

            try {
                // Выбираем стратегию переноса старых элементов
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                    // Можно безопасно перемещать старые элементы
                    std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
                } else {
                    // Приходится копировать старые элементы
                    std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
                }
            } catch (...) {
                std::destroy_at(new_data + size_);
                throw;
            }

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);


            return data_[size_++];
        } else {
            new (data_ + size_) T(std::forward<Args>(args)...);
            return data_[size_++];
        }
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        //Проверка на вхождение в диапозон
        assert(pos >= begin() && pos <= end());

        // Определяем позицию вставки как смещение от начала
        size_t index = pos - begin();

        if (size_ == data_.Capacity()) {
            // Случай 1: требуется релокация памяти
            return EmplaceWithReallocation(index, std::forward<Args>(args)...);

        } else {
            // Случай 2: релокация не нужна
            return EmplaceWithoutReallocation(index, std::forward<Args>(args)...);
        }

    }


    // Insert на основе Emplace
    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    iterator Erase(const_iterator pos) {
        //Проверка на вхождение в диапозон
        assert(pos >= begin() && pos <= end());

        size_t index = pos - begin();

        // Сдвигаем элементы влево
        std::move(data_.GetAddress() + index + 1,
                  data_.GetAddress() + size_,
                  data_.GetAddress() + index);

        // Уничтожаем последний элемент (который теперь "дублируется")
        std::destroy_at(data_ + size_ - 1);

        --size_;
        return begin() + index;
    }

    bool Empty() const noexcept{
        return size_ == 0;
    }


    void PopBack() noexcept{
        if(!Empty()){
            std::destroy_at(data_ + (size_ - 1));
            size_--;
        }

    }



    // Метод void Reserve(size_t capacity).
    // Резервирует достаточно места, чтобы вместить количество элементов, равное capacity.
    // Если новая вместимость не превышает текущую, метод не делает ничего.
    // Алгоритмическая сложность: O(размер вектора).
    void Reserve(size_t new_capacity){
        if(new_capacity <= data_.Capacity()){
            return;
        }

        //Выделяем память под новое место
        RawMemory<T> new_data(new_capacity);

        //Перемещение можно,
        //если соблюдается хотя бы одно из условий:
        //  конструктор перемещения типа T не выбрасывает исключений;
        //  тип T не имеет копирующего конструктора
        //В остальных случаях - копирывание
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());

        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }

        // Разрушаем элементы в data_
        std::destroy_n(data_.GetAddress(), size_);

        // Избавляемся от старой сырой памяти, обменивая её на новую
        data_.Swap(new_data);
        // При выходе из метода старая память будет возвращена в кучу

        //Меняем значение вместимости
        data_.SetCapacity(new_capacity);
    }

    Vector& operator=(const Vector& rhs){
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                //Размер больше вместимости
                /* copy-and-swap */
                Vector buffer(rhs);
                Swap(buffer);

            } else {
                //Размер меньше вместимости
                CopyAssignNoReallocation(rhs);
            }
        }
        return *this;
    }


    Vector& operator=(Vector&& rhs) noexcept{
        if (this != &rhs) {
            Swap(rhs);
            rhs.size_ = 0;
        }
        return *this;
    }


    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    [[nodiscard]] size_t Size() const noexcept {
        return size_;
    }

    [[nodiscard]] size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        // Компилятор автоматически преобразует data_[index] в *(data_ + index)
        return data_[index];
    }

    ~Vector(){
        DestroyN(data_.GetAddress(), size_);
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

    // Удаляет n элементов из буфера
    static void DestroyN(T* buf, size_t n) noexcept {
        for (size_t i = 0; i != n; ++i) {
            Destroy(buf + i);
        }
    }

    // Создаёт копию объекта elem в сырой памяти по адресу buf
    static void CopyConstruct(T* buf, const T& elem) {
        new (buf) T(elem);
    }

    // Вызывает деструктор объекта по адресу buf
    static void Destroy(T* buf) noexcept {
        buf->~T();
    }

    void CopyAssignNoReallocation(const Vector& rhs){
        // 1. Обновляем существующие элементы
        auto copy_count = std::min(size_, rhs.size_);
        std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + copy_count, data_.GetAddress());

        size_t i = copy_count;

        // Если rhs больше - добавляем новые элементы
        if (rhs.size_ > size_) {
            std::uninitialized_copy_n(rhs.data_ + i, rhs.size_ - i, data_ + i);
        }

        // Если rhs меньше - удаляем лишние
        else if (size_ > rhs.size_) {
            DestroyN(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
        }

        size_ = rhs.size_;
    }

    template <typename... Args>
    iterator EmplaceWithReallocation(size_t index, Args&&... args) {
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);

        // 1) Сначала конструируем новый элемент в новой памяти
        //    Это защищает от ситуации, когда args ссылается на элемент текущего вектора

        // Если исключение, новая память сама очистится через деструктор RawMemory
        new (new_data + index) T(std::forward<Args>(args)...);

        // 2) Копируем/перемещаем элементы, которые идут ДО позиции вставки
        if (index > 0) {
            try {
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move_n(data_.GetAddress(), index, new_data.GetAddress());
                } else {
                    std::uninitialized_copy_n(data_.GetAddress(), index, new_data.GetAddress());
                }
            } catch (...) {
                // Уничтожаем уже созданный новый элемент
                std::destroy_at(new_data + index);
                throw;
            }
        }

        // 3) Копируем/перемещаем элементы, которые идут ПОСЛЕ позиции вставки
        if (size_ > index) {
            try {
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move_n(data_.GetAddress() + index, size_ - index,
                                              new_data.GetAddress() + index + 1);
                } else {
                    std::uninitialized_copy_n(data_.GetAddress() + index, size_ - index,
                                              new_data.GetAddress() + index + 1);
                }
            } catch (...) {
                // Уничтожаем всё, что успели создать: [0, index] + вставленный элемент
                std::destroy_n(new_data.GetAddress(), index + 1);
                throw;
            }
        }

        // 4) Уничтожаем старые элементы и заменяем буфер
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
        ++size_;

        return begin() + index;
    }

    template <typename... Args>
    iterator EmplaceWithoutReallocation(size_t index, Args&&... args) {
        // Вставка в конец
        if (index == size_) {
            EmplaceBack(std::forward<Args>(args)...);
            return begin() + index;
        }

        // Создаём временную копию/перемещение вставляемого значения (защита от самовставки)
        T temp(std::forward<Args>(args)...);

        // Сначала создаём копию последнего элемента в неинициализированной области
        new (data_ + size_) T(std::move(data_[size_ - 1]));

        // Сдвигаем диапазон [index, size_-1] вправо на 1
        // Используем move_backward, чтобы не затереть элементы
        try{
            std::move_backward(data_.GetAddress() + index,
                               data_.GetAddress() + size_ - 1,
                               data_.GetAddress() + size_);

        }catch(...){
            std::destroy_at(data_ + size_);
            throw;
        }

        // Вставляем временное значение
        data_[index] = std::move(temp);
        ++size_;

        return begin() + index;
    }
};