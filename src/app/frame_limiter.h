#ifndef FRAME_LIMITER_H
#define FRAME_LIMITER_H

// Sleeps until 1/fps seconds have passed since the previous call; fps <= 0 disables limiting.
void limitFrameRate(int fps);

#endif  // FRAME_LIMITER_H
