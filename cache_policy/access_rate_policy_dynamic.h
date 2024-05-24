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
                     std::shared_ptr<BlockDB> block_db, uint64_t cache_size_, uint64_t access_rate_ = 1, uint64_t access_per_itr_ = 1000)
      : block_cache_config(block_cache_config_), CachePolicy<KeyType, ValueType>(block_cache_config, block_db,
                                        cache_size) {
    if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
    {
      rdma_key_value_storage = std::make_shared<RDMAKeyValueStorage>(block_cache_config);
    }
    secm = std::make_shared<Cache>(cache_size_, block_cache_config, rdma_key_value_storage);
    secm->setDecrementCallback([this]() { this->decrement_total_cache_duplication(); });
    access_rate = access_rate_;
    access_per_itr = access_per_itr_;
    total_accesses = 0;
    block_db_num_entries = block_cache_config.db.block_db.num_entries;
    // cache_size = block_cache_config.cache.lru.cache_size;
    cache_size = cache_size_;
    water_mark_local = 0;
    if(cache_size_ * 3 <= block_db_num_entries)
      water_mark_remote = cache_size_ * 3;
    else
      water_mark_remote = block_db_num_entries;
    water_mark_disk = block_db_num_entries;
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

  bool check_if_key_access_rate_match_the_past(const KeyType &key) {
    ConstFrequencyAccessor acc;
    if (keys_from_past.find(acc, key)) {
      if(acc->second >= access_rate){
        return true;
      }
    }
    return false;
  }

  bool put_access_rate_match(const KeyType &key, const ValueType &val,
           bool owning = false) override {
    update_frequency(key);
    bool should_put = true;
    if(check_key_duplication(key)){
      if(current_duplicates.load() >= duplications_allowed.load()){
        should_put = false;
      }
    }
    if(should_put && get_frequency(key) >= access_rate){
      // info("Access rate match for key: {}", key);
      put(key, val, owning);
      if(check_key_duplication(key)){
        increment_total_cache_duplication();
      }
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
    shadow_freq.clear();

    // Iterate over the map to collect keys
    for (auto it = key_freq.begin(); it != key_freq.end(); ++it) {
        keys.push_back(it->first);
        shadow_freq.push_back(std::make_pair(it->first, it->second));
    }

    // Remove each key collected
    for (auto& key : keys) {
        key_freq.erase(key);
    }
    is_clearing.store(false);
    // return shadow_freq;
  }
  
  void set_keys_from_past(std::vector<std::pair<uint64_t,std::string>>& cdf) {
    bool found = false;
    for (auto& it : cdf) {
      found = false;
      {
        FrequencyAccessor acc;
        if (key_freq.find(acc, it.second)) {
          acc->second = it.first;
          found = true;
        }
      }
      if (!found)
      {
        FrequencyAccessor acc;
        key_freq.insert(acc, it.second);
        acc->second = it.first;
      }
    }
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
    accessrate_history.push_back(access_rate);
    return true;
  }

  void set_perf_stats(uint64_t local_size_, uint64_t remote_size_, uint64_t performance_) {
    local_size_history.push_back(local_size_);
    remote_size_history.push_back(remote_size_);
    performance_history.push_back(performance_);
    set_duplications_allowed(local_size_);
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
    if(cache_size * 3 <= block_db_num_entries)
      return cache_size * 3;
    else
      return block_db_num_entries;
  }

  void set_keys_under_l(const std::vector<KeyType>& keys) {
    for (const auto& key : keys) {
      keys_from_past.insert(std::make_pair(key, 0));
    }
  }
  
  void print_shadow_freq_to_a_file(){
    std::ofstream file;
    file.open("shadow_freq.txt");
    std::sort(shadow_freq.begin(), shadow_freq.end(), [](const std::pair<KeyType, uint64_t>& a, const std::pair<KeyType, uint64_t>& b) {
      return a.second > b.second;
    });
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

  void print_access_rate(){
    std::ofstream file;
    file.open("access_rate.txt");
    for (int i = 0; i < accessrate_history.size(); i++){
      file << accessrate_history[i] << ";" << local_size_history[i] << ";" << remote_size_history[i] 
           << ";" << performance_history[i] << ";" << duplication_allowed[i] << ";" << current_duplicates_allowed[i] << ";" << current_duplicates_set[i] << std::endl;
    }
    file << access_rate << std::endl;
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
    print_access_rate();
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
    return current_duplicates.load();
  }

  void set_total_cache_duplication(uint64_t total_cache_duplication_) {
    current_duplicates.store(total_cache_duplication_);
  }

  void increment_total_cache_duplication() {
    current_duplicates.fetch_add(1);
  }

  void decrement_total_cache_duplication() {
    if(current_duplicates.load() > 0){
      current_duplicates.fetch_sub(1);
    }
  }

  uint64_t get_duplications_allowed() {
    return duplications_allowed.load();
  }

  void set_duplications_allowed(uint64_t duplications_allowed_) {
    duplications_allowed.store(duplications_allowed_);
    duplication_allowed.push_back(duplications_allowed_);
    current_duplicates_allowed.push_back(current_duplicates.load());
  }

  bool check_key_duplication(const KeyType &key) {
    int replication_count = rdma_key_value_storage->get_num_cache_index_buffers_containing_key(std::stoi(key.c_str()));
    if (replication_count > 1) {
      return true;
    }
    return false;
  }

  void check_and_set_total_cache_duplication() {
    uint64_t total_cache_duplication = 0;
    for (int i = 0; i < block_db_num_entries; i++) {
      if(exist(std::to_string(i))){
        int replication_count = rdma_key_value_storage->get_num_cache_index_buffers_containing_key(i);
        if (replication_count > 1) {
          total_cache_duplication += replication_count - 1;
        }
      }
    }
    current_duplicates_set.push_back(total_cache_duplication);
    // current_duplicates.store(total_cache_duplication);
  }



private:
  BlockCacheConfig block_cache_config;
  std::shared_ptr<Cache> secm = nullptr;
  std::shared_ptr<RDMAKeyValueStorage> rdma_key_value_storage = nullptr;
  std::atomic<uint64_t> total_accesses;
  std::atomic<uint64_t> current_duplicates;
  std::atomic<uint64_t> duplications_allowed;

  std::atomic<bool> is_clearing;

  uint64_t access_rate;
  uint64_t access_per_itr;

  uint64_t water_mark_local;
  uint64_t water_mark_remote;
  uint64_t water_mark_disk;
  
  uint64_t block_db_num_entries;
  uint64_t cache_size;

  std::vector<uint64_t> accessrate_history;
  std::vector<uint64_t> local_size_history;
  std::vector<uint64_t> remote_size_history;
  std::vector<uint64_t> performance_history;
  
  std::vector<uint64_t> duplication_allowed;
  std::vector<uint64_t> current_duplicates_allowed;
  std::vector<uint64_t> current_duplicates_set;
  
  tbb::concurrent_hash_map<KeyType, uint64_t> key_freq;
  tbb::concurrent_hash_map<KeyType, uint64_t> keys_from_past;
  std::vector<std::pair<KeyType, uint64_t>> shadow_freq;
  std::mutex key_freq_mutex;
  std::mutex clear_freq_lock;
};