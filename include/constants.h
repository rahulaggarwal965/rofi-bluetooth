#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#include "bluetooth_internal.h"

global_variable const char *controller_props[3] = {
    "Powered",
    "Discoverable",
    "Discovering"
};

global_variable const char *device_props[2] = {
    "Connected",
    "Trusted"
};

global_variable const char *device_strings[3][2] = {
    {"Connect Device", "Disconnect Device"},
    {"Pair Device", "Remove Device"},
    {"Trust Device", "Untrust Device"}
};
global_variable const char *true_false_array[2] = {"False", "True"};

#endif
