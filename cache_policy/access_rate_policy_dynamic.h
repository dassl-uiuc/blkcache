#pragma once

#include "cache_policy/cache_policy.h"

#include <cassert>
#include <list>
#include <unordered_map>
#include <span>
#include <atomic>

#include "concurrentqueue.h"
#include "parallel_hashmap/phmap.h"
#include "thread_safe_lru/access_rate-cache.h"
#include <tbb/concurrent_hash_map.h>

typedef tstarling::ThreadSafeStringKey String;
typedef String::HashCompare HashCompare;

using RDMAFriendlyString = tstarling::ThreadSafeStringKey;

template <typename KeyType, typename ValueType>
class ThreadSafeLRUAccessRateDynamicCache : public CachePolicy<KeyType, ValueType> {
public:
  using Cache = tstarling::ThreadSafeLRUAccessRateCache<String, std::string, HashCompare>;
  
  ThreadSafeLRUAccessRateDynamicCache(BlockCacheConfig block_cache_config_,
                     std::shared_ptr<BlockDB> block_db, uint64_t cache_size, uint64_t access_rate_ = 1, uint64_t access_per_itr_ = 1000)
      : block_cache_config(block_cache_config_), CachePolicy<KeyType, ValueType>(block_cache_config, block_db,
                                        cache_size) {
    if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
    {
      rdma_key_value_storage = std::make_shared<RDMAKeyValueStorage>(block_cache_config);
    }
    secm = std::make_shared<Cache>(cache_size, block_cache_config, rdma_key_value_storage);
    access_rate = access_rate_;
    access_per_itr = access_per_itr_;
    total_accesses = 0;
    block_db_num_entries = block_cache_config.db.block_db.num_entries;
    cache_size = block_cache_config.cache.lru.cache_size;
    water_mark_local = 0.0;
    water_mark_remote = float(cache_size / block_db_num_entries);
    water_mark_disk = 100.0;
    info("access_rate: {} and access_per_itr: {} and cache_size: {}", access_rate, access_per_itr, cache_size);
    info("water_mark_local: {} and water_mark_remote: {}", water_mark_local, water_mark_remote);
    info("block_db_num_entries: {}", block_db_num_entries);
    // info("Access rate: {} and access per itr: {}", access_rate, access_per_itr);
    info("This is Dynamic access_rate: {}", access_rate);
  }

  void put(const KeyType &key, const ValueType &val,
           bool owning = false) override {
    String skey(key.c_str(), key.length());
    secm->insert(skey, val);
    for (const auto& callback : this->write_callbacks) {
      callback(key, val);
    }
  }

  bool put_access_rate_match(const KeyType &key, const ValueType &val,
           bool owning = false) override {
    update_frequency(key);
    if(get_frequency(key) >= access_rate){
      // info("Access rate match for key: {}", key);
      put(key, val, owning);
      return true;
    }
    return false;
  }

  ValueType get(const KeyType &key) override {
    String skey(key.c_str(), key.length());
    uint64_t current_accesses = total_accesses.fetch_add(1, std::memory_order_relaxed) + 1;
    total_accesses++;
    update_frequency(key);
    
    Cache::ConstAccessor ac;
    ValueType ret;
    if (secm->find(ac, skey)) {
      ret = *ac;
    }
    for (const auto& callback : this->read_callbacks) {
      callback(key);
    }
    return ret;
  }

  bool exist(const KeyType &key) override {
    String skey(key.c_str(), key.length());
    Cache::ConstAccessor ac;
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

  RDMAKeyValueStorage* get_rdma_key_value_storage() override { return rdma_key_value_storage.get(); }

  using ConstFrequencyAccessor = tbb::concurrent_hash_map<KeyType, uint64_t>::const_accessor;
  using FrequencyAccessor = tbb::concurrent_hash_map<KeyType, uint64_t>::accessor;

  uint64_t get_frequency(const KeyType& key) {
    ConstFrequencyAccessor acc;
    if (key_freq.find(acc, key)) {
      return acc->second;
    }
    return 0;
  }

  void update_frequency(const KeyType& key) {
    wait_on_isclearing();
    bool found = false;
    {
      FrequencyAccessor acc;
      if (key_freq.find(acc, key)) {
        acc->second++;
        found = true;
      }
    }
    if (!found)
    {
      FrequencyAccessor acc;
      key_freq.insert(acc, key);
      acc->second = 1;
    }
  }

  void clear_frequency() {
    is_clearing.store(true);
    std::vector<KeyType> keys;

    // Iterate over the map to collect keys
    for (auto it = key_freq.begin(); it != key_freq.end(); ++it) {
        keys.push_back(it->first);
        shadow_freq.push_back(std::make_pair(it->first, it->second));
        FrequencyAccessor acc;
        keys_from_past.insert(acc, it->first);
        acc->second = it->second;
    }

    // Remove each key collected
    for (auto& key : keys) {
        key_freq.erase(key);
    }
    is_clearing.store(false);
    // return shadow_freq;
  }

  void wait_on_isclearing() {
    while (is_clearing.load()) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  std::vector<std::pair<KeyType, uint64_t>> get_key_freq_map() {
    return shadow_freq;
  }
  
  std::pair<uint64_t, uint64_t> get_access_rate_and_access_per_itr() {
    return std::make_pair(access_rate, access_per_itr);
  }

  bool set_access_rate(uint64_t access_rate_) {
    access_rate = access_rate_;
    return true;
  }

  bool set_access_per_itr(uint64_t access_per_itr_) {
    access_per_itr = access_per_itr_;
    return true;
  }

  std::tuple<uint64_t, uint64_t, uint64_t> get_water_marks() {
    return std::make_tuple(water_mark_local, water_mark_remote, water_mark_disk);
  }

  void set_water_marks(uint64_t water_mark_local_, uint64_t water_mark_remote_) {
    water_mark_local = water_mark_local_;
    water_mark_remote = water_mark_remote_;
  }

  uint64_t get_block_db_num_entries() {
    return block_db_num_entries;
  }

  uint64_t get_cache_size() {
    return cache_size;
  }

  void set_keys_under_l(const std::vector<KeyType>& keys) {
    for (const auto& key : keys) {
      keys_from_past.insert(std::make_pair(key, 0));
    }
  }
  
  void print_shadow_freq_to_a_file(){
    std::ofstream file;
    file.open("shadow_freq.txt");
    for (auto& key_freq : shadow_freq){
      file << key_freq.first << " " << key_freq.second << std::endl;
    }
    file.close();  
  }
  
  void print_key_freq_to_a_file(){
    std::ofstream file;
    file.open("key_freq.txt");
    for (auto& key_freq : key_freq){
      file << key_freq.first << " " << key_freq.second << std::endl;
    }
    file.close();
  }

  void print_keys_from_past_to_a_file(){
    std::ofstream file;
    file.open("keys_from_past.txt");
    for (auto& key_freq : keys_from_past){
      file << key_freq.first << " " << key_freq.second << std::endl;
    }
    file.close();
  }

  void print_cache_stats(){
    std::ofstream file;
    file.open("cache_stats.txt");
    file << "Cache size: " << cache_size << std::endl;
    file << "Access rate: " << access_rate << std::endl;
    file << "Access per itr: " << access_per_itr << std::endl;
    file << "Block db num entries: " << block_db_num_entries << std::endl;
    file << "Water mark local: " << water_mark_local << std::endl;
    file << "Water mark remote: " << water_mark_remote << std::endl;
    file << "Water mark disk: " << water_mark_disk << std::endl;
    file.close();
  }

  void print_all_stats(){
    print_shadow_freq_to_a_file();
    print_key_freq_to_a_file();
    print_keys_from_past_to_a_file();
    print_cache_stats();
  }

  bool is_ready(){
    if(total_accesses.load() > 400000){
      return true;
    }
  }

  uint64_t get_total_accesses() {
    return total_accesses.load();
  }

  uint64_t get_total_cache_duplication() {
    return Total_cache_duplication.load();
  }

  void set_total_cache_duplication(uint64_t total_cache_duplication_) {
    Total_cache_duplication.store(total_cache_duplication_);
  }

  void update_total_cache_duplication(uint64_t total_cache_duplication_) {
    Total_cache_duplication.fetch_add(total_cache_duplication_, std::memory_order_relaxed);
  }

  uint64_t get_duplications_allowed() {
    return duplications_allowed;
  }

  void set_duplications_allowed(uint64_t duplications_allowed_) {
    duplications_allowed = duplications_allowed_;
  }



private:
  BlockCacheConfig block_cache_config;
  std::shared_ptr<Cache> secm = nullptr;
  std::shared_ptr<RDMAKeyValueStorage> rdma_key_value_storage = nullptr;
  std::atomic<uint64_t> total_accesses;
  std::atomic<uint64_t> Total_cache_duplication;
  uint64_t duplications_allowed;
  std::atomic<bool> is_clearing;

  uint64_t access_rate;
  uint64_t access_per_itr;

  uint64_t water_mark_local;
  uint64_t water_mark_remote;
  uint64_t water_mark_disk;
  
  uint64_t block_db_num_entries;
  uint64_t cache_size;
  
  tbb::concurrent_hash_map<KeyType, uint64_t> key_freq;
  tbb::concurrent_hash_map<KeyType, uint64_t> keys_from_past;
  std::vector<std::pair<KeyType, uint64_t>> shadow_freq;
  std::mutex key_freq_mutex;
  std::mutex clear_freq_lock;
};