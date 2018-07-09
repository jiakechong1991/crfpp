//
//  CRF++ -- Yet Another CRF toolkit
//
//  $Id: feature_cache.h 1588 2007-02-12 09:03:39Z taku $;
//
//  Copyright(C) 2005-2007 Taku Kudo <taku@chasen.org>
//
#ifndef CRFPP_FEATURE_CACHE_H_
#define CRFPP_FEATURE_CACHE_H_

#include <vector>
#include <map>
#include "freelist.h"

namespace CRFPP {

class FeatureCache: public std::vector <int *> {  // 特征函数的类表示
 public:
  void clear() {
    std::vector<int *>::clear();
    feature_freelist_.free();
  }

  void add(const std::vector<int> &);
  void shrink(std::map<int, int> *);

  explicit FeatureCache(): feature_freelist_(8192 * 16) {}
  virtual ~FeatureCache() {}

 private:
  FreeList<int> feature_freelist_;  // 存储的是： 本tagger中的 某个字 用到的特征函数ID
		// 你要知道，一个句子中有多个字，每个字对应一个feature_freelist_(一对特征函数)
};
}
#endif
