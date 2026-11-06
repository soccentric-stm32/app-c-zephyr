/*
 * flatcc_port.h - flatcc runtime allocation hooks
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  STM32F767ZIT6
 * SRS:     DRT-004 (no allocation after start-up), DCON-002 (flatcc runtime)
 *
 * The flatcc runtime is compiled with FLATCC_ALLOC/FLATCC_FREE/... mapped to
 * these functions, which use a fixed static arena.  This header is force-
 * included into every translation unit in CMakeLists.txt so the macro
 * expansion sees the prototypes.
 */

#ifndef FLATCC_PORT_H
#define FLATCC_PORT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Allocate @p size bytes from the flatcc fixed static arena.
 *
 * @param size Requested size in bytes.
 * @return Pointer to usable memory, or NULL on arena exhaustion.
 */
void *app_flatcc_alloc(size_t size);

/**
 * @brief Release a block previously obtained from the flatcc arena.
 *
 * @param ptr Pointer from a flatcc allocation, or NULL (no-op).
 */
void app_flatcc_free(void *ptr);

/**
 * @brief Resize a flatcc arena block.
 *
 * @param ptr Existing arena block, or NULL for a fresh allocation.
 * @param size New size in bytes.
 * @return Pointer to the resized block, or NULL on failure.
 */
void *app_flatcc_realloc(void *ptr, size_t size);

/**
 * @brief Allocate @p count * @p size zero-initialized bytes from the arena.
 *
 * @param count Number of elements.
 * @param size  Size of each element in bytes.
 * @return Zeroed pointer, or NULL on arena exhaustion.
 */
void *app_flatcc_calloc(size_t count, size_t size);

/**
 * @brief Allocate @p size bytes aligned to @p alignment from the arena.
 *
 * @param alignment Required alignment in bytes (power of two).
 * @param size      Requested size in bytes.
 * @return Aligned pointer, or NULL on arena exhaustion.
 */
void *app_flatcc_aligned_alloc(size_t alignment, size_t size);

/**
 * @brief Release an aligned arena block.
 *
 * @param ptr Pointer from app_flatcc_aligned_alloc(), or NULL (no-op).
 */
void app_flatcc_aligned_free(void *ptr);

#endif /* FLATCC_PORT_H */
