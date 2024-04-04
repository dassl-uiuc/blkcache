#pragma once

template<typename KeyType, typename ValueType>
struct EvictionCallbackData
{
    const KeyType& k;
    const ValueType& v;
    bool singleton;
    uint64_t forwarding_count;
};

template<typename KeyType, typename ValueType>
using EvictionCallback = std::function<void(EvictionCallbackData<KeyType, ValueType>)>;
