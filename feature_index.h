//
//  CRF++ -- Yet Another CRF toolkit
//
//  $Id: feature_index.h 1588 2007-02-12 09:03:39Z taku $;
//
//  Copyright(C) 2005-2007 Taku Kudo <taku@chasen.org>
//
#ifndef CRFPP_FEATURE_INDEX_H_
#define CRFPP_FEATURE_INDEX_H_

#include <vector>
#include <map>
#include <iostream>
#include "common.h"
#include "scoped_ptr.h"
#include "feature_cache.h"
#include "path.h"
#include "node.h"
#include "freelist.h"
#include "mmap.h"
#include "darts.h"

namespace CRFPP {
class TaggerImpl;

class Allocator {  // 这边定义了一个模板类，后面会不断的重写它  (用于内存管理)
 public:
  explicit Allocator(size_t thread_num);
  Allocator();
  virtual ~Allocator();

  char *strdup(const char *str);
  Path *newPath(size_t thread_id); // 为指定的线程创建 path
  Node *newNode(size_t thread_id); // 为指定的线程创建 node
  void clear();  // 清理内存
  void clear_freelist(size_t thread_id);  // 清理某个线程的内存
  FeatureCache *feature_cache() const;  // 返回 缓存的feature
  size_t thread_num() const;

 private:
  void init();

  size_t                       thread_num_;
  scoped_ptr<FeatureCache>     feature_cache_;  // 这个句子的 "字"特征函数集 的 集合
  scoped_ptr<FreeList<char> >  char_freelist_;
  scoped_array< FreeList<Path> > path_freelist_; // 这个句子 构建的 path 列表
  scoped_array< FreeList<Node> > node_freelist_;  // 这个句子构建的 node 列表
};

class FeatureIndex {  // template 的基类
 public:
  static const unsigned int version = MODEL_VERSION;

  size_t size() const  { return maxid_; }  // 最大的特征函数ID，如200，0000
  size_t xsize() const { return xsize_; }  // 返回 讯训练文件的列数
  size_t ysize() const { return y_.size(); }  // 返回 状态集合的len
  const char* y(size_t i) const { return y_[i].c_str(); }
	// 设置 特征函数的权重
	void   set_alpha(const double *alpha) { alpha_ = alpha; }
  const float *alpha_float() { return alpha_float_; }
  const double *alpha() const { return alpha_; }
  void set_cost_factor(double cost_factor) { cost_factor_ = cost_factor; }
  double cost_factor() const { return cost_factor_; }

	// 代价计算函数 --两个重载
  void calcCost(Node *node) const;
  void calcCost(Path *path) const;

	// 构建特征函数，并把特征函数插入字典维护
	// 虚函数
  bool buildFeatures(TaggerImpl *tagger) const;

	// 	构建网络
  void rebuildFeatures(TaggerImpl *tagger) const;

  const char* what() { return what_.str(); }

	// 构造函数
  explicit FeatureIndex(): maxid_(0), alpha_(0), alpha_float_(0),
                           cost_factor_(1.0), xsize_(0),
                           check_max_xsize_(false), max_xsize_(0) {}
  virtual ~FeatureIndex() {}

  const char *getTemplate() const;

 protected:
  virtual int getID(const char *str) const = 0;
  const char *getIndex(const char *&p,
                       size_t pos,
                       const TaggerImpl &tagger) const;
  bool applyRule(string_buffer *os,
                 const char *pattern,
                 size_t pos, const TaggerImpl &tagger) const;

  mutable unsigned int      maxid_;  // 生成的特征的个数,也就是最大的特征函数ID-index
  const double             *alpha_; // 特征函数的权重列表， 里面的每个权重值，就是公式里的 w
  const float              *alpha_float_;  // 用float数据类型存储权重，否则用double格式存储
  double                    cost_factor_;  // 代价因子: 一个衰减因子  cost_factor_*∑(w*f)
  unsigned int              xsize_;  // 训练文件的列数
  bool check_max_xsize_;
  mutable unsigned int      max_xsize_;
  std::vector<std::string>  unigram_templs_;  // 存储U类模板规则的列表，每行相当于一个元素
  std::vector<std::string>  bigram_templs_;  // 存储B类模板规则的列表
  std::vector<std::string>  y_;  // 去重后的状态标记集合
  std::string               templs_;  // 模板文件中的规则，拼成一个大字符串
  whatlog                   what_;
};

    // 这一块应该是模板解析的地方
class EncoderFeatureIndex: public FeatureIndex {
 public:
  bool open(const char *template_filename,
            const char *model_filename);
  bool save(const char *filename, bool emit_textmodelfile);
  bool convert(const char *text_filename,
               const char *binary_filename);
  void shrink(size_t freq, Allocator *allocator);

 private:
  int getID(const char *str) const;
  bool openTemplate(const char *filename);
  bool openTagSet(const char *filename);

	// <特征函数字符串，< 索引序列号(从0递增)，该特征的出现次数> >，所有生成的特征函数都放在这里
	// 特征函数字符串 : U05:毎/日/新, 就是这样产生的特征函数
	// 索引序列号,每增加一个新的特征字符串，就会+1
	// 该特征的出现次数: 如果生成一个同样的"U05:毎/日/新"，表明这个特征被重复使用，然后这个次数就会+1
  mutable std::map<std::string, std::pair<int, unsigned int> > dic_;
};

class DecoderFeatureIndex: public FeatureIndex {
 public:
  bool open(const char *model_filename);
  bool openFromArray(const char *buf, size_t size);

 private:
  Mmap <char> mmap_;
  Darts::DoubleArray da_;
  int getID(const char *str) const;
};
}
#endif
