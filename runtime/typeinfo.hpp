#pragma once

union TypeInfo
{
	enum Kind
	{
		KindVoid,
		KindBool,
		KindInteger,
		KindFloat,
		KindString,
		KindArray,
		KindPointer,
		KindFunction,
		KindStruct,
	};

	struct StructField
	{
		const char* name;
		TypeInfo* type;
		int offset;
	};

	Kind kind;
	struct { Kind kind; TypeInfo* element; } dataArray;
	struct { Kind kind; TypeInfo* element; } dataPointer;
	struct { Kind kind; const char* name; int fieldCount; StructField fields[1]; } dataStruct;
};
