#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" int entrypoint();

#if defined(__linux)
	#define AIKE_EXTERN extern "C" __attribute__ ((visibility("default")))
#else
	#define AIKE_EXTERN extern "C" __declspec(dllexport)
#endif

template <typename T> struct AikeArray
{
	T* data;
	int length;
};

AIKE_EXTERN
void printint(int value)
{
    printf("%d\n", value);
}

AIKE_EXTERN
void printi(int value)
{
    printf("%d", value);
}

AIKE_EXTERN
void print(AikeArray<char> value)
{
    fwrite(value.data, value.length, 1, stdout);
}

AIKE_EXTERN
AikeArray<char> readfile(AikeArray<char> path)
{
    char* cpath = static_cast<char*>(malloc(path.length + 1));
    memcpy(cpath, path.data, path.length);
    cpath[path.length] = 0;

    FILE* file = fopen(cpath, "r");

    free(cpath);

    if (!file)
    {
        AikeArray<char> ret = {};
        return ret;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* result = static_cast<char*>(malloc(length));

    fread(result, length, 1, file);
    fclose(file);

	AikeArray<char> ret = {result, length};
	return ret;
}

extern "C" void _chkstk()
{
}

int main()
{
    printf("%d\n", entrypoint());
}
