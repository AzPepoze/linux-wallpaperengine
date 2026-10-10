#ifndef PROPERTY_LISTING_H
#define PROPERTY_LISTING_H

#include <stdio.h>

#include "wallpaper/user_properties.h"

// Writes the properties in the layout of upstream --list-properties, sorted by key.
void printProperties(FILE* out, const UserProperties& properties);

#endif  // PROPERTY_LISTING_H
