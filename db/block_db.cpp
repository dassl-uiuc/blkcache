#pragma once

#include "db/block_db.h"

#include <assert.h>
#include <list>
#include <unordered_map>

uint64_t writes_blocked_ns = 0;
