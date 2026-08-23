#ifndef SOKOL_SYNC_H
#define SOKOL_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

// Block CPU until all pending GPU command buffers and operations have fully completed.
void lwe_vk_wait_idle(void);

#ifdef __cplusplus
}
#endif

#endif  // SOKOL_SYNC_H
