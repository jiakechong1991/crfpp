//
//  CRF++ -- Yet Another CRF toolkit
//
//  $Id: feature_index.cpp 1587 2007-02-12 09:00:36Z taku $;
//
//  Copyright(C) 2005-2007 Taku Kudo <taku@chasen.org>
//
#include <iostream>
#include <fstream>
#include <cstring>
#include <set>
#include "common.h"
#include "feature_index.h"

namespace CRFPP {
namespace {
const char *read_ptr(const char **ptr, size_t size) {
  const char *r = *ptr;
  *ptr += size;
  return r;
}

template <class T> static inline void read_static(const char **ptr,
                                                  T *value) {
  const char *r = read_ptr(ptr, sizeof(T));
  memcpy(value, r, sizeof(T));
}

void make_templs(const std::vector<std::string> unigram_templs,
                 const std::vector<std::string> bigram_templs,
                 std::string *templs) {
    // 把所有的模板规则字符串合并进一个字符串里面
  templs->clear();
  for (size_t i = 0; i < unigram_templs.size(); ++i) {
    templs->append(unigram_templs[i]);
    templs->append("\n");
  }
  for (size_t i = 0; i < bigram_templs.size(); ++i) {
    templs->append(bigram_templs[i]);
    templs->append("\n");
  }
  // std::cout << templs << std::endl;

}
}  // namespace

char *Allocator::strdup(const char *p) {  // 拷贝一个新的字符串
  const size_t len = std::strlen(p);
  char *q = char_freelist_->alloc(len + 1);
  std::strcpy(q, p);
  return q;
}

// 重写这个类的构造函数
Allocator::Allocator(size_t thread_num)  // 负责各种内存管理
    : thread_num_(thread_num),
      feature_cache_(new FeatureCache),
      char_freelist_(new FreeList<char>(8192)) {
  init();
}

// 重写这个类的构造函数
Allocator::Allocator()
    : thread_num_(1),
      feature_cache_(new FeatureCache),
      char_freelist_(new FreeList<char>(8192)) {
  init();
}

Allocator::~Allocator() {}

Path *Allocator::newPath(size_t thread_id) {
  return path_freelist_[thread_id].alloc();
}

Node *Allocator::newNode(size_t thread_id) {
  return node_freelist_[thread_id].alloc();
}

void Allocator::clear() {
  feature_cache_->clear();
  char_freelist_->free();
  for (size_t i = 0; i < thread_num_; ++i) {
    path_freelist_[i].free();
    node_freelist_[i].free();
  }
}

void Allocator::clear_freelist(size_t thread_id) {
  path_freelist_[thread_id].free();
  node_freelist_[thread_id].free();
}

FeatureCache *Allocator::feature_cache() const {
  return feature_cache_.get();
}

size_t Allocator::thread_num() const {
  return thread_num_;
}

void Allocator::init() {
  path_freelist_.reset(new FreeList<Path> [thread_num_]);
  node_freelist_.reset(new FreeList<Node> [thread_num_]);
  for (size_t i = 0; i < thread_num_; ++i) {
    path_freelist_[i].set_size(8192 * 16);
    node_freelist_[i].set_size(8192);
  }
}

// 解码阶段使用
int DecoderFeatureIndex::getID(const char *key) const {
  return da_.exactMatchSearch<Darts::DoubleArray::result_type>(key);
}

// 编码阶段使用
int EncoderFeatureIndex::getID(const char *key) const {
			// 把特征函数插入特征字典容器中
  std::map <std::string, std::pair<int, unsigned int> >::iterator
      it = dic_.find(key);  // 查找改特征函数在字典中的索引位置
  if (it == dic_.end()) {  // 如果不在，初始化插入，特征使用次数设置为1
    dic_.insert(std::make_pair
                (std::string(key),
                 std::make_pair(maxid_, static_cast<unsigned int>(1))));
    const int n = maxid_;
	  // maxid_用来分段计数，作为base 5,5+1, 5+2...
	  // 为什么它能折磨写，就是因为，他知道：这些特征模板都会作用一遍，所以如果你是U类模板，那本次就有
	  // y_.size 个，如果是B类模板，那就有y_.size * y_.size个
    maxid_ += (key[0] == 'U' ? y_.size() : y_.size() * y_.size());
    return n;
  } else { // 如果特征函数已经在字典中存在
    it->second.second++;  // 使用次数+1
    return it->second.first; // 特征索引ID   base + 1
  }
  return -1;
}

bool EncoderFeatureIndex::open(const char *template_filename,
                               const char *train_filename) {
  check_max_xsize_ = true;
    // 逐行解析  模板文件    训练文件
  return openTemplate(template_filename) && openTagSet(train_filename);
}

bool EncoderFeatureIndex::openTemplate(const char *filename) {
    // 打开模板文件，并解析
  std::ifstream ifs(WPATH(filename));  // 打开文件句柄
  CHECK_FALSE(ifs) << "open failed: "  << filename;

  std::string line;
  while (std::getline(ifs, line)) { // 逐行读取
	std::cout << "本行模板：" << line << std::endl;
    if (!line[0] || line[0] == '#') {  // 如果本行是# Unigram 这种说明，就跳过
      continue;
    }
      // 看到了吗？这里只支持两种类型的模板规则
    if (line[0] == 'U') {
      unigram_templs_.push_back(line);
    } else if (line[0] == 'B') {
      bigram_templs_.push_back(line);
    } else {
      CHECK_FALSE(true) << "unknown type: " << line << " " << filename;
    }
  }
	// 把所有的模板规则字符串合并进一个字符串里面
  make_templs(unigram_templs_, bigram_templs_, &templs_);
	std::cout << "全部模板字符串：" << templs_  << "结束" << std::endl;


  return true;
}

bool EncoderFeatureIndex::openTagSet(const char *filename) {
    // 读取并解析训练文件
  std::ifstream ifs(WPATH(filename));  // 创建文件句柄
  CHECK_FALSE(ifs) << "no such file or directory: " << filename;

  scoped_fixed_array<char, 8192> line; // 一个数组
  scoped_fixed_array<char *, 1024> column;
  size_t max_size = 0;
  std::set<std::string> candset;  // 这是一个集合是，用来收集所有的训练集中的状态值： BMES等

	// 从句柄ifs中， 逐次读取一行line.size()的这么长的字符串，然后写到line.get()这个buffer中，
  while (ifs.getline(line.get(), line.size())) {
      // 逐行读取并解析训练文件
    if (line[0] == '\0' || line[0] == ' ' || line[0] == '\t') {
      continue;
    }
    const size_t size = tokenize2(line.get(), "\t ",
                                  column.get(), column.size());

	  // 检查每一行训练文件的列数，如果不一致，就报错
	  if (max_size == 0) {
      max_size = size;
    }
    CHECK_FALSE(max_size == size)
        << "inconsistent column size: "
        << max_size << " " << size << " " << filename;
    xsize_ = size - 1;
    candset.insert(column[max_size-1]);
  }


  y_.clear();
  for (std::set<std::string>::iterator it = candset.begin();
       it != candset.end(); ++it) {
	  // std::cout <<"!!!" <<  *it << std::endl;
    y_.push_back(*it);
  }

  ifs.close();

  return true;
}

bool DecoderFeatureIndex::open(const char *model_filename) {
  CHECK_FALSE(mmap_.open(model_filename)) << mmap_.what();
  return openFromArray(mmap_.begin(), mmap_.file_size());
}

bool DecoderFeatureIndex::openFromArray(const char *ptr, size_t size) {
  unsigned int version_ = 0;
  const char *end = ptr + size;
  read_static<unsigned int>(&ptr, &version_);

  CHECK_FALSE(version_ / 100 == version / 100)
      << "model version is different: " << version_
      << " vs " << version;
  int type = 0;
  read_static<int>(&ptr, &type);
  read_static<double>(&ptr, &cost_factor_);
  read_static<unsigned int>(&ptr, &maxid_);
  read_static<unsigned int>(&ptr, &xsize_);

  unsigned int dsize = 0;
  read_static<unsigned int>(&ptr, &dsize);

  unsigned int y_str_size;
  read_static<unsigned int>(&ptr, &y_str_size);
  const char *y_str = read_ptr(&ptr, y_str_size);
  size_t pos = 0;
  while (pos < y_str_size) {
    y_.push_back(y_str + pos);
    while (y_str[pos++] != '\0') {}
  }

  unsigned int tmpl_str_size;
  read_static<unsigned int>(&ptr, &tmpl_str_size);
  const char *tmpl_str = read_ptr(&ptr, tmpl_str_size);
  pos = 0;
  while (pos < tmpl_str_size) {
    const char *v = tmpl_str + pos;
    if (v[0] == '\0') {
      ++pos;
    } else if (v[0] == 'U') {
      unigram_templs_.push_back(v);
    } else if (v[0] == 'B') {
      bigram_templs_.push_back(v);
    } else {
      CHECK_FALSE(true) << "unknown type: " << v;
    }
    while (tmpl_str[pos++] != '\0') {}
  }

  make_templs(unigram_templs_, bigram_templs_, &templs_);

  da_.set_array(const_cast<char *>(ptr));
  ptr += dsize;

  alpha_float_ = reinterpret_cast<const float *>(ptr);
  ptr += sizeof(alpha_float_[0]) * maxid_;

  CHECK_FALSE(ptr == end) << "model file is broken.";

  return true;
}

void EncoderFeatureIndex::shrink(size_t freq, Allocator *allocator) {
	// 检查特征函数字典，如果字典中的某个特征函数使用频次< 指定值freq ， 就删除这个特征函数
  if (freq <= 1) { // 频率<=1 直接崩溃退出
    return;
  }

  std::map<int, int> old2new;
  int new_maxid = 0;

  for (std::map<std::string, std::pair<int, unsigned int> >::iterator
           it = dic_.begin(); it != dic_.end();) {
    const std::string &key = it->first;

    if (it->second.second >= freq) {  // 如果这个特征函数的出现频次 >= freq
	    // 保留这个特征函数
      old2new.insert(std::make_pair(it->second.first, new_maxid));
      it->second.first = new_maxid;
      new_maxid += (key[0] == 'U' ? y_.size() : y_.size() * y_.size());
      ++it;
    } else {
	    // 删除这个特征函数
      dic_.erase(it++);  // 从特征字典中删除 这个特征函数项
    }
  }

  allocator->feature_cache()->shrink(&old2new);

  maxid_ = new_maxid;
}

bool EncoderFeatureIndex::convert(const char *text_filename,
                                  const char *binary_filename) {
  std::ifstream ifs(WPATH(text_filename));

  y_.clear();
  dic_.clear();
  unigram_templs_.clear();
  bigram_templs_.clear();
  xsize_ = 0;
  maxid_ = 0;

  CHECK_FALSE(ifs) << "open failed: " << text_filename;

  scoped_fixed_array<char, 8192> line;
  char *column[8];

  // read header
  while (true) {
    CHECK_FALSE(ifs.getline(line.get(), line.size()))
        << " format error: " << text_filename;

    if (std::strlen(line.get()) == 0) {
      break;
    }

    const size_t size = tokenize(line.get(), "\t ", column, 2);

    CHECK_FALSE(size == 2) << "format error: " << text_filename;

    if (std::strcmp(column[0], "xsize:") == 0) {
      xsize_ = std::atoi(column[1]);
    }

    if (std::strcmp(column[0], "maxid:") == 0) {
      maxid_ = std::atoi(column[1]);
    }
  }

  CHECK_FALSE(maxid_ > 0) << "maxid is not defined: " << text_filename;

  CHECK_FALSE(xsize_ > 0) << "xsize is not defined: " << text_filename;

  while (true) {
    CHECK_FALSE(ifs.getline(line.get(), line.size()))
        << "format error: " << text_filename;
    if (std::strlen(line.get()) == 0) {
      break;
    }
    y_.push_back(line.get());
  }

  while (true) {
    CHECK_FALSE(ifs.getline(line.get(), line.size()))
        << "format error: " << text_filename;
    if (std::strlen(line.get()) == 0) {
      break;
    }
    if (line[0] == 'U') {
      unigram_templs_.push_back(line.get());
    } else if (line[0] == 'B') {
      bigram_templs_.push_back(line.get());
    } else {
      CHECK_FALSE(true) << "unknown type: " << line.get()
                        << " " << text_filename;
    }
  }

  while (true) {
    CHECK_FALSE(ifs.getline(line.get(), line.size()))
        << "format error: " << text_filename;
    if (std::strlen(line.get()) == 0) {
      break;
    }

    const size_t size = tokenize(line.get(), "\t ", column, 2);
    CHECK_FALSE(size == 2) << "format error: " << text_filename;

    dic_.insert(std::make_pair
                (std::string(column[1]),
                 std::make_pair(std::atoi(column[0]),
                                static_cast<unsigned int>(1))));
  }

  std::vector<double> alpha;
  while (ifs.getline(line.get(), line.size())) {
    alpha.push_back(std::atof(line.get()));
  }

  alpha_ = &alpha[0];

  CHECK_FALSE(alpha.size() == maxid_) << " file is broken: "  << text_filename;

  return save(binary_filename, false);
}


bool EncoderFeatureIndex::save(const char *filename,
                               bool textmodelfile) {
  std::vector<char *> key;
  std::vector<int>    val;

  std::string y_str;
  for (size_t i = 0; i < y_.size(); ++i) {
    y_str += y_[i];
    y_str += '\0';
  }

  std::string templ_str;
  for (size_t i = 0; i < unigram_templs_.size(); ++i) {
    templ_str += unigram_templs_[i];
    templ_str += '\0';
  }

  for (size_t i = 0; i < bigram_templs_.size(); ++i) {
    templ_str += bigram_templs_[i];
    templ_str += '\0';
  }

  while ((y_str.size() + templ_str.size()) % 4 != 0) {
    templ_str += '\0';
  }

  for (std::map<std::string, std::pair<int, unsigned int> >::iterator
           it = dic_.begin();
       it != dic_.end(); ++it) {
    key.push_back(const_cast<char *>(it->first.c_str()));
    val.push_back(it->second.first);
  }

  Darts::DoubleArray da;

  CHECK_FALSE(da.build(key.size(), &key[0], 0, &val[0]) == 0)
      << "cannot build double-array";

  std::ofstream bofs;
  bofs.open(WPATH(filename), OUTPUT_MODE);

  CHECK_FALSE(bofs) << "open failed: " << filename;

  unsigned int version_ = version;
  bofs.write(reinterpret_cast<char *>(&version_), sizeof(unsigned int));

  int type = 0;
  bofs.write(reinterpret_cast<char *>(&type), sizeof(type));
  bofs.write(reinterpret_cast<char *>(&cost_factor_), sizeof(cost_factor_));
  bofs.write(reinterpret_cast<char *>(&maxid_), sizeof(maxid_));

  if (max_xsize_ > 0) {
    xsize_ = std::min(xsize_, max_xsize_);
  }
  bofs.write(reinterpret_cast<char *>(&xsize_), sizeof(xsize_));
  unsigned int dsize = da.unit_size() * da.size();
  bofs.write(reinterpret_cast<char *>(&dsize), sizeof(dsize));
  unsigned int size = y_str.size();
  bofs.write(reinterpret_cast<char *>(&size),  sizeof(size));
  bofs.write(const_cast<char *>(y_str.data()), y_str.size());
  size = templ_str.size();
  bofs.write(reinterpret_cast<char *>(&size),  sizeof(size));
  bofs.write(const_cast<char *>(templ_str.data()), templ_str.size());
  bofs.write(reinterpret_cast<const char *>(da.array()), dsize);

  for (size_t i  = 0; i < maxid_; ++i) {
    float alpha = static_cast<float>(alpha_[i]);
    bofs.write(reinterpret_cast<char *>(&alpha), sizeof(alpha));
  }

  bofs.close();

  if (textmodelfile) {
    std::string filename2 = filename;
    filename2 += ".txt";

    std::ofstream tofs(WPATH(filename2.c_str()));

    CHECK_FALSE(tofs) << " no such file or directory: " << filename2;

    // header
    tofs << "version: "     << version_ << std::endl;
    tofs << "cost-factor: " << cost_factor_ << std::endl;
    tofs << "maxid: "       << maxid_ << std::endl;
    tofs << "xsize: "       << xsize_ << std::endl;

    tofs << std::endl;

    // y
    for (size_t i = 0; i < y_.size(); ++i) {
      tofs << y_[i] << std::endl;
    }

    tofs << std::endl;

    // template
    for (size_t i = 0; i < unigram_templs_.size(); ++i) {
      tofs << unigram_templs_[i] << std::endl;
    }

    for (size_t i = 0; i < bigram_templs_.size(); ++i) {
      tofs << bigram_templs_[i] << std::endl;
    }

    tofs << std::endl;

    // dic
    for (std::map<std::string, std::pair<int, unsigned int> >::iterator
             it = dic_.begin();
         it != dic_.end(); ++it) {
      tofs << it->second.first << " " << it->first << std::endl;
    }

    tofs << std::endl;

    tofs.setf(std::ios::fixed, std::ios::floatfield);
    tofs.precision(16);

    for (size_t i  = 0; i < maxid_; ++i) {
      tofs << alpha_[i] << std::endl;
    }
  }

  return true;
}

const char *FeatureIndex::getTemplate() const {
  return templs_.c_str();
}

void FeatureIndex::calcCost(Node *n) const {
	// 计算 状态特征函数(点)  的代价
  n->cost = 0.0;  // 代价清零


	// 计算 cost_factor_*∑(w*f)
#define ADD_COST(T, A)                                                  \
  do { T c = 0;                                                               \
    for (const int *f = n->fvector; *f != -1; ++f) { c += (A)[*f + n->y];  }  \
    n->cost =cost_factor_ *(T)c; } while (0)
	// 解释：
	// f: 改字的特征函数集合(不完整特征)的数组头部
	// n->y: 这个节点的输出状态值（是index值），所以这里可以*f + n->y 这样相加来进行偏移
	// A: alpha_:这是真正特征函数的权值向量:
	// !!一定要知道真正的特征函数个数 = 不完整特征个数 * len(状态)!!

	// c += (A)[*f + n->y] 你一定很好奇：这里看起来不像  w(权重)*f(特征函数)啊!!
	// 其实(A)[*f + n->y]仅仅是权重w, 但是因为f,要么是0,要么是1，所以这个作者直接就合并在了
	// 权重w上.

  if (alpha_float_) {
    ADD_COST(float,  alpha_float_);
  } else {
    ADD_COST(double, alpha_);
  }
#undef ADD_COST
}

void FeatureIndex::calcCost(Path *p) const {
	// 计算 转移特征函数(边) 的代价
  p->cost = 0.0;

	// cost_factor_*∑(w*f)
#define ADD_COST(T, A)                                          \
  { T c = 0.0;                                                  \
    for (const int *f = p->fvector; *f != -1; ++f) {            \
      c += (A)[*f + p->lnode->y * y_.size() + p->rnode->y];     \
    }                                                           \
    p->cost =cost_factor_*(T)c; }

	// 解释：(A)[*f + p->lnode->y * y_.size() + p->rnode->y] 代表这个边的权重
	// *f + p->lnode->y * y_.size() + p->rnode->y  代表这个边的权重所在的index
  if (alpha_float_) {
    ADD_COST(float,  alpha_float_);
  } else {
    ADD_COST(double, alpha_);
  }
}
#undef ADD_COST
}
