#define VMA_IMPLEMENTATION
#ifdef DEBUG_ENABLED
#ifndef _MSC_VER
#define _DEBUG
#endif
#endif

// Makes integrated AMD GPUs not crash when Godot exceeds
// more than 4096 allocations
#define VMA_DEBUG_DONT_EXCEED_MAX_MEMORY_ALLOCATION_COUNT 0

#include "vk_mem_alloc.h"
