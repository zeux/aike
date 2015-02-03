#pragma once

template <typename T> struct Array
{
	T* data;
	size_t size;
	size_t capacity;

	Array(): data(0), size(0), capacity(0)
	{
	}

	const T* begin() const
	{
		return data;
	}

	T* begin()
	{
		return data;
	}

	const T* end() const
	{
		return data + size;
	}

	T* end()
	{
		return data + size;
	}

	const T& operator[](size_t i) const
	{
		assert(i < size);
		return data[i];
	}

	T& operator[](size_t i)
	{
		assert(i < size);
		return data[i];
	}

	void push(const T& item)
	{
		if (size == capacity)
		{
			size_t new_capacity = capacity + capacity / 2 + 1;
			T* new_data = new T[new_capacity];

			for (size_t i = 0; i < capacity; ++i)
				new_data[i] = data[i];

			data = new_data;
			capacity = new_capacity;
		}

		data[size++] = item;
	}
};