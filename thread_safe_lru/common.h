#pragma once

template<typename KeyType, typename ValueType>
struct EvictionCallbackData
{
    KeyType key;
    uint64_t keyi;
    ValueType value;
    bool singleton;
    uint64_t forward_count;
    int replica_count;
    bool dirty;
};

template<typename KeyType, typename ValueType>
using EvictionCallback = std::function<void(const EvictionCallbackData<KeyType, ValueType>&)>;
