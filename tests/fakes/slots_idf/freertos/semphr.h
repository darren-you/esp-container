#ifndef TEST_SEMPHR_H
#define TEST_SEMPHR_H

typedef struct test_semaphore *SemaphoreHandle_t;
#define pdTRUE 1
#define pdFALSE 0
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned ticks);
int xSemaphoreGive(SemaphoreHandle_t semaphore);

#endif
