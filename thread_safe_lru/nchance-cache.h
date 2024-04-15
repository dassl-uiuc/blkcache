/*
 * Copyright (c) 2014 Tim Starling
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "config.h"
#include "thread_safe_lru/common.h"

#include <atomic>
#include <mutex>
#include <new>
#include <thread>
#include <vector>
#include <memory>
#include <span>
#include <tbb/concurrent_hash_map.h>

namespace tstarling {

/**
 * ThreadSafeLRUNchanceCache is a thread-safe hashtable with a limited size. When
 * it is full, insert() evicts the least recently used item from the cache.
 *
 * The find() operation fills a ConstAccessor object, which is a smart pointer
 * similar to TBB's const_accessor. After eviction, destruction of the value is
 * deferred until all ConstAccessor objects are destroyed.
 *
 * The implementation is generally conservative, relying on the documented
 * behaviour of tbb::concurrent_hash_map. LRU list transactions are protected
 * with a single mutex. Having our own doubly-linked list implementation helps
 * to ensure that list transactions are sufficiently brief, consisting of only
 * a few loads and stores. User code is not executed while the lock is held.
 *
 * The acquisition of the list mutex during find() is non-blocking (try_lock),
 * so under heavy lookup load, the container will not stall, instead some LRU
 * update operations will be omitted.
 *
 * Insert performance was observed to degrade rapidly when there is a heavy
 * concurrent insert/evict load, mostly due to locks in the underlying
 * TBB::CHM. So if that is a possibility for your workload,
 * ThreadSafeScalableCache is recommended instead.
 */
template <class TKey, class TValue, class THash = tbb::tbb_hash_compare<TKey>>
class ThreadSafeLRUNchanceCache {
  /**
   * The LRU list node.
   *
   * We make a copy of the key in the list node, allowing us to find the
   * TBB::CHM element from the list node. TBB::CHM invalidates iterators
   * on most operations, even find(), ruling out more efficient
   * implementations.
   */
  struct ListNode {
    ListNode()
      : m_prev(OutOfListMarker), m_next(nullptr), isSingleton(false), forward_count(-1)
    {}

    ListNode(const TKey& key)
      : m_key(key), m_prev(OutOfListMarker), m_next(nullptr), isSingleton(false), forward_count(-1)
    {}

    TKey m_key;
    KeyValue key_value;
    ListNode* m_prev;
    ListNode* m_next;
    bool isSingleton;
    int forward_count;

    bool isInList() const {
      return m_prev != OutOfListMarker;
    }
  };

  static ListNode* const OutOfListMarker;

  /**
   * The value that we store in the hashtable. The list node is allocated from
   * an internal object_pool. The ListNode* is owned by the list.
   */
  struct HashMapValue {
    HashMapValue()
      : m_listNode(nullptr)
    {}
    
    HashMapValue(const TValue& value, ListNode* node)
      : m_value(value), m_listNode(node)
    {}

    TValue m_value;
    ListNode* m_listNode;
  };

  typedef tbb::concurrent_hash_map<TKey, HashMapValue, THash> HashMap;
  typedef typename HashMap::const_accessor HashMapConstAccessor;
  typedef typename HashMap::accessor HashMapAccessor;
  typedef typename HashMap::value_type HashMapValuePair;
  typedef std::pair<const TKey, TValue> SnapshotValue;

public:
  /**
   * The proxy object for TBB::CHM::const_accessor. Provides direct access to
   * the user's value by dereferencing, thus hiding our implementation
   * details.
   */
  struct ConstAccessor {
    ConstAccessor() {}

    const TValue& operator*() const {
      return *get();
    }

    const TValue* operator->() const {
      return get();
    }

    const TValue* get() const {
      return &m_hashAccessor->second.m_value;
    }

    bool empty() const {
      return m_hashAccessor.empty();
    }

  private:
    friend class ThreadSafeLRUNchanceCache;
    HashMapConstAccessor m_hashAccessor;
  };

  /**
   * Create a container with a given maximum size
   */
  explicit ThreadSafeLRUNchanceCache(size_t maxSize, BlockCacheConfig block_cache_config_, std::shared_ptr<RDMAKeyValueStorage> rdma_key_value_storage_);

  ThreadSafeLRUNchanceCache(const ThreadSafeLRUNchanceCache& other) = delete;
  ThreadSafeLRUNchanceCache& operator=(const ThreadSafeLRUNchanceCache&) = delete;

  ~ThreadSafeLRUNchanceCache() {
    clear();
  }

  /**
   * Find a value by key, and return it by filling the ConstAccessor, which
   * can be default-constructed. Returns true if the element was found, false
   * otherwise. Updates the eviction list, making the element the
   * most-recently used.
   */
  bool find(ConstAccessor& ac, const TKey& key);

  /**
   * Insert a value into the container. Both the key and value will be copied.
   * The new element will put into the eviction list as the most-recently
   * used.
   *
   * If there was already an element in the container with the same key, it
   * will not be updated, and false will be returned. Otherwise, true will be
   * returned.
   */
  void* insert(const TKey& key, const TValue& value);

  bool insert_singleton(const TKey& key, const TValue& value, bool isSingleton, int forward_count);

  bool delete_node(const TKey& key);

  /**
   * Clear the container. NOT THREAD SAFE -- do not use while other threads
   * are accessing the container.
   */
  void clear();

  /**
   * Get a snapshot of the keys in the container by copying them into the
   * supplied vector. This will block inserts and prevent LRU updates while it
   * completes. The keys will be inserted in order from most-recently used to
   * least-recently used.
   */
  void snapshotKeys(std::vector<TKey>& keys);

  /**
   * Get the approximate size of the container. May be slightly too low when
   * insertion is in progress.
   */
  size_t size() const {
    return m_size.load();
  }
  
  void add_callback_on_eviction(EvictionCallback<std::string, TValue> callback) { eviction_callbacks.emplace_back(callback); }

private:
  /**
   * Unlink a node from the list. The caller must lock the list mutex while
   * this is called.
   */
  void delink(ListNode* node);

  /**
   * Add a new node to the list in the most-recently used position. The caller
   * must lock the list mutex while this is called.
   */
  void pushFront(ListNode* node);

  /**
   * Evict the least-recently used item from the container. This function does
   * its own locking.
   */ 
  void* evict();
  
  void evict_for_singleton();

  ListNode* get_oldest_non_singleton_node();
  
  ListNode* get_oldest_singleton_with_lowest_forward_count_node();

  /**
   * The maximum number of elements in the container.
   */
  size_t m_maxSize;  

  /**
   * This atomic variable is used to signal to all threads whether or not
   * eviction should be done on insert. It is approximately equal to the
   * number of elements in the container.
   */
  std::atomic<size_t> m_size;

  /** 
   * The underlying TBB hash map.
   */
  HashMap m_map;

  /**
   * The linked list. The "head" is the most-recently used node, and the
   * "tail" is the least-recently used node. The list mutex must be held
   * during both read and write.
   */
  ListNode m_head;
  ListNode m_tail;
  typedef std::mutex ListMutex;
  ListMutex m_listMutex;

  // RMDA related
  BlockCacheConfig block_cache_config;
  std::shared_ptr<RDMAKeyValueStorage> rdma_key_value_storage;
  std::vector<EvictionCallback<std::string, TValue>> eviction_callbacks;
};

template <class TKey, class TValue, class THash>
typename ThreadSafeLRUNchanceCache<TKey, TValue, THash>::ListNode* const
ThreadSafeLRUNchanceCache<TKey, TValue, THash>::OutOfListMarker = (ListNode*)-1;

template <class TKey, class TValue, class THash>
ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
ThreadSafeLRUNchanceCache(size_t maxSize, BlockCacheConfig block_cache_config_, std::shared_ptr<RDMAKeyValueStorage> rdma_key_value_storage_)
  : m_maxSize(maxSize), block_cache_config(block_cache_config_), rdma_key_value_storage(rdma_key_value_storage_), m_size(0),
  m_map(std::thread::hardware_concurrency() * 4) // it will automatically grow
{
  m_head.m_prev = nullptr;
  m_head.m_next = &m_tail;
  m_tail.m_prev = &m_head;
}

template <class TKey, class TValue, class THash>
bool ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
find(ConstAccessor& ac, const TKey& key) {
  HashMapConstAccessor& hashAccessor = ac.m_hashAccessor;
  if (!m_map.find(hashAccessor, key)) {
    return false;
  }

  // Acquire the lock, but don't block if it is already held
  std::unique_lock<ListMutex> lock(m_listMutex, std::try_to_lock);
  if (lock) {
    ListNode* node = hashAccessor->second.m_listNode;
    // The list node may be out of the list if it is in the process of being
    // inserted or evicted. Doing this check allows us to lock the list for
    // shorter periods of time.
    if (node->isInList()) {
      delink(node);
      pushFront(node);
    }
    lock.unlock();
  }
  return true;
}

template <class TKey, class TValue, class THash>
void* ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
insert(const TKey& key, const TValue& value) {
  // Insert into the CHM
  ListNode* node = nullptr;
  ListNode* tmp = nullptr;
  void* evict_node = nullptr;

  node = new ListNode(key);
  HashMapAccessor hashAccessor;
  HashMapValuePair hashMapValue(key, HashMapValue(value, node));
  if (!m_map.insert(hashAccessor, hashMapValue)) {
    // tmp = hashAccessor->second.m_listNode;
    // ListNode* dnode = hashAccessor->second.m_listNode;
    // if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
    // {
    //   rdma_key_value_storage->deallocate(dnode->key_value);
    // }
    // delete dnode;
    // hashAccessor->second = HashMapValue(value, node);
    // if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
    // {
    //   rdma_key_value_storage->deallocate(node->key_value);
    // }
    std::unique_lock<ListMutex> lock(m_listMutex, std::try_to_lock);
    if (lock) {
      ListNode* orig_node = hashAccessor->second.m_listNode;
      if (orig_node->isInList()) {
        delink(orig_node);
        pushFront(orig_node);
      }
      lock.unlock();
    }

    delete node;
    return evict_node;
  }
  hashAccessor.release();
  // if(tmp->isInList()){
  //   delink(tmp);
  //   delete(tmp);
  // }

  if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
  {
    KeyValue key_value = rdma_key_value_storage->allocate(std::stoi(key.c_str()));
    std::copy(std::begin(value), std::end(value), std::begin(key_value.value));
    node->key_value = key_value;
    node->isSingleton = false;
    node->forward_count = 2;
  }

  // Evict if necessary, now that we know the hashmap insertion was successful.
  size_t size = m_size.load();
  bool evictionDone = false;
  if (size >= m_maxSize) {
    // The container is at (or over) capacity, so eviction needs to be done.
    // Do not decrement m_size, since that would cause other threads to
    // inappropriately omit eviction during their own inserts.
    evict_node = evict();
    evictionDone = true;
  }

  // Note that we have to update the LRU list before we increment m_size, so
  // that other threads don't attempt to evict list items before they even
  // exist.
  std::unique_lock<ListMutex> lock(m_listMutex);
  pushFront(node);
  lock.unlock();
  if (!evictionDone) {
    size = m_size++;
  }
  if (size > m_maxSize) {
    // It is possible for the size to temporarily exceed the maximum if there is
    // a heavy insert() load, once only as the cache fills. In this situation,
    // we have to be careful not to have every thread simultaneously attempt to
    // evict the extra entries, since we could end up underfilled. Instead we do
    // a compare-and-exchange to acquire an exclusive right to reduce the size
    // to a particular value.
    //
    // We could continue to evict in a loop, but if there are a lot of threads
    // here at the same time, that could lead to spinning. So we will just evict
    // one extra element per insert() until the overfill is rectified.
    if (m_size.compare_exchange_strong(size, size - 1)) {
      evict_node = evict();
    }
  }
  return evict_node; 
}

template <class TKey, class TValue, class THash>
bool ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
insert_singleton(const TKey& key, const TValue& value, bool isSingleton, int forward_count) {
  // Insert into the CHM
  ListNode* node = nullptr;
  node = new ListNode(key);

  HashMapAccessor hashAccessor;
  HashMapValuePair hashMapValue(key, HashMapValue(value, node));
  if (!m_map.insert(hashAccessor, hashMapValue)) {
    // if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
    // {
    //   rdma_key_value_storage->deallocate(node->key_value);
    // }

    std::unique_lock<ListMutex> lock(m_listMutex, std::try_to_lock);
    if (lock) {
      ListNode* orig_node = hashAccessor->second.m_listNode;
      if (orig_node->isInList()) {
        delink(orig_node);
        pushFront(orig_node);
      }
      lock.unlock();
    }

    delete node;
    return false;
  }
  hashAccessor.release(); 

  if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
  {
    auto key_index = std::stoi(key.c_str());
    KeyValue key_value = rdma_key_value_storage->allocate(key_index);
    std::copy(std::begin(value), std::end(value), std::begin(key_value.value));
    node->key_value = key_value;

    RDMACacheIndex* ci = rdma_key_value_storage->get_cache_index_buffer();
    if(isSingleton){
      info("Singleton key recieved : {} with forward count : {}", key.c_str(), forward_count);
      auto new_forward_count = std::min(forward_count - 1, 0);
      ci[key_index].isSingleton = true;
      ci[key_index].forward_count = new_forward_count;
      node->isSingleton = true;
      node->forward_count = new_forward_count;
    } else {
      ci[key_index].isSingleton = true;
      ci[key_index].forward_count = 2;
      node->isSingleton = true;
      node->forward_count = 2;
    }
    node->key_value = key_value;
  }
  

  // Evict if necessary, now that we know the hashmap insertion was successful.
  size_t size = m_size.load();
  bool evictionDone = false;
  if (size >= m_maxSize) {
    // The container is at (or over) capacity, so eviction needs to be done.
    // Do not decrement m_size, since that would cause other threads to
    // inappropriately omit eviction during their own inserts.
    evict_for_singleton();
    evictionDone = true;
  }

  // Note that we have to update the LRU list before we increment m_size, so
  // that other threads don't attempt to evict list items before they even
  // exist.
  std::unique_lock<ListMutex> lock(m_listMutex);
  pushFront(node);
  lock.unlock();
  if (!evictionDone) {
    size = m_size++;
  }
  if (size > m_maxSize) {
    // It is possible for the size to temporarily exceed the maximum if there is
    // a heavy insert() load, once only as the cache fills. In this situation,
    // we have to be careful not to have every thread simultaneously attempt to
    // evict the extra entries, since we could end up underfilled. Instead we do
    // a compare-and-exchange to acquire an exclusive right to reduce the size
    // to a particular value.
    //
    // We could continue to evict in a loop, but if there are a lot of threads
    // here at the same time, that could lead to spinning. So we will just evict
    // one extra element per insert() until the overfill is rectified.
    if (m_size.compare_exchange_strong(size, size - 1)) {
      evict_for_singleton();
    }
  }
  return true;
}

template <class TKey, class TValue, class THash>
void ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
clear() {
  m_map.clear();
  ListNode* node = m_head.m_next;
  ListNode* next;
  while (node != &m_tail) {
    next = node->m_next;
    if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
    {
      rdma_key_value_storage->deallocate(node->key_value);
    }
    delete node;
    node = next;
  }
  m_head.m_next = &m_tail;
  m_tail.m_prev = &m_head;
  m_size = 0;
}

template <class TKey, class TValue, class THash>
void ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
snapshotKeys(std::vector<TKey>& keys) {
  keys.reserve(keys.size() + m_size.load());
  std::lock_guard<ListMutex> lock(m_listMutex);
  for (ListNode* node = m_head.m_next; node != &m_tail; node = node->m_next) {
    keys.push_back(node->m_key);
  }
}

template <class TKey, class TValue, class THash>
inline void ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
delink(ListNode* node) {
  ListNode* prev = node->m_prev;
  ListNode* next = node->m_next;
  prev->m_next = next;
  next->m_prev = prev;
  node->m_prev = OutOfListMarker;
}

template <class TKey, class TValue, class THash>
inline void ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
pushFront(ListNode* node) {
  ListNode* oldRealHead = m_head.m_next;
  node->m_prev = &m_head;
  node->m_next = oldRealHead;
  oldRealHead->m_prev = node;
  m_head.m_next = node;
}

template <class TKey, class TValue, class THash>
void* ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
evict() {
  info("[Evicting] {}", eviction_callbacks.size());
  std::unique_lock<ListMutex> lock(m_listMutex);
  ListNode* moribund = m_tail.m_prev;
  ListNode nodeCopy = *moribund;
  uint64_t replicaCount = rdma_key_value_storage->get_num_cache_index_buffers_containing_key(*nodeCopy.key_value.key);
  if (moribund == &m_head) {
    // List is empty, can't evict
    info("List is empty, can't evict");
    return nullptr;
  }
  delink(moribund);
  lock.unlock();

  HashMapAccessor hashAccessor;
  if (!m_map.find(hashAccessor, moribund->m_key)) {
    // Presumably unreachable
    info("[NOT Found]");
    return nullptr;
  }
  EvictionCallbackData<std::string, TValue>* data = nullptr;
  data = new EvictionCallbackData<std::string, TValue>();
  data->key = data->key = std::to_string(*nodeCopy.key_value.key);
  data->value = std::string(reinterpret_cast<const char*>(nodeCopy.key_value.value.data()), nodeCopy.key_value.value.size());
  data->singleton = nodeCopy.isSingleton;
  data->forward_count = nodeCopy.forward_count;
  data->replica_count = replicaCount;
  
  for (const auto& callback : eviction_callbacks)
  {
    callback(*data);
  }

  m_map.erase(hashAccessor);
  if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
  {
    rdma_key_value_storage->deallocate(moribund->key_value);
  }
  
  delete moribund;

  if (nodeCopy.isSingleton && nodeCopy.forward_count > 0) {
    info("[Singleton to forward]");
    return static_cast<void*>(data);
  } else {
    if (replicaCount > 1) {
      info("[Singleton to forward]");
      return static_cast<void*>(data);
    }
  }
  info("[NOT Returned] data->key : {} data->value : {} data->singleton : {} data->forward_count : {} data->replica_count : {}", data->key, data->value, data->singleton, data->forward_count, data->replica_count);
  return nullptr;
}

template <class TKey, class TValue, class THash>
void ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
evict_for_singleton() {
  std::unique_lock<ListMutex> lock(m_listMutex);
  ListNode* nodeToRemove = nullptr;
  nodeToRemove = get_oldest_non_singleton_node();
  if(nodeToRemove == nullptr){
    nodeToRemove = get_oldest_singleton_with_lowest_forward_count_node();
  }
  if(nodeToRemove == nullptr){
    nodeToRemove = m_tail.m_prev;
  }
  if (nodeToRemove == &m_head) {
    // List is empty, can't evict
    info("List is empty, can't evict");
    return;
  }
  delink(nodeToRemove);
  lock.unlock();

  HashMapAccessor hashAccessor;
  if (!m_map.find(hashAccessor, nodeToRemove->m_key)) {
    // Presumably unreachable
    info("[NOT Found]");
    return;
  }

  for (const auto& callback : eviction_callbacks)
  {
    callback({nodeToRemove->m_key.c_str(), hashAccessor->second.m_value});
  }

  m_map.erase(hashAccessor);
  if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
  {
    rdma_key_value_storage->deallocate(nodeToRemove->key_value);
  }
  delete nodeToRemove;
}
/*
  * Get the oldest node with duplicates cached in the system
*/
template <class TKey, class TValue, class THash>
typename ThreadSafeLRUNchanceCache<TKey, TValue, THash>::ListNode*
ThreadSafeLRUNchanceCache<TKey, TValue, THash>::get_oldest_non_singleton_node() {
  uint64_t replica = 0;
  for (ListNode* node = m_tail.m_prev; node != &m_head; node = node->m_prev) {
      if (!node->isSingleton) {
        replica = rdma_key_value_storage->get_num_cache_index_buffers_containing_key(*node->key_value.key);
        if (replica > 1) {
          return node;
        }
      }
    }
    return nullptr;
  }

/*
  * Get the oldest singleton with lowest forward count node
*/
template <class TKey, class TValue, class THash>
typename ThreadSafeLRUNchanceCache<TKey, TValue, THash>::ListNode*
ThreadSafeLRUNchanceCache<TKey, TValue, THash>::get_oldest_singleton_with_lowest_forward_count_node() {
  ListNode* oldestSingletonNode = nullptr;
  int lowestForwardCount = 2;

  for (ListNode* node = m_tail.m_prev; node != &m_head; node = node->m_prev) {
    if (node->isSingleton) {
      int forwardCount = node->forward_count;
      if (forwardCount <= lowestForwardCount) {
        oldestSingletonNode = node;
        lowestForwardCount = forwardCount--;
        if(lowestForwardCount < 0){
          break;
        }
      }
    }
  }

  return oldestSingletonNode;
}

template <class TKey, class TValue, class THash>
bool ThreadSafeLRUNchanceCache<TKey, TValue, THash>::
delete_node(const TKey& key) {
  HashMapAccessor hashAccessor;
  if (!m_map.find(hashAccessor, key)) {
    return false;
  }
  ListNode* node = hashAccessor->second.m_listNode;
  if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
  {
    rdma_key_value_storage->deallocate(node->key_value);
  }
  delete node;
  m_map.erase(hashAccessor);
  return true;
}

} // namespace tstarling