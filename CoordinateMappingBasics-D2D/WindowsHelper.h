#pragma once

#include <wrl/client.h>

#define V(__hr__) \
    if (FAILED(__hr__)) { return; }

#define V_RET(__hr__) \
    if (FAILED(__hr__)) { return __hr__; }

#define V_CHECK(__bool__) \
    if (!(__bool__)) { return; }

#define V_CHECK_HR(__bool__) \
    if (!(__bool__)) { return E_FAIL; }