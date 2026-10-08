#ifndef OUTPUT_POINTER_H
#define OUTPUT_POINTER_H

// A pointer position as fractions of one output: (0, 0) is its top-left corner.
struct OutputPointer {
    float x = 0.5f;
    float y = 0.5f;
    bool inside = false;
};

#endif  // OUTPUT_POINTER_H
