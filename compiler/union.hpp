#pragma once

#define UNION_CASE(kindname, var, value) \
	auto var = ((value)->kind == (value)->Kind##kindname) \
		? &(value)->data##kindname \
		: nullptr

#define UNION_MAKE(type, kindname, ...) \
		([&]() -> type { \
			type __result = { type::Kind##kindname, 0 }; \
			__result.data##kindname = { __VA_ARGS__ }; \
			return __result; \
		})()

#define UNION_NEW(type, kindname, ...) \
		([&]() -> type* { \
			type* __result = new type { type::Kind##kindname, 0 }; \
			__result->data##kindname = __VA_ARGS__; \
			return __result; \
		})()

#define UNION_DECL_KIND(name, ...) Kind##name,
#define UNION_DECL_STRUCT(name, ...) struct name __VA_ARGS__;
#define UNION_DECL_FIELD(name, ...) name data##name;

#define UNION_DECL(name, def) \
	struct name { \
		enum Kind { def(UNION_DECL_KIND) } kind; \
		def(UNION_DECL_STRUCT) \
		union { int dummy; def(UNION_DECL_FIELD) }; \
	};