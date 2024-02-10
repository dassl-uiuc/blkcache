#pragma once

#include "cache_policy/cache_policy.h"

#include <cassert>
#include <list>
#include <unordered_map>

#include "concurrentqueue.h"
#include "parallel_hashmap/phmap.h"
#include "thread-safe-lru/scalable-cache.h"

typedef tstarling::ThreadSafeStringKey String;
typedef String::HashCompare HashCompare;
typedef tstarling::ThreadSafeScalableCache<String, std::string, HashCompare> ScalableCache;

template <typename KeyType, typename ValueType>
class ThreadSafeLRUCache : public CachePolicy<KeyType, ValueType> {
public:
  ThreadSafeLRUCache(BlockCacheConfig block_cache_config,
                     std::shared_ptr<BlockDB> block_db, uint64_t cache_size)
      : CachePolicy<KeyType, ValueType>(block_cache_config, block_db,
                                        cache_size) {
    secm = std::unique_ptr<ScalableCache>(new ScalableCache(cache_size));
  }

  void put(const KeyType &key, const ValueType &val,
           bool owning = false) override {
    String skey(key.c_str(), key.length());
    secm->insert(skey, val);
  }

  ValueType get(const KeyType &key) override {
    String skey(key.c_str(), key.length());
    ScalableCache::ConstAccessor ac;
    ValueType ret;
    if (secm->find(ac, skey)) {
      ret = *ac;
    }

    return ret;
  }

  bool exist(const KeyType &key) override {
    String skey(key.c_str(), key.length());
    ScalableCache::ConstAccessor ac;
    if (secm->find(ac, skey)) {
      return true;
    }
    return false;
  }

  void remove(const KeyType &key) override { panic("Unsupported"); }

  void dump(std::ostream &os) override {
    std::vector<String> skeys;
    secm->snapshotKeys(skeys);
    for (auto &skey : skeys) {
      os << skey.data() << "\n";
    }
  }

private:
  std::unique_ptr<ScalableCache> secm = nullptr;
};


// constexpr size_t ParallelFlatHashMapSubmapSize = 8; // 2**N submaps

// template <typename T, typename T2>
// using ThreadSafeMap = phmap::parallel_flat_hash_map<
//     T, T2, phmap::priv::hash_default_hash<T>, phmap::priv::hash_default_eq<T>,
//     phmap::priv::Allocator<phmap::priv::Pair<const T, T2>>,
//     ParallelFlatHashMapSubmapSize, std::mutex>;

// template <typename KeyType, typename ValueType>
// class ThreadSafeLRUCache : public CachePolicy<KeyType, ValueType> {
// public:
//   struct ListNode {
//     ListNode() : prev(OutOfListMarker), next(nullptr) {}

//     ListNode(const KeyType &key)
//         : prev(OutOfListMarker), next(nullptr), k(key) {}

//     bool IsInList() const { return prev != OutOfListMarker; }

//     KeyType k;
//     ListNode *prev;
//     ListNode *next;
//     int orig_k;
//   };
//   inline static ListNode *const OutOfListMarker = (ListNode *)-1;

//   struct NodeValuePair {
//     NodeValuePair(ListNode *n, const ValueType &v) : node(n), value(v) {}

//     ListNode *node;
//     ValueType value;
//   };

//   ThreadSafeLRUCache(BlockCacheConfig block_cache_config,
//                      std::shared_ptr<BlockDB> block_db, uint64_t cache_size)
//       : CachePolicy<KeyType, ValueType>(block_cache_config, block_db,
//                                         cache_size) {
//     head.prev = nullptr;
//     head.next = &tail;
//     tail.prev = &head;

//     current_size = 0;
//   }

//   void put(const KeyType &key, const ValueType &val,
//            bool owning = false) override {
//     std::lock_guard<std::mutex> lock(m);
//     ListNode *n = nullptr;
//     bool inserted = false;
//     item_map.lazy_emplace_l(
//         key,
//         [&](auto &v) {
//           n = v.second.node;
//           erase(n);
//           v.second.value = val;
//         },
//         [&](const auto &ctor) {
//           inserted = true;
//           n = new ListNode(key);
//           NodeValuePair nvp(n, val);
//           ctor(key, nvp);
//         });
//     n->orig_k = std::stoi(key);

//     auto size = current_size.load(std::memory_order_relaxed);
//     bool eviction_performed = false;
//     if (size >= this->cache_size) {
//       evict();
//       eviction_performed = true;
//     }

//     push_front(n);

//     if (!eviction_performed) {
//       size = current_size.fetch_add(1, std::memory_order_relaxed);
//     }

//     // if (size > this->cache_size) {
//     //   if (current_size.compare_exchange_strong(size, size - 1)) {
//     //     evict();
//     //   }
//     // }

//     // auto it = item_map.find(key);
//     // if (it != item_map.end()) {
//     //   item_list.erase(it->second);
//     //   item_map.erase(it);
//     // }
//     // item_list.push_front(make_pair(key, val));
//     // item_map.insert(make_pair(key, item_list.begin()));
//     // clean();
//   }

//   ValueType get(const KeyType &key) override {
//     // assert(exist(key));
//     // auto it = item_map.find(key);
//     // item_list.splice(item_list.begin(), item_list, it->second);
//     // return it->second->second;

//     ValueType ret{};
//     ListNode *n = nullptr;
//     item_map.modify_if(key, [&](auto &v) {
//       n = v.second.node;
//       ret = v.second.value;
//     });
//     {
//       // Try to push the node to the front of the list
//       std::unique_lock<std::mutex> lock(m, std::try_to_lock);
//       if (lock)
//       {
//         if (n->IsInList())
//         {
//           erase(n);
//           push_front(n);
//         }
//         lock.unlock();
//       }
//     }
//     return ret;
//   }

//   bool exist(const KeyType &key) override {
//     // if (key == "4201")
//     // {
//     //   dump(std::cout);
//     //   info("Checking for key {}", key);
//     //   info("CONTAINS {}", item_map.contains(key));
//     //   bool acutally_contains = false;
//     //   item_map.if_contains(key, [&](auto &v) {
//     //     acutally_contains = true;
//     //   });
//     //   info("ACTUALLY CONTAINS {}", acutally_contains);
//     // }
//     return item_map.contains(key);
//     // return (item_map.count(key) > 0);
//   }

//   void remove(const KeyType &key) override { panic("Unsupported"); }

//   void dump(std::ostream &os) override {
//     auto linked_list_size = 0;
//     for (auto* h = head.next; h != &tail; h = h->next) {
//       os << h->k << "\n";
//       linked_list_size++;
//     }
//     os << "Item map size: " << item_map.size() << "\n";
//     os << "Linked list size: " << linked_list_size << "\n";
//     os << "Current size: " << current_size.load() << "\n";
//     os << "Cache size: " << this->cache_size << "\n";

//     // for (const auto &[k, v] : item_list) {
//     //   os << k << "\n";
//     // }
//   }

//   void push_front(ListNode *node) {
//     ListNode *oldRealHead = head.next;
//     node->prev = &head;
//     node->next = oldRealHead;
//     oldRealHead->prev = node;
//     head.next = node;
//   }

//   void erase(ListNode *node) {
//     ListNode *prev = node->prev;
//     ListNode *next = node->next;
//     prev->next = next;
//     next->prev = prev;
//     node->prev = OutOfListMarker;
//     node->next = nullptr;
//   }

//   void evict() {
//     ListNode *last_node = nullptr;
//     {
//       // std::lock_guard<std::mutex> lock(m);
//       last_node = tail.prev;
//       if (last_node == &head) {
//         return;
//       }
//       // info("Erased {}", last_node->k);

//     item_map.erase_if(last_node->k, [&](auto &v) {
//       // delete v.second.node;
//       return last_node == v.second.node;
//     });
//       erase(last_node);
//     delete last_node;
//     }
//   }

// private:
//   void clean(void){
//       // while (item_map.size() > this->cache_size) {
//       //   auto last_it = item_list.end();
//       //   last_it--;
//       //   item_map.erase(last_it->first);
//       //   item_list.pop_back();
//       // }
//   };

// private:
//   // std::list<std::pair<KeyType, ValueType>> item_list;
//   // std::unordered_map<KeyType, decltype(item_list.begin())> item_map;
//   ThreadSafeMap<KeyType, NodeValuePair> item_map;
//   // moodycamel::ConcurrentQueue<std::pair<KeyType, ValueType>> item_list;
//   ListNode head;
//   ListNode tail;
//   std::mutex m;
//   std::atomic<uint64_t> current_size;
// };
