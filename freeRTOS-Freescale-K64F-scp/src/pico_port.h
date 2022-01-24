#ifndef PICO_PORT_H
#define PICO_PORT_H
#include "FreeRTOS.h"
#include "task.h"
#include "portmacro.h"
#include "pico_stack.h"

#define PICO_TIME_MS() xTaskGetTickCount()
#define PICO_TIME()    (xTaskGetTickCount() / configTICK_RATE_HZ)
#define PICO_IDLE()    do{}while(0)

#define dbg(...)

#define PICO_SUPPORT_MUTEX

#define pico_free(x) vPortFree(x)
#define free(x)      vPortFree(x)

static inline void *pico_zalloc(size_t size)
{
    void *ptr = pvPortMalloc(size);

    if(ptr)
        memset(ptr, 0u, size);

    return ptr;
}

#define malloc(x) pico_zalloc(x)



#endif

