#include "common.hpp"

#include "types.hpp"
#include "typeinfo.hpp"

AIKE_EXTERN void println(AikeString str)
{
	printf("%.*s\n", int(str.size), str.data);
}

AIKE_EXTERN void printfn(AikeString format, ...)
{
	char* buffer = static_cast<char*>(alloca(format.size + 2));

	memcpy(buffer, format.data, format.size);
	buffer[format.size + 0] = '\n';
	buffer[format.size + 1] = 0;

	va_list args;
	va_start(args, format);
	vprintf(buffer, args);
	va_end(args);
}

static void print(TypeInfo* type, void* value)
{
	if (type->kind == TypeInfo::KindVoid)
	{
		printf("()");
	}
	else if (type->kind == TypeInfo::KindBool)
	{
		printf("%s", *static_cast<bool*>(value) ? "true" : "false");
	}
	else if (type->kind == TypeInfo::KindInteger)
	{
		printf("%d", *static_cast<int*>(value));
	}
	else if (type->kind == TypeInfo::KindFloat)
	{
		printf("%g", *static_cast<float*>(value));
	}
	else if (type->kind == TypeInfo::KindString)
	{
		auto data = static_cast<AikeString*>(value);

		printf("%.*s", int(data->size), data->data);
	}
	else if (type->kind == TypeInfo::KindArray)
	{
		auto data = static_cast<AikeArray<char>*>(value);

		printf("[");

		for (int i = 0; i < data->size; ++i)
		{
			if (i != 0) printf(", ");

			print(type->dataArray.element, data->data + i * type->dataArray.stride);
		}

		printf("]");
	}
	else if (type->kind == TypeInfo::KindPointer)
	{
		printf("%p", *static_cast<void**>(value));
	}
	else if (type->kind == TypeInfo::KindFunction)
	{
		printf("fun(%p)", *static_cast<void**>(value));
	}
	else if (type->kind == TypeInfo::KindStruct)
	{
		printf("%s {", type->dataStruct.name);

		for (int i = 0; i < type->dataStruct.fieldCount; ++i)
		{
			const TypeInfo::StructField& f = type->dataStruct.fields[i];

			printf(" %s=", f.name);
			print(f.type, static_cast<char*>(value) + f.offset);
		}

		printf(" }");
	}
	else
	{
		printf("?");
	}
}

AIKE_EXTERN void print(AikeAny* data, int count)
{
	for (int i = 0; i < count; ++i)
	{
		if (i != 0) printf(" ");
		print(data[i].type, data[i].value);
	}

	printf("\n");
}