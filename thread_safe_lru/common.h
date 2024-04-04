#pragma once

template<typename KeyType, typename ValueType>
struct EvictionCallbackData
{
    KeyType key;
    ValueType value;
    bool singleton;
    uint64_t forward_count;
};

template<typename KeyType, typename ValueType>
using EvictionCallback = std::function<void(EvictionCallbackData<KeyType, ValueType>)>;
