#pragma once

template <typename T> struct Arr
{
	T* data;
	size_t size;
	size_t capacity;

	Arr(): data(0), size(0), capacity(0)
	{
	}

	template <typename It> Arr(It begin, It end): data(0), size(0), capacity(0)
	{
		capacity = end - begin;
		data = new T[capacity];

		for (It it = begin; it != end; ++it)
			data[size++] = *it;
	}

	Arr(initializer_list<T> list): Arr(list.begin(), list.end())
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