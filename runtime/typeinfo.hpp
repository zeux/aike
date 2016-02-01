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
		KindTuple,
		KindArray,
		KindPointer,
		KindFunction,
		KindStruct,
	};

	struct TupleField
	{
		TypeInfo* type;
		int offset;
	};

	struct StructField
	{
		const char* name;
		TypeInfo* type;
		int offset;
	};

	Kind kind;
	struct { Kind kind; int fieldCount; TupleField fields[1]; } dataTuple;
	struct { Kind kind; TypeInfo* element; int stride; } dataArray;
	struct { Kind kind; TypeInfo* element; } dataPointer;
	struct { Kind kind; const char* name; int fieldCount; StructField fields[1]; } dataStruct;
};
